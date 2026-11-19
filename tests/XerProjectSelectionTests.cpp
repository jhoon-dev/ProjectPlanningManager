#include "persistence/PrimaveraXer.h"
#include "persistence/JsonProjectStore.h"
#include "calculation/Scheduler.h"
#include "domain/CalculationControl.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <iostream>
using namespace ppm;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "read");
    return f.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly), "write");
    check(f.write(bytes) == bytes.size(), "short write");
}
QByteArray modify(const QByteArray &bytes, const QString &name, int row, const QString &field, const QString &value) {
    auto lines = QString::fromUtf8(bytes).split('\n');
    bool inside = false;
    QStringList fields;
    for (auto &line : lines) {
        const bool cr = line.endsWith('\r');
        auto plain = line;
        if (cr)
            plain.chop(1);
        auto cells = plain.split('\t');
        if (cells[0] == "%T")
            inside = cells.value(1) == name;
        else if (inside && cells[0] == "%F")
            fields = cells.mid(1);
        else if (inside && cells[0] == "%R" && row-- == 0) {
            const auto column = fields.indexOf(field);
            check(column >= 0, "field missing");
            cells[column + 1] = value;
            line = cells.join('\t') + (cr ? "\r" : "");
            return lines.join('\n').toUtf8();
        }
    }
    throw std::runtime_error("row missing");
}
QByteArray append(const QByteArray &bytes, const QByteArray &name, const QByteArray &record) {
    auto lines = bytes.split('\n');
    QByteArray out;
    bool inside = false, inserted = false;
    for (const auto &line : lines) {
        if (inside && (line.startsWith("%T\t") || line.startsWith("%E"))) {
            out += record + "\r\n";
            inserted = true;
            inside = false;
        }
        if (line.trimmed() == "%T\t" + name)
            inside = true;
        out += line + '\n';
    }
    check(inserted, "append missing table");
    return out;
}
void rejects(const std::function<void()> &f, const char *code) {
    try {
        f();
    } catch (const AppError &e) {
        check(e.code() == code, "wrong diagnostic code");
        return;
    }
    throw std::runtime_error("expected rejection");
}
const TaskSchedule &row(const ScheduleResult &r, const std::string &id) {
    for (const auto &t : r.tasks)
        if (t.taskId == id)
            return t;
    throw std::runtime_error("timing missing");
}
QByteArray replaceRows(const QByteArray &source, const QByteArray &name, const QList<QByteArray> &rows) {
    QByteArray out;
    bool inside = false, inserted = false;
    for (const auto &line : source.split('\n')) {
        if (inside && (line.startsWith("%T\t") || line.startsWith("%E"))) {
            for (const auto &r : rows)
                out += r + "\r\n";
            inserted = true;
            inside = false;
        }
        if (line.trimmed() == "%T\t" + name)
            inside = true;
        if (!(inside && line.startsWith("%R\t")))
            out += line + '\n';
    }
    check(inserted, "replace missing table");
    return out;
}
QByteArray large(const QByteArray &source, int count) {
    QList<QByteArray> tasks, links;
    for (int i = 0; i < count; ++i) {
        const bool b = i >= count / 2;
        const auto pid = b ? "200" : "100";
        QStringList task{"%R",
                         QString::number(50000 + i),
                         pid,
                         b ? "1011" : "11",
                         b ? "502" : "1",
                         QString("T%1").arg(i),
                         QString("작업 %1").arg(i),
                         "TT_Task",
                         "DT_FixedDrtn",
                         "1",
                         "",
                         "",
                         "",
                         "",
                         "TK_NotStart",
                         ""};
        tasks.push_back(task.join('\t').toUtf8());
        if (i && i != count / 2)
            links.push_back(QString("%R\t%1\t%2\t%3\t%4\t%4\tPR_FS\t0")
                                .arg(200000 + i)
                                .arg(50000 + i)
                                .arg(49999 + i)
                                .arg(pid)
                                .toUtf8());
    }
    auto bytes = replaceRows(source, "TASK", tasks);
    bytes = replaceRows(bytes, "TASKPRED", links);
    return replaceRows(bytes, "TASKRSRC", {});
}
void benchmark(const QByteArray &source) {
    QJsonArray metrics;
    for (int count : {1000, 10000}) {
        const auto bytes = large(source, count);
        auto start = std::chrono::steady_clock::now();
        const auto inventory = listPrimaveraXerProjects(bytes);
        auto listed = std::chrono::steady_clock::now();
        const auto e = importPrimaveraXer(bytes, "large.xer", XerEncoding::Utf8, "200");
        auto imported = std::chrono::steady_clock::now();
        check(inventory.size() == 2 && e.project.tasks.size() == std::size_t(count / 2) && e.project.resources.empty(),
              "large selection expanded wrong scope");
        check(calculate(e.project).status == CalculationStatus::Valid, "large selected chain invalid");
        metrics.append(
            QJsonObject{{"input_tasks", count},
                        {"selected_tasks", count / 2},
                        {"bytes", qint64(bytes.size())},
                        {"inventory_ms", std::chrono::duration<double, std::milli>(listed - start).count()},
                        {"import_ms", std::chrono::duration<double, std::milli>(imported - listed).count()}});
    }
    rejects([&] { listPrimaveraXerProjects(large(source, 10002)); }, "FILE_PRIMAVERAXER_024");
    const auto bytes = large(source, 10000);
    auto control = std::make_shared<CalculationControl>();
    auto worker = std::async(std::launch::async, [&] {
        EvaluationScope scope(control);
        try {
            (void)listPrimaveraXerProjects(bytes);
        } catch (const CalculationCancelled &) {
            return true;
        }
        return false;
    });
    while (control->checkpoints.load() < 32 &&
           worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        std::this_thread::yield();
    control->cancel();
    check(worker.get(), "running inventory ignored cancellation");
    write("xer-selection-qa/core-metrics.json", QJsonDocument(metrics).toJson());
}
void contracts(const QByteArray &source) {
    const auto projects = listPrimaveraXerProjects(source);
    check(projects.size() == 2 && projects[0].id == "100" && projects[1].id == "200" && projects[0].tasks == 6 &&
              projects[1].tasks == 6,
          "inventory wrong");
    rejects([&] { importPrimaveraXer(source, "multi.xer"); }, "FILE_PRIMAVERAXER_042");
    rejects([&] { importPrimaveraXer(source, "multi.xer", XerEncoding::Utf8, "999"); }, "FILE_PRIMAVERAXER_042");
    auto a = importPrimaveraXer(source, "multi.xer", XerEncoding::Utf8, "100");
    auto b = importPrimaveraXer(source, "multi.xer", XerEncoding::Utf8, "200");
    check(a.project.name == "현장 A <원문>" && b.project.name == "현장 B" &&
              a.project.documentId != b.project.documentId,
          "project identity/name mixed");
    check(a.project.tasks.size() == 6 && a.project.resources.size() == 1 && a.project.calendars.size() == 1 &&
              a.project.assignments.size() == 2,
          "A pruning wrong");
    check(b.project.tasks.size() == 6 && b.project.resources.size() == 2 && b.project.calendars.size() == 2 &&
              b.project.assignments.size() == 2,
          "B closure wrong");
    check(a.project.tasks.front().id == "xer-task-20" && b.project.tasks.front().id == "xer-task-1020" &&
              a.project.tasks.front().durationMinutes == 480 && b.project.tasks.front().durationMinutes == 120,
          "tasks leaked across projects");
    auto sa = calculatePlan(a.project), sb = calculatePlan(b.project);
    check(sa.status == CalculationStatus::Valid && sb.status == CalculationStatus::Valid, "selection schedule invalid");
    check(row(sa, "xer-task-20").finish == "2026-10-26 18:00" && row(sb, "xer-task-1020").finish == "2026-10-26 11:00",
          "independent hand dates wrong");
    auto selected = a.report["projectSelection"].toObject();
    check(selected["sourceProjectCount"] == 2 && selected["selectedProjectId"] == "100" && a.xer == source,
          "selection/original not recorded");
    check(a.report["source"].toObject()["sha256"].toString() ==
              QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex()),
          "original digest wrong");
    check(primaveraXerPreview(a).contains("선택하지 않은 프로젝트"), "preview missing whole source notice");
    // Unsupported semantics in the unselected project do not contaminate the selected projection.
    auto unsupported = modify(source, "TASK", 6, "task_type", "TT_LOE");
    check(importPrimaveraXer(unsupported, "multi.xer", XerEncoding::Utf8, "100").project.tasks.size() == 6,
          "unselected task rejected A");
    rejects([&] { importPrimaveraXer(unsupported, "multi.xer", XerEncoding::Utf8, "200"); }, "FILE_PRIMAVERAXER_064");
    const auto material = modify(source, "RSRC", 2, "rsrc_type", "RT_Mat");
    check(importPrimaveraXer(material, "multi.xer", XerEncoding::Utf8, "100").project.resources.size() == 1,
          "unselected material resource contaminated A");
    rejects([&] { importPrimaveraXer(material, "multi.xer", XerEncoding::Utf8, "200"); }, "FILE_PRIMAVERAXER_070");
    const auto unused = modify(source, "CALENDAR", 3, "clndr_data", "invalid-unused");
    check(importPrimaveraXer(unused, "multi.xer", XerEncoding::Utf8, "200").project.calendars.size() == 2,
          "unused calendar semantic validation leaked into projection");
    const auto invalidCalendar = modify(source, "CALENDAR", 2, "clndr_data", "invalid-selected");
    check(importPrimaveraXer(invalidCalendar, "multi.xer", XerEncoding::Utf8, "100").project.calendars.size() == 1,
          "unselected invalid calendar contaminated A");
    rejects([&] { importPrimaveraXer(invalidCalendar, "multi.xer", XerEncoding::Utf8, "200"); },
            "FILE_PRIMAVERAXER_028");
    check(importPrimaveraXer(modify(source, "CALENDAR", 0, "proj_id", "00"), "zero.xer", XerEncoding::Utf8, "200")
                  .project.calendars.size() == 2,
          "numeric zero global calendar owner rejected");
    auto incoming = append(source, "TASKPRED", "%R\t9000\t30\t1020\t100\t200\tPR_FS\t0");
    auto inv = listPrimaveraXerProjects(incoming);
    check(inv[0].externalIncoming == 1 && inv[1].externalOutgoing == 1, "external relation inventory wrong");
    for (const auto &id : {"100", "200"})
        rejects([&] { importPrimaveraXer(incoming, "cross.xer", XerEncoding::Utf8, id); }, "FILE_PRIMAVERAXER_026");
    auto absent = append(source, "TASKPRED", "%R\t9000\t30\t99999\t100\t999\tPR_FS\t0");
    check(listPrimaveraXerProjects(absent)[0].externalIncoming == 1, "absent external project not counted");
    rejects([&] { importPrimaveraXer(absent, "cross.xer", XerEncoding::Utf8, "100"); }, "FILE_PRIMAVERAXER_026");
    // Corrupt references and IDs cannot be made safe by selecting a smaller scope.
    rejects([&] { listPrimaveraXerProjects(modify(source, "PROJECT", 1, "proj_id", "100")); }, "FILE_PRIMAVERAXER_025");
    rejects([&] { listPrimaveraXerProjects(modify(source, "TASK", 6, "task_id", "20")); }, "FILE_PRIMAVERAXER_025");
    rejects([&] { listPrimaveraXerProjects(modify(source, "TASK", 6, "proj_id", "999")); }, "FILE_PRIMAVERAXER_026");
    rejects([&] { importPrimaveraXer(modify(source, "TASK", 0, "wbs_id", "1011"), "bad", XerEncoding::Utf8, "100"); },
            "FILE_PRIMAVERAXER_063");
    rejects(
        [&] {
            importPrimaveraXer(modify(source, "TASKPRED", 0, "pred_task_id", "1020"), "bad", XerEncoding::Utf8, "100");
        },
        "FILE_PRIMAVERAXER_068");
    rejects(
        [&] { importPrimaveraXer(modify(source, "TASKRSRC", 0, "task_id", "1020"), "bad", XerEncoding::Utf8, "100"); },
        "FILE_PRIMAVERAXER_072");
    rejects(
        [&] { importPrimaveraXer(modify(source, "TASKRSRC", 0, "rsrc_id", "99999"), "bad", XerEncoding::Utf8, "100"); },
        "FILE_PRIMAVERAXER_072");
    rejects([&] { importPrimaveraXer(modify(source, "TASK", 0, "clndr_id", "502"), "bad", XerEncoding::Utf8, "100"); },
            "FILE_PRIMAVERAXER_044");
    rejects(
        [&] {
            importPrimaveraXer(modify(source, "CALENDAR", 2, "base_clndr_id", "9999"), "bad", XerEncoding::Utf8, "200");
        },
        "FILE_PRIMAVERAXER_045");
    rejects(
        [&] {
            importPrimaveraXer(modify(source, "CALENDAR", 2, "base_clndr_id", "502"), "bad", XerEncoding::Utf8, "200");
        },
        "FILE_PRIMAVERAXER_045");
    auto tooLarge = source + QByteArray(16 * 1024 * 1024, ' ');
    rejects([&] { listPrimaveraXerProjects(tooLarge); }, "FILE_PRIMAVERAXER_001");
    bool cancelled = false;
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    try {
        EvaluationScope scope(control);
        listPrimaveraXerProjects(source);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "inventory cancellation ignored");
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto destination = temp.filePath("selected");
    publishPrimaveraXer(b, destination);
    check(read(QDir(destination).filePath("original.xer")) == source, "published original bytes changed");
    auto opened = store.load(QDir(destination).filePath("project.ppm").toStdString());
    check(opened.project == b.project, "published project differs");
    rejects([&] { publishPrimaveraXer(a, destination); }, "FILE_EXCHANGEPACKAGE_002");
    check(store.load(QDir(destination).filePath("project.ppm").toStdString()).project == b.project,
          "existing publication replaced");
    QDir().mkpath("xer-selection-qa");
    write("xer-selection-qa/original.xer", source);
    write("xer-selection-qa/selected-a.xer", exportPrimaveraXer(a.project).xer);
    write("xer-selection-qa/selected-b.xer", exportPrimaveraXer(b.project).xer);
    write("xer-selection-qa/selected-a.json", QJsonDocument(a.report).toJson());
    write("xer-selection-qa/selected-b.json", QJsonDocument(b.report).toJson());
    write("xer-selection-qa/contract.json", QJsonDocument(QJsonObject{{"projects", 2},
                                                                      {"A_tasks", 6},
                                                                      {"B_tasks", 6},
                                                                      {"A_resources", 1},
                                                                      {"B_resources", 2},
                                                                      {"A_calendars", 1},
                                                                      {"B_calendars", 2},
                                                                      {"A_finish", "2026-10-26 18:00"},
                                                                      {"B_finish", "2026-10-26 11:00"},
                                                                      {"external_relations_rejected", true},
                                                                      {"original_unchanged", true}})
                                                .toJson());
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto source = read(QString::fromUtf8(PPM_XER_MULTI_FIXTURE));
        contracts(source);
        benchmark(source);
        std::cout << "XER selection: scope/ownership/closure/external links/identity/publication/cancel passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
