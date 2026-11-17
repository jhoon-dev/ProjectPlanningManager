#include "domain/NativeSchema.h"
#include "application/Document.h"
#include "domain/Classification.h"
#include "domain/XerSourceRelink.h"
#include "persistence/XerSourceImport.h"
#include "persistence/XerFinancialJson.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "ui/XerSourceRelinkDialog.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace jobs;
namespace {
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f) {
    bool bad = false;
    try {
        f();
    } catch (const std::exception &) {
        bad = true;
    }
    check(bad, "expected rejection");
}
void write(const QString &p, const QByteArray &b) {
    QFile f(p);
    check(f.open(QIODevice::WriteOnly) && f.write(b) == b.size(), "write evidence");
}
QByteArray envelope(const Project &p, int schema) {
    auto body = json::encodeProject(p);
    return QJsonDocument(
               QJsonObject{{"format", "JOBS"},
                           {"schemaVersion", schema},
                           {"payload", body},
                           {"sha256", QString::fromLatin1(
                                          QCryptographicHash::hash(QJsonDocument(body).toJson(QJsonDocument::Compact),
                                                                   QCryptographicHash::Sha256)
                                              .toHex())}})
        .toJson();
}
QByteArray version(const QByteArray &input) {
    auto lines = QString::fromUtf8(input).split('\n');
    QStringList fields;
    for (auto &line : lines) {
        auto c = line.split('\t');
        if (c[0] == "%F")
            fields = c.mid(1);
        if (c[0] == "%R")
            for (int i = 0; i < fields.size(); ++i) {
                auto &v = c[i + 1];
                if ((fields[i] == "task_id" || fields[i] == "pred_task_id" || fields[i] == "rsrc_id") && !v.isEmpty())
                    v = QString::number(v.toInt() + 1000);
                if (fields[i] == "task_name" && v == "A 설계")
                    v = "새 원본 설계";
                if (fields[i] == "rsrc_name" && v.contains("설계자"))
                    v = "새 원본 설계자";
                if (fields[i] == "proj_short_name")
                    v += "-v2";
            }
        line = c.join('\t');
    }
    return lines.join('\n').toUtf8();
}
XerRelinkAudit linked(XerRelinkAudit a, const Project &p) {
    a.incomingLinks = suggestXerLinks(a.incoming, p);
    a.previousLinks = suggestXerLinks(*activeXerFinancials(p), p);
    for (auto &l : a.incomingLinks) {
        if (l.sourceId == "xer-task-1020")
            l.targetId = p.tasks.front().id;
        if (l.sourceId == "xer-resource-1099")
            l.targetId = p.resources.front().id;
    }
    a.confirmed = true;
    return a;
}
XerRelinkSubmission submission(XerRelinkAudit a) {
    return {std::move(a), {{}, "연결 작성자", "새 원본과 삭제 ID 검토", "2026-11-08T02:00:00Z"}};
}
auto prepare(Document &d, XerRelinkAudit a,
             std::shared_ptr<CalculationControl> c = std::make_shared<CalculationControl>()) {
    return Document::prepareEdit(d.makeEditRequest(XerRelinkEdit{submission(std::move(a)), xerFinancialProjectionHash}),
                                 std::move(c));
}
void immutable(const Project &a, const Project &b) {
    check(a.xerFinancials == b.xerFinancials && a.periodActuals == b.periodActuals && a.costEntries == b.costEntries &&
              a.progress == b.progress && a.baselines == b.baselines &&
              static_cast<const PlanInput &>(a) == static_cast<const PlanInput &>(b) && a.statusAt == b.statusAt,
          "relink changed native/source data");
}
void wait(const std::function<bool()> &done) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < 30000) {
        QApplication::processEvents();
        QTest::qWait(10);
    }
    check(done(), "UI load timeout");
}
void ui(Document &d, const QString &path, bool small, bool publish) {
    const auto before = *d.project();
    XerSourceRelinkDialog dialog(d);
    dialog.show();
    QApplication::processEvents();
    if (small)
        dialog.resize(800, 600);
    dialog.loadSource(path);
    auto *open = dialog.findChild<QPushButton *>("xerRelinkOpen");
    wait([&] { return open->isEnabled(); });
    auto *table = dialog.findChild<QTableView *>("xerRelinkLinks");
    auto *confirm = dialog.findChild<QCheckBox *>("xerRelinkConfirm");
    auto *review = dialog.findChild<QPushButton *>("xerRelinkReview");
    check(table->model()->rowCount() == 16 && !review->isEnabled(), "source/mapping UI absent");
    check(dialog.findChild<QLabel *>("xerRelinkSource")->textFormat() == Qt::PlainText, "source treated as markup");
    check(table->model()->setData(table->model()->index(0, 4), QString::fromStdString(before.tasks[0].id),
                                  Qt::EditRole) &&
              table->model()->setData(table->model()->index(6, 4), QString::fromStdString(before.resources[0].id),
                                      Qt::EditRole),
          "manual connection UI");
    confirm->setChecked(true);
    check(review->isEnabled() && *d.project() == before, "load changed document");
    dialog.grab().save(small ? "xer-relink-qa/mapping-small.png" : "xer-relink-qa/mapping.png");
    bool seen = false, started = false;
    QTimer timer;
    timer.setInterval(20);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *child = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!child || child->objectName() != "xerFinancialMergeDialog")
            return;
        seen = true;
        if (!started) {
            started = true;
            child->findChild<QLineEdit *>("xerMergeReporter")->setText("UI 연결자");
            child->findChild<QLineEdit *>("xerMergeReason")->setText("후속 원본의 ID 연결 확인");
            child->findChild<QPushButton *>("xerMergeAnalyze")->click();
            return;
        }
        auto *submit = child->findChild<QPushButton *>("xerMergeSubmit");
        if (submit->isEnabled()) {
            child->grab().save("xer-relink-qa/review.png");
            if (publish)
                submit->click();
            else
                child->reject();
        }
    });
    timer.start();
    review->click();
    timer.stop();
    check(seen, "merge review not opened");
    if (publish) {
        immutable(before, *d.project());
        check(d.project()->xerFinancialRevisions.back().relink.has_value(), "UI did not register source");
    } else {
        dialog.reject();
        check(*d.project() == before, "close registered source");
    }
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setFont(QFont("Malgun Gothic", 9));
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, [] { std::_Exit(3); });
    guard.start(150000);
    try {
        QDir().mkpath("xer-relink-qa");
        auto bytes = readPrimaveraXer(QString::fromUtf8(JOBS_XER_FINANCIAL_FIXTURE));
        auto next = version(bytes);
        auto p = importPrimaveraXer(bytes, "original.xer").project;
        QTemporaryDir temp;
        JsonProjectStore store;
        int seq = 0;
        auto document = [&](const Project &v) {
            auto path = (temp.path() + QString("/%1.ppm").arg(++seq)).toStdString();
            store.save(path, v, {});
            auto d = std::make_unique<Document>(store);
            d->open(path);
            return d;
        };
        auto d = document(p);
        std::string deleted, replacement, name;
        for (const auto &t : p.tasks)
            if (!d->previewRemoveTasks({t.id}).protectedTaskIds.size()) {
                auto it = std::find_if(p.xerFinancials->tasks.begin(), p.xerFinancials->tasks.end(),
                                       [&](const auto &f) { return f.id == t.id; });
                if (it != p.xerFinancials->tasks.end() && !it->actualMinor &&
                    std::none_of(p.assignments.begin(), p.assignments.end(),
                                 [&](const auto &a) { return a.taskId == t.id; })) {
                    deleted = t.id;
                    name = t.name;
                    break;
                }
            }
        check(!deleted.empty(), "no deletable unobserved fixture task");
        d->removeTasks({deleted});
        d->addTask(name);
        replacement = d->project()->tasks.back().id;
        d->setDuration(d->project()->tasks.size() - 1, 60);
        d->setStatusAt("2026-10-29 18:00");
        const auto before = *d->project();
        auto raw = importXerRelinkSource(next, "version2.xer", XerEncoding::Utf8, "100", *before.statusAt);
        check(raw.incoming.tasks[0].id == "xer-task-1020" && raw.incoming.sourceSha256 != p.xerFinancials->sourceSha256,
              "new source identities/hash");
        auto audit = linked(raw, before);
        auto old = std::find_if(audit.previousLinks.begin(), audit.previousLinks.end(),
                                [&](const auto &l) { return l.sourceId == deleted; });
        check(old != audit.previousLinks.end() && old->targetId == replacement, "deleted ID candidate not reconnected");
        auto proposal = projectForXerRelink(before, audit);
        check(mergeXerFinancials(proposal).ready(), "candidate merge not ready");
        auto prepared = prepare(*d, audit);
        if (prepared->failure())
            std::cerr << prepared->error() << '\n';
        check(prepared->state() == EditState::Ready && *d->project() == before,
              "relink preparation failed/changed data");
        check(d->applyPreparedEdit(*prepared) == EditState::Applied, "relink commit");
        immutable(before, *d->project());
        check(nativeSchemaFor(*d->project()) == 28 && xerFinancialsCurrent(*d->project()) && !d->canUndo(),
              "schema28/current/audit");
        check(d->project()->xerFinancialRevisions.back().relink->incoming == raw.incoming, "incoming archive changed");
        d->recordXerFinancialMerge({{}, "병합자", "연결 후 병합", "2026-11-08T03:00:00Z"}, xerFinancialProjectionHash);
        immutable(before, *d->project());
        check(xerFinancialsCurrent(*d->project()), "post-version merge metadata");
        d->save();
        check(store.load(d->path()).project == *d->project(), "schema28 roundtrip");
        rejects([&] { decodeProjectFile(envelope(*d->project(), 27)); });
        check(decodeProjectFile(envelope(*d->project(), 30)) == *d->project(), "legacy data in schema30 envelope");
        check(decodeProjectFile(envelope(*d->project(), 52)) == *d->project(), "supported schema52");
        rejects([&] { decodeProjectFile(envelope(*d->project(), maximumNativeSchema + 1)); });
        for (int kind = 0; kind < 6; ++kind) {
            auto bad = audit;
            if (kind == 0)
                bad.confirmed = false;
            if (kind == 1)
                bad.incomingLinks[0].targetId = "missing";
            if (kind == 2)
                bad.incomingLinks[1].targetId = bad.incomingLinks[0].targetId;
            if (kind == 3)
                bad.incomingLinks[1].targetId.clear();
            if (kind == 4)
                bad.incoming.resources[0].type = "equipment";
            if (kind == 5)
                bad.incoming.currency = "KRW";
            rejects([&] { projectForXerRelink(before, bad); });
        }
        auto ambiguous = before;
        ambiguous.tasks[0].name = raw.incoming.tasks[0].name;
        ambiguous.tasks[1].name = raw.incoming.tasks[0].name;
        check(suggestXerLinks(raw.incoming, ambiguous)[0].targetId.empty(), "duplicate name guessed");
        auto retired = document(p);
        retired->removeTasks({deleted});
        auto retire = linked(raw, *retired->project());
        for (auto &l : retire.previousLinks)
            if (l.sourceId == deleted)
                l.targetId.clear();
        for (auto &l : retire.incomingLinks)
            if (l.kind == "task") {
                const auto nameIt = std::find_if(raw.incoming.tasks.begin(), raw.incoming.tasks.end(),
                                                 [&](const auto &t) { return t.id == l.sourceId && t.name == name; });
                if (nameIt != raw.incoming.tasks.end())
                    l.targetId.clear();
            }
        retired->recordXerRelink(submission(retire), xerFinancialProjectionHash);
        check(retired->project()->tasks.size() == p.tasks.size() - 1, "retirement changed native scope");
        auto plain = p;
        plain.xerFinancials.reset();
        plain.statusAt.reset();
        plain.periodActuals.clear();
        plain.costEntries.clear();
        plain.progress.clear();
        auto plainBytes = exportPrimaveraXer(plain).xer;
        auto infos = listPrimaveraXerProjects(plainBytes);
        auto dictionary =
            importXerRelinkSource(plainBytes, "plan-only.xer", XerEncoding::Utf8, infos[0].id, *before.statusAt);
        check(!dictionary.sourceStatusAt && dictionary.incoming.periods.empty(), "undated dictionary mislabeled");
        auto dictionaryAudit = linked(dictionary, before);
        auto planDoc = document(before);
        planDoc->recordXerRelink(submission(dictionaryAudit), xerFinancialProjectionHash);
        immutable(before, *planDoc->project());
        dictionaryAudit.replaceMissingData = true;
        check(!mergeXerFinancials(projectForXerRelink(before, dictionaryAudit)).ready(),
              "blank source erased native without review");
        auto exported = exportPrimaveraXer(*d->project());
        auto back = importPrimaveraXer(exported.xer, "relinked.xer").project;
        check(costAsOf(back, back.tasks[0].id, *back.statusAt)->actualMinor == 12280, "XER AC after mapping");
        write("xer-relink-qa/source.xer", bytes);
        write("xer-relink-qa/version2.xer", next);
        write("xer-relink-qa/export.xer", exported.xer);
        write("xer-relink-qa/export-report.json", QJsonDocument(exported.report).toJson());
        write("xer-relink-qa/before.json", QJsonDocument(json::encodeProject(before)).toJson());
        write("xer-relink-qa/relinked.json", QJsonDocument(json::encodeProject(*d->project())).toJson());
        write("xer-relink-qa/relinked.ppm", envelope(*d->project(), 28));
        auto body = json::encodeProject(*d->project());
        for (int kind = 0; kind < 3; ++kind) {
            auto bad = body;
            auto history = bad["xerFinancialRevisions"].toArray();
            auto r = history[0].toObject();
            auto a = r["relink"].toObject();
            if (kind == 0)
                a["confirmed"] = false;
            if (kind == 1)
                a["policy"] = "unknown";
            if (kind == 2) {
                auto links = a["incomingLinks"].toArray();
                auto l = links[0].toObject();
                l["targetId"] = "missing";
                links[0] = l;
                a["incomingLinks"] = links;
            }
            r["relink"] = a;
            history[0] = r;
            bad["xerFinancialRevisions"] = history;
            rejects([&] { json::decodeProject(bad, 28); });
        }
        d->renameTask(0, "복구 이름");
        JsonRecoveryStore recovery(temp.path() + "/recovery");
        auto s = recovery.newSession();
        s->checkpoint(d->recoverySnapshot("2026-11-08T04:00:00Z"));
        s.reset();
        auto scan = recovery.scan();
        check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema28 recovery");
        auto claim = recovery.claim(scan.candidates[0]);
        Document restored(store);
        restored.recover(claim->snapshot);
        check(restored.project()->xerFinancialRevisions == d->project()->xerFinancialRevisions,
              "recovery lost source links");
        d->undo();
        d->saveAs((temp.path() + "/copy.ppm").toStdString());
        check(xerFinancialsCurrent(*d->project()), "copy lost source binding");
        auto pending = prepare(*d, linked(raw, *d->project()));
        d->open(d->path());
        check(d->applyPreparedEdit(*pending) == EditState::Stale, "new session accepted relink");
        auto c = std::make_shared<CalculationControl>();
        c->cancel();
        auto cancelled = prepare(*d, linked(raw, *d->project()), c);
        check(cancelled->state() == EditState::Cancelled, "cancel not honored");
        Document ro(store);
        ro.open(d->path(), true);
        rejects([&] { ro.recordXerRelink(submission(audit), xerFinancialProjectionHash); });
        XerSourceRelinkDialog readOnly(ro);
        check(!readOnly.findChild<QPushButton *>("xerRelinkOpen")->isEnabled(), "readonly source load active");
        const auto versionPath = temp.path() + "/version2.xer";
        write(versionPath, next);
        auto cancelDoc = document(before);
        const auto cancelBefore = *cancelDoc->project();
        XerSourceRelinkDialog cancelRead(*cancelDoc);
        cancelRead.loadSource(versionPath);
        cancelRead.findChild<QPushButton *>("xerRelinkCancel")->click();
        wait([&] { return cancelRead.findChild<QPushButton *>("xerRelinkOpen")->isEnabled(); });
        check(cancelRead.findChild<QTableView *>("xerRelinkLinks")->model()->rowCount() == 0 &&
                  *cancelDoc->project() == cancelBefore,
              "cancelled file read retained new source");
        auto first = exportPrimaveraXer(p), second = exportPrimaveraXer(p);
        second.project.name = "선택한 후속 프로젝트";
        second = exportPrimaveraXer(second.project);
        std::vector<PrimaveraXerExchange> projects{first, second};
        auto multiple = combinePrimaveraXer(projects);
        auto projectList = listPrimaveraXerProjects(multiple);
        check(projectList.size() == 2, "multi-source fixture");
        auto selected =
            importXerRelinkSource(multiple, "multiple.xer", XerEncoding::Utf8, projectList[1].id, *before.statusAt);
        check(selected.projectName == "선택한 후속 프로젝트" && selected.incoming.tasks.size() == p.tasks.size(),
              "selected source project leaked scope");
        rejects(
            [&] { importXerRelinkSource(multiple, "multiple.xer", XerEncoding::Utf8, "missing", *before.statusAt); });
        auto uiDoc = document(before);
        ui(*uiDoc, versionPath, true, false);
        ui(*uiDoc, versionPath, false, true);
        MainWindow window(*uiDoc);
        auto *action = window.findChild<QAction *>("relinkXerSource");
        check(action && action->isEnabled(), "source menu absent");
        bool seen = false;
        QTimer timer;
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, [&] {
            auto *v = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (v && v->objectName() == "xerSourceRelinkDialog") {
                seen = true;
                v->reject();
            }
        });
        timer.start();
        action->trigger();
        timer.stop();
        check(seen, "source menu not opened");
        std::cout << "PASS: new XER source/schema28, changed/deleted ID maps and unobserved retirement, source "
                     "archive/native fact preservation, ambiguity/type/zero guards, undated dictionary, post-version "
                     "merge, recovery/copy/stale/cancel/read-only, async file and native review UI\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
