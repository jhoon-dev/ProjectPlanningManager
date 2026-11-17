#include "persistence/XerBatch.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonWorkspaceStore.h"
#include "domain/CalculationControl.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
using namespace jobs;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "read");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write");
}
void rejects(const std::function<void()> &call) {
    try {
        call();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
QByteArray edit(QByteArray bytes, const QString &table, const QString &field, const QString &value) {
    auto lines = QString::fromUtf8(bytes).split('\n');
    bool inside = false;
    QStringList fields;
    for (auto &line : lines) {
        const bool cr = line.endsWith('\r');
        auto cells = line.trimmed().split('\t');
        if (cells[0] == "%T")
            inside = cells.value(1) == table;
        if (inside && cells[0] == "%F")
            fields = cells.mid(1);
        if (inside && cells[0] == "%R") {
            // Keep trailing empty cells intact.
            auto raw = line;
            if (cr)
                raw.chop(1);
            cells = raw.split('\t');
            check(fields.contains(field), "edit field");
            cells[fields.indexOf(field) + 1] = value;
            line = cells.join('\t') + (cr ? "\r" : "");
            return lines.join('\n').toUtf8();
        }
    }
    throw std::runtime_error("edit row missing");
}
void codes(const Project &p) {
    check(p.codeTypes.size() == 2, "code types");
    check(p.codeTypes[0].values.size() == 3 && p.codeTypes[1].values.size() == 1, "code values");
    check(p.codeTypes[0].values[1].parentId == p.codeTypes[0].values[0].id, "code parent");
    check(p.tasks[0].attributes.size() == 2 && p.tasks[1].attributes.size() == 1, "task code assignments");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto bytes = read(JOBS_XER_MULTI_FIXTURE);
        auto imported = importXerBatch({{bytes, "two.xer", XerEncoding::Utf8, {"200", "100"}}});
        check(imported.projects.size() == 2 && imported.workspace.projects.size() == 2, "batch import count");
        check(imported.projects[0].report["batchSourceProjectId"] == "200", "explicit selection order");
        check(imported.projects[0].project.documentId != imported.projects[1].project.documentId,
              "independent native IDs");
        check(imported.originals.size() == 1 && imported.originals[0] == bytes, "full original preserved once");
        rejects([&] { importXerBatch({{bytes, "empty.xer", XerEncoding::Utf8, {}}}); });
        rejects([&] { importXerBatch({{bytes, "bad.xer", XerEncoding::Utf8, {"100", "100"}}}); });
        rejects([&] { importXerBatch({{bytes, "bad.xer", XerEncoding::Utf8, {"999"}}}); });
        rejects([&] {
            importXerBatch(
                {{bytes, "a.xer", XerEncoding::Utf8, {"100"}}, {bytes, "b.xer", XerEncoding::Utf8, {"200"}}});
        });
        rejects([&] {
            importXerBatch(
                {{bytes, "a.xer", XerEncoding::Utf8, {"100"}}, {"invalid", "b.xer", XerEncoding::Utf8, {"1"}}});
        });
        const auto external = edit(bytes, "TASKPRED", "pred_proj_id", "200");
        rejects([&] { importXerBatch({{external, "external.xer", XerEncoding::Utf8, {"100", "200"}}}); });
        check(imported.workspace.resources.size() == 1 && imported.workspace.bindings.size() == 2,
              "matching source resource not shared");
        auto fallback = edit(bytes, "RSRC", "clndr_id", "");
        auto conflict = importXerBatch({{fallback, "fallback.xer", XerEncoding::Utf8, {"100", "200"}}});
        check(conflict.workspace.resources.empty() && !conflict.report["diagnostics"].toArray().isEmpty(),
              "conflicting calendars silently shared");
        auto otherSource = bytes;
        otherSource.replace("SITE-01", "SITE-X1");
        auto separateSources = importXerBatch({{bytes, "first.xer", XerEncoding::Utf8, {"100"}},
                                               {otherSource, "second.xer", XerEncoding::Utf8, {"100"}}});
        check(separateSources.projects.size() == 2 && separateSources.workspace.resources.empty(),
              "cross-source IDs merged");
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary directory");
        const auto folder = publishXerBatch(imported, temporary.filePath("import"));
        JsonProjectStore files;
        JsonWorkspaceStore workspaces(files);
        auto loaded = workspaces.load(QDir(folder).filePath("workspace.ppmworkspace").toStdString());
        check(loaded.workspace.projects.size() == 2, "load workspace");
        for (std::size_t i = 0; i < loaded.workspace.projects.size(); ++i)
            check(files.load(loaded.workspace.projects[i].path).project == imported.projects[i].project,
                  "native lossless readback");
        check(read(QDir(folder).filePath("originals/001.xer")) == bytes, "published original exact");
        rejects([&] { publishXerBatch(imported, folder); });
        const auto failed = temporary.filePath("failed");
        rejects(
            [&] { publishXerBatch(imported, failed, [] { throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED); }); });
        check(!QFileInfo::exists(failed), "failure published partial result");
        const auto raced = temporary.filePath("raced");
        rejects([&] {
            publishXerBatch(imported, raced, [&] {
                check(QDir().mkdir(raced), "race mkdir");
                write(QDir(raced).filePath("sentinel"), "keep");
            });
        });
        check(read(QDir(raced).filePath("sentinel")) == "keep", "race overwrite");
        auto control = std::make_shared<CalculationControl>();
        {
            EvaluationScope scope(control);
            try {
                publishXerBatch(imported, temporary.filePath("cancelled"), [&] { control->cancel(); });
                throw std::runtime_error("cancel accepted");
            } catch (const CalculationCancelled &) {
            }
        }
        check(!QFileInfo::exists(temporary.filePath("cancelled")), "cancel published");
        check(QDir(temporary.path())
                  .entryList({".ppm-xer-batch-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                  .isEmpty(),
              "stage leaked");
        auto p = imported.projects[1].project;
        p.codeTypes = {{"type/a", "공종", {{"root", "설계", ""}, {"child", "상세", "root"}, {"other", "시공", ""}}},
                       {"type/b", "구역", {{"root", "A구역", ""}}}};
        p.tasks[0].attributes = {{"code:type/a", "child"}, {"code:type/b", "root"}};
        p.tasks[1].attributes = {{"code:type/a", "other"}};
        auto single = exportPrimaveraXer(p);
        codes(importPrimaveraXer(single.xer, "codes.xer").project);
        auto cycle = edit(single.xer, "ACTVCODE", "parent_actv_code_id", "2");
        rejects([&] { importPrimaveraXer(cycle, "cycle.xer"); });
        rejects([&] { importPrimaveraXer(edit(single.xer, "TASKACTV", "actv_code_type_id", "2"), "wrong-type.xer"); });
        rejects(
            [&] { importPrimaveraXer(edit(single.xer, "ACTVCODE", "actv_code_type_id", "99"), "missing-type.xer"); });
        rejects([&] { importPrimaveraXer(edit(single.xer, "TASKACTV", "task_id", "9999"), "missing-task.xer"); });
        auto q = p;
        q.documentId = "second-project";
        q.name = "두 번째 현장";
        q.tasks[0].name = "다른 프로젝트의 같은 native 작업 ID";
        auto batch = exportXerBatch({p, q});
        check(batch.projects[0].report["combinedProjectId"] == "1" &&
                  batch.projects[1].report["combinedProjectId"] == "2",
              "combined project mapping");
        const auto inventory = listPrimaveraXerProjects(batch.combined);
        check(inventory.size() == 2, "combined inventory");
        for (const auto &info : inventory)
            codes(importPrimaveraXer(batch.combined, "combined.xer", XerEncoding::Utf8, info.id).project);
        check(importPrimaveraXer(batch.combined, "combined.xer", XerEncoding::Utf8, "2").project.tasks[0].name ==
                  q.tasks[0].name,
              "cross-project task collision");
        auto otherCurrency = q;
        otherCurrency.currency = "USD";
        rejects([&] { exportXerBatch({p, otherCurrency}); });
        rejects([&] { exportXerBatch({p, p}); });
        rejects([&] { exportXerBatch({}); });
        auto exported = publishXerBatch(batch, temporary.filePath("export"));
        check(read(QDir(exported).filePath("workspace.xer")) == batch.combined, "combined publish");
        auto native = workspaces.load(QDir(exported).filePath("workspace.ppmworkspace").toStdString());
        check(files.load(native.workspace.projects[0].path).project == p &&
                  files.load(native.workspace.projects[1].path).project == q,
              "source snapshots exact");
        check(QDir().rename(exported, temporary.filePath("relocated")), "relocate package");
        auto relocated = workspaces.load(temporary.filePath("relocated/workspace.ppmworkspace").toStdString());
        check(files.load(relocated.workspace.projects[0].path).project == p, "relative references not relocatable");
        auto reimport = importXerBatch({{batch.combined, "combined.xer", XerEncoding::Utf8, {"1", "2"}}});
        check(reimport.workspace.resources.empty(), "separate XER IDs falsely merged by name");
        // Explicit native shared catalog/bindings remain in the package, even though XER uses separate local IDs.
        Workspace shared;
        shared.id = "shared-workspace";
        shared.projects = {{p.documentId, {}, p.name}, {q.documentId, {}, q.name}};
        shared.resources.push_back({"pool", "공유 인력", p.resources[0].type, 100, {}, {}});
        shared.bindings = {{p.documentId, p.resources[0].id, "pool"}, {q.documentId, q.resources[0].id, "pool"}};
        auto withShared = exportXerBatch({p, q}, shared);
        check(withShared.workspace.bindings == shared.bindings && withShared.workspace.resources == shared.resources,
              "native shared catalog lost");
        auto subset = exportXerBatch({p}, shared);
        check(subset.workspace.projects.size() == 1 && subset.workspace.bindings.size() == 1,
              "selected subset binding scope");
        write("xer-batch-export.xer", batch.combined);
        write("xer-batch-export.json", QJsonDocument(batch.projects[1].report).toJson());
        // Stable artifacts for an independent reader.
        if (argc > 1)
            publishXerBatch(withShared, QString::fromLocal8Bit(argv[1]));
        std::cout << "PASS: batch import/export, remapping, codes, native workspace, rollback/race/cancel/relocation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (const auto *appError = dynamic_cast<const AppError *>(&error))
            for (const auto &argument : appError->diagnostic().arguments)
                std::cerr << argument << '\n';
        return 1;
    }
}
