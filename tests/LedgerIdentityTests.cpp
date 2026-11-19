#include "domain/NativeSchema.h"
#include "application/Document.h"
#include "domain/LedgerIdentity.h"
#include "domain/Classification.h"
#include "domain/PeriodActuals.h"
#include "domain/RemainingWork.h"
#include "calculation/Costs.h"
#include "calculation/Resources.h"
#include "formatting/DiagnosticText.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectJson.h"
#include "persistence/XerFinancialJson.h"
#include "persistence/PrimaveraXer.h"
#include "ui/LedgerIdentityDialog.h"
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
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <iostream>
using namespace ppm;
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
    check(bad, "expected identity migration rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write evidence");
}
QByteArray envelope(const Project &p, int version) {
    const auto body = json::encodeProject(p);
    return QJsonDocument(
               QJsonObject{{"format", "PPM"},
                           {"schemaVersion", version},
                           {"payload", body},
                           {"sha256", QString::fromLatin1(
                                          QCryptographicHash::hash(QJsonDocument(body).toJson(QJsonDocument::Compact),
                                                                   QCryptographicHash::Sha256)
                                              .toHex())}})
        .toJson();
}
LedgerIdSubmission submission(const std::string &oldTask, const std::string &task, const std::string &oldResource,
                              const std::string &resource) {
    return {{{"task", oldTask, task}, {"resource", oldResource, resource}},
            "식별 검토자",
            "같은 작업·자원의 새 버전 ID 확인",
            "2026-11-08T11:00:00Z",
            true};
}
auto prepare(Document &d, const LedgerIdSubmission &s,
             std::shared_ptr<CalculationControl> c = std::make_shared<CalculationControl>()) {
    return Document::prepareEdit(d.makeEditRequest(LedgerIdEdit{s}), c);
}
void wait(const std::function<bool()> &done) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < 30000) {
        QApplication::processEvents();
        QTest::qWait(10);
    }
    check(done(), "identity UI timeout");
}
void records(const Project &a, const Project &b, const std::string &oldTask, const std::string &newTask,
             const std::string &oldR, const std::string &newR) {
    auto task = [&](auto &rows) {
        for (auto &e : rows)
            if (e.taskId == oldTask)
                e.taskId = newTask;
    };
    auto progress = a.progress;
    task(progress);
    check(progress == b.progress, "progress values/provenance changed");
    auto costs = a.costEntries;
    task(costs);
    check(costs == b.costEntries, "cost values/provenance changed");
    auto steps = a.stepReports;
    task(steps);
    check(steps == b.stepReports, "Steps values/provenance changed");
    auto periods = a.periodActuals;
    task(periods);
    for (auto &e : periods)
        if (e.resourceId == oldR)
            e.resourceId = newR;
    check(periods == b.periodActuals, "dated facts changed beyond owner IDs");
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
        QDir().mkpath("ledger-id-qa");
        QTemporaryDir temp;
        JsonProjectStore store;
        auto original =
            importPrimaveraXer(readPrimaveraXer(QString::fromUtf8(PPM_XER_FINANCIAL_FIXTURE)), "original.xer").project;
        const auto path = (temp.path() + "/initial.ppm").toStdString();
        store.save(path, original, {});
        Document setup(store);
        setup.open(path);
        const auto oldTask = original.tasks[0].id, oldR = original.resources[0].id;
        ActivityDefinition activity;
        activity.name = "실측 단계";
        activity.taskId = oldTask;
        activity.steps = {{"S1", "설계", 1}, {"S2", "검토", 1}};
        setup.putActivity(activity);
        StepReport report;
        report.taskId = oldTask;
        report.steps = {{{"S1", "설계", 1}, 7500}, {{"S2", "검토", 1}, 2500}};
        report.reporter = "검측자";
        report.reason = "실측 진척";
        report.recordedAt = "2026-11-08T08:00:00Z";
        setup.recordSteps(report);
        setup.saveScenario("이관 전 시나리오", "task-order", 260, "2026-11-08T08:05:00Z", setup.project()->documentId,
                           setup.project()->revision);
        const auto baseline = setup.captureBaseline("이관 전 승인", "PM", "동결 수치", "2026-11-08T08:10:00Z");
        FinancialPeriodSubmission period;
        period.period.name = "잠긴 기간";
        period.period.baselineId = baseline;
        period.period.fromDate = period.period.toDate = "2026-10-26";
        period.reporter = "PM";
        period.reason = "기준선/마감 보존";
        period.recordedAt = "2026-11-08T08:20:00Z";
        period.confirmed = true;
        period.period.id = setup.recordFinancialPeriod(period);
        period.action = "close";
        period.recordedAt = "2026-11-08T08:21:00Z";
        setup.recordFinancialPeriod(period);
        setup.recordXerFinancialMerge({{}, "검토자", "이관 전 원장 대사", "2026-11-08T08:30:00Z"},
                                      xerFinancialProjectionHash);
        XerRelinkAudit relink;
        relink.incoming = *activeXerFinancials(*setup.project());
        relink.projectName = "같은 원본 후속 등록";
        relink.sourceStatusAt = relink.incoming.statusAt;
        relink.incomingLinks = suggestXerLinks(relink.incoming, *setup.project());
        relink.previousLinks = relink.incomingLinks;
        relink.confirmed = true;
        setup.recordXerRelink({relink, {{}, "검토자", "원본 연결 참조 보존", "2026-11-08T08:40:00Z"}},
                              xerFinancialProjectionHash);
        ExternalEvmSubmission evm;
        evm.baselineId = baseline;
        evm.taskIds = {oldTask};
        evm.confirmed = true;
        evm.reporter = "PM";
        evm.reason = "이관 전 외부 지표";
        evm.recordedAt = "2026-11-08T08:50:00Z";
        const auto &source = *activeXerFinancials(*setup.project());
        evm.financialRevisionId = setup.project()->xerFinancialRevisions.back().id;
        evm.sourceSha256 = source.sourceSha256;
        evm.sourceProjectionSha256 = source.projectionSha256;
        for (std::size_t i = 0; i < source.periods.size(); ++i)
            if (source.periods[i].taskId == oldTask && source.periods[i].resourceId.empty() &&
                (source.periods[i].bcwp || source.periods[i].bcws))
                evm.rows.push_back({i, 0, 0});
        setup.recordExternalEvm(evm);
        XerScheduleSubmission version;
        version.source.input = *setup.project();
        version.source.input.tasks[0].name = "새 XER 설계";
        version.source.input.tasks[0].durationMinutes = 720;
        version.source.sourceName = "new-plan.xer";
        version.source.projectId = "next";
        version.source.sourceSha256 = std::string(64, 'b');
        version.source.importedAt = "2026-11-08T09:00:00Z";
        version.source.reportJson = "{}";
        version.schedule = true;
        version.wbs = false;
        version.calendars = false;
        version.confirmed = true;
        version.reporter = "계획자";
        version.reason = "명시 신규 ID로 채택";
        version.recordedAt = "2026-11-08T09:10:00Z";
        for (const auto &t : version.source.input.tasks)
            version.links.push_back({t.id, "", t.id == oldTask});
        setup.recordXerSchedule(version);
        const auto target = setup.project()->tasks.back().id;
        Resource r = original.resources[0];
        r.id.clear();
        r.name = "새 XER 설계자";
        const auto targetR = setup.putResource(r);
        const auto before = *setup.project();
        const auto input = submission(oldTask, target, oldR, targetR);
        auto prepared = prepare(setup, input);
        if (prepared->failure())
            std::cerr << diagnosticMessage(*prepared->failure()) << '\n';
        check(prepared->state() == EditState::Ready && *setup.project() == before,
              "identity preparation changed source or failed");
        const auto result = *prepared->previewProject();
        check(result.tasks.size() == before.tasks.size() - 1 && result.resources.size() == before.resources.size() - 1,
              "merge did not remove duplicate definitions");
        records(before, result, oldTask, target, oldR, targetR);
        check(result.xerScheduleAdoptions == before.xerScheduleAdoptions, "original schedule adoption rewritten");
        const auto priorCosts =
            analyzeCosts(before, baseline, "budget-remaining-v1", std::vector<std::string>{oldTask});
        const auto newCosts = analyzeCosts(result, baseline, "budget-remaining-v1", std::vector<std::string>{target});
        check(priorCosts.evm.bac == newCosts.evm.bac && priorCosts.evm.pv == newCosts.evm.pv &&
                  priorCosts.evm.ev == newCosts.evm.ev && priorCosts.evm.ac == newCosts.evm.ac,
              "native/external EVM changed under identity mapping");
        check(setup.applyPreparedEdit(*prepared) == EditState::Applied && nativeSchemaFor(*setup.project()) == 33 &&
                  !setup.canUndo(),
              "identity commit/schema33/history");
        const auto after = *setup.project();
        check(
            *after.ledgerIdMigrations.back().before ==
                [&] {
                    auto p = before;
                    p.ledgerIdMigrations.clear();
                    return p;
                }(),
            "original snapshot lost");
        setup.save();
        check(store.load(setup.path()).project == *setup.project(), "schema33 roundtrip/deep snapshot equality");
        rejects([&] { decodeProjectFile(envelope(after, 32)); });
        check(decodeProjectFile(envelope(after, 52)) == after, "supported schema52");
        rejects([&] { decodeProjectFile(envelope(after, maximumNativeSchema + 1)); });
        for (int i = 0; i < 12; ++i) {
            auto bad = input;
            if (i == 0)
                bad.confirmed = false;
            if (i == 1)
                bad.links.clear();
            if (i == 2)
                bad.links[0].targetId = oldTask;
            if (i == 3)
                bad.links[0].sourceId = "missing";
            if (i == 4)
                bad.links[0].targetId = before.tasks[1].id;
            if (i == 5)
                bad.links.push_back(bad.links[0]);
            if (i == 6)
                bad.links[1].targetId = before.resources[1].id;
            if (i == 7)
                bad.reason = " ";
            if (i == 8)
                bad.recordedAt = "yesterday";
            if (i == 9)
                bad.links[0].targetId = oldR;
            if (i == 10)
                bad.links[0].targetId = " ";
            if (i == 11) {
                bad.links.push_back({"task", target, oldTask});
            }
            auto p = (temp.path() + QString("/reject-%1.ppm").arg(i)).toStdString();
            store.save(p, before, {});
            Document d(store);
            d.open(p);
            const auto failed = prepare(d, bad);
            check(failed->state() == EditState::Failed && *d.project() == before, "invalid migration applied");
        }
        auto collision = before;
        collision.baselines[0].input.tasks.push_back(collision.tasks.back());
        rejects([&] { migrateLedgerIdentities(collision, input.links); });
        auto targetOnly = before;
        targetOnly.baselines[0].input.tasks[0].id = target;
        rejects([&] { migrateLedgerIdentities(targetOnly, input.links); });
        auto budget = before;
        budget.taskCosts.push_back({target, 0, "uniform", "physical"});
        check(std::none_of(before.taskCosts.begin(), before.taskCosts.end(),
                           [&](const auto &v) { return v.taskId == target; }),
              "budget fixture target already used");
        rejects([&] { migrateLedgerIdentities(budget, input.links); });
        for (int i = 0; i < 13; ++i) {
            auto body = json::encodeProject(after);
            auto events = body["ledgerIdMigrations"].toArray();
            auto e = events[0].toObject();
            if (i == 0)
                e["policy"] = "unknown";
            if (i == 1)
                e["confirmed"] = false;
            if (i == 2)
                e["recordedRevision"] = 1.5;
            if (i == 3)
                e["unexpected"] = true;
            if (i == 4)
                e.remove("before");
            if (i == 5) {
                auto a = e["after"].toObject();
                auto facts = a["costEntries"].toArray();
                auto f = facts[0].toObject();
                f["actualMinor"] = 1;
                facts[0] = f;
                a["costEntries"] = facts;
                e["after"] = a;
            }
            if (i == 6) {
                auto a = e["before"].toObject();
                a["ledgerIdMigrations"] = QJsonArray{};
                e["before"] = a;
            }
            if (i == 7)
                e["links"] = QJsonArray{};
            if (i == 8)
                e["reporter"] = " ";
            if (i == 9)
                e["recordedAt"] = "today";
            if (i == 10)
                e["sourceDocumentId"] = "other";
            if (i == 11) {
                auto a = e["after"].toObject();
                a["revision"] = 1;
                e["after"] = a;
            }
            events[0] = e;
            body["ledgerIdMigrations"] = events;
            if (i == 12) {
                auto tasks = body["tasks"].toArray();
                auto t = tasks[0].toObject();
                t["name"] = "tampered";
                tasks[0] = t;
                body["tasks"] = tasks;
            }
            rejects([&] { json::decodeProject(body, 33); });
            write(QString("ledger-id-qa/malformed-%1.json").arg(i), QJsonDocument(body).toJson());
        }
        auto cancel = std::make_shared<CalculationControl>();
        cancel->cancel();
        check(prepare(setup, submission(target, "cancelled-task", targetR, "cancelled-resource"), cancel)->state() ==
                  EditState::Cancelled,
              "cancel ignored");
        auto pending = prepare(setup, submission(target, "later-task", targetR, "later-resource"));
        check(pending->state() == EditState::Ready, "repeat preparation");
        setup.renameTask(0, "ordinary");
        check(setup.applyPreparedEdit(*pending) == EditState::Stale, "stale migration applied");
        setup.undo();
        JsonRecoveryStore recovery(temp.path() + "/recovery");
        setup.renameTask(0, "복구 검증");
        auto session = recovery.newSession();
        session->checkpoint(setup.recoverySnapshot("2026-11-08T12:00:00Z"));
        session.reset();
        auto scan = recovery.scan();
        check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema33 recovery");
        auto claim = recovery.claim(scan.candidates[0]);
        Document restored(store);
        restored.recover(claim->snapshot);
        check(restored.project()->ledgerIdMigrations == setup.project()->ledgerIdMigrations,
              "recovery lost original identity snapshots");
        setup.undo();
        setup.saveAs((temp.path() + "/copy.ppm").toStdString());
        check(store.load(setup.path()).project == *setup.project(), "copy identity history");
        Document ro(store);
        ro.open(setup.path(), true);
        rejects([&] { ro.migrateLedgerIds(input); });
        setup.recordXerFinancialMerge({{}, "검토자", "ID 이관 후 병합", "2026-11-08T12:10:00Z"},
                                      xerFinancialProjectionHash);
        check(xerFinancialsCurrent(*setup.project()), "post migration source rebind");
        auto future = submission(target, "final-task", targetR, "final-resource");
        setup.migrateLedgerIds(future);
        check(setup.project()->ledgerIdMigrations.size() == 2, "sequential identity migrations");
        setup.save();
        check(store.load(setup.path()).project == *setup.project(), "sequential replay");
        records(after, *setup.project(), target, "final-task", targetR, "final-resource");
        const auto exported = exportPrimaveraXer(*setup.project());
        const auto readback = importPrimaveraXer(exported.xer, "migrated-export.xer").project;
        check(readback.tasks.size() == setup.project()->tasks.size(), "identity export task count");
        check(exported.report["actualCosts"].toObject()["ledgerIdMigrationCount"].toInt() == 2,
              "exchange migration receipt");
        write("ledger-id-qa/before.json", QJsonDocument(json::encodeProject(before)).toJson());
        write("ledger-id-qa/migrated.ppm", envelope(after, 33));
        write("ledger-id-qa/sequential.ppm", envelope(*setup.project(), 33));
        write("ledger-id-qa/export.xer", exported.xer);
        write("ledger-id-qa/ids.json", QJsonDocument(QJsonObject{{"sourceTask", QString::fromStdString(oldTask)},
                                                                 {"targetTask", QString::fromStdString(target)},
                                                                 {"sourceResource", QString::fromStdString(oldR)},
                                                                 {"targetResource", QString::fromStdString(targetR)}})
                                           .toJson());
        auto fresh = migrateLedgerIdentities(before, submission(oldTask, "fresh-task", oldR, "fresh-resource").links);
        records(before, fresh, oldTask, "fresh-task", oldR, "fresh-resource");
        check(fresh.tasks.size() == before.tasks.size() && fresh.resources.size() == before.resources.size(),
              "fresh identity removed objects");
        // A separate forecast covers dated goals, material/role references and automatic leveling invalidation.
        Project detailed;
        detailed.documentId = "detailed";
        detailed.calculationProfile = effortProfile;
        detailed.name = "참조 검증";
        detailed.startDate = "2026-11-02";
        detailed.statusAt = "2026-11-02 09:00";
        detailed.revision = 2;
        detailed.tasks = {{"T", "관측 작업", 480, {}}};
        Resource worker;
        worker.id = "R";
        worker.name = "작업자";
        detailed.resources = {worker};
        detailed.assignments = {{"T", "R", 100}};
        ProgressEntry progress;
        progress.id = "P";
        progress.taskId = "T";
        progress.statusAt = *detailed.statusAt;
        progress.recordedAt = "2026-11-08T08:00:00Z";
        progress.reporter = "PM";
        progress.reason = "예측";
        progress.sourceDocumentId = detailed.documentId;
        progress.recordedRevision = 2;
        progress.remainingMinutes = 480;
        detailed.progress = {progress};
        PeriodActualEntry actual;
        actual.id = "D";
        actual.taskId = "T";
        actual.resourceId = "R";
        actual.resourceType = "labor";
        actual.currency = "KRW";
        actual.date = "2026-11-02";
        actual.statusAt = *detailed.statusAt;
        actual.recordedAt = progress.recordedAt;
        actual.reporter = "PM";
        actual.reason = "명시 0 공수";
        actual.sourceDocumentId = detailed.documentId;
        actual.recordedRevision = 2;
        actual.workUnitMinutes = 0;
        detailed.periodActuals = {actual};
        detailed.remainingWork = {{"T", "R", *detailed.statusAt, {{"2026-11-02", 48000}}}};
        detailed.resourcePlan.materials = {{"M", "자재", "개", 100, 1000}};
        detailed.resourcePlan.uses = {{"U", "T", "M", 1000, "start"}};
        detailed.resourcePlan.expenses = {{"E", "T", "비용", "운반", 100, "uniform"}};
        detailed.resourcePlan.roles = {{"role", "작업 역할", "labor"}};
        detailed.resourcePlan.demands = {{"demand", "T", "role", "R", 100}};
        detailed.networkPositions = {{"T", 100, 200}};
        const auto detailPath = (temp.path() + "/detailed.ppm").toStdString();
        store.save(detailPath, detailed, {});
        Document detail(store);
        detail.open(detailPath);
        detail.saveScenario("날짜 목표 시나리오", "task-order", 30, "2026-11-08T09:00:00Z",
                            detail.project()->documentId, detail.project()->revision);
        LevelingSelection selected;
        selected.policy = datedRemainingPolicy;
        selected.reason = "검토";
        selected.adoptedAt = "2026-11-08T10:00:00Z";
        selected.sourceDocumentId = detail.project()->documentId;
        selected.sourceRevision = detail.project()->revision;
        selected.horizonWorkDays = 30;
        const auto detailLeveling = levelResources(*detail.project(), "task-order", 30);
        if (detailLeveling.leveled.status != CalculationStatus::Valid)
            for (const auto &diagnostic : detailLeveling.leveled.diagnostics)
                std::cerr << diagnosticMessage(diagnostic) << '\n';
        check(detailLeveling.leveled.status == CalculationStatus::Valid, "dated reference fixture cannot level");
        detail.adoptLeveling(selected);
        const auto selectedBefore = *detail.project();
        detail.migrateLedgerIds(submission("T", "T-new", "R", "R-new"));
        check(!detail.project()->remainingLeveling &&
                  detail.project()->levelingDecisions.size() == selectedBefore.levelingDecisions.size() + 1,
              "migration retained stale leveling");
        const auto &dp = *detail.project();
        check(dp.remainingWork[0].taskId == "T-new" && dp.remainingWork[0].resourceId == "R-new" &&
                  dp.scenarios[0].remainingWork[0].taskId == "T-new" && dp.scenarios[0].progress[0].taskId == "T-new" &&
                  dp.resourcePlan.demands[0].resourceId == "R-new" && dp.resourcePlan.uses[0].taskId == "T-new" &&
                  dp.resourcePlan.expenses[0].taskId == "T-new" && dp.networkPositions[0].taskId == "T-new",
              "dated/scenario/resource-plan/network refs stale");
        detail.save();
        check(store.load(detail.path()).project == dp, "leveling invalidation persistence");
        write("ledger-id-qa/detailed.ppm", envelope(dp, 33));
        std::string cappedTask = "T-new", cappedResource = "R-new";
        for (int i = 2; i <= 16; ++i) {
            auto nextTask = "limit-task-" + std::to_string(i), nextResource = "limit-resource-" + std::to_string(i);
            detail.migrateLedgerIds(submission(cappedTask, nextTask, cappedResource, nextResource));
            cappedTask = nextTask;
            cappedResource = nextResource;
        }
        const auto capped = *detail.project();
        check(capped.ledgerIdMigrations.size() == 16, "supported migration history boundary");
        rejects([&] {
            detail.migrateLedgerIds(submission(cappedTask, "seventeenth-task", cappedResource, "seventeenth-resource"));
        });
        check(*detail.project() == capped, "history limit refusal mutated document");
        auto uiRun = [&](bool small, bool publish) {
            const auto p = (temp.path() + (publish ? "/ui-submit.ppm" : "/ui-cancel.ppm")).toStdString();
            store.save(p, before, {});
            Document d(store);
            d.open(p);
            LedgerIdentityDialog dialog(d);
            dialog.show();
            QApplication::processEvents();
            if (small)
                dialog.resize(800, 600);
            auto *table = dialog.findChild<QTableView *>("ledgerIdLinks");
            int found = 0;
            for (int row = 0; row < table->model()->rowCount(); ++row) {
                const auto id = table->model()->index(row, 2).data().toString().toStdString();
                if (id == oldTask || id == oldR) {
                    check(table->model()->setData(table->model()->index(row, 4),
                                                  "id:" + QString::fromStdString(id == oldTask ? target : targetR),
                                                  Qt::EditRole),
                          "UI identity mapping");
                    ++found;
                }
            }
            check(found == 2, "observed identity inventory");
            dialog.findChild<QLineEdit *>("ledgerIdReporter")->setText("UI 검토자");
            auto *reason = dialog.findChild<QLineEdit *>("ledgerIdReason");
            reason->setText("같은 작업/자원 ID 확인");
            auto *analyze = dialog.findChild<QPushButton *>("ledgerIdAnalyze");
            analyze->click();
            wait([&] { return analyze->isEnabled(); });
            auto *facts = dialog.findChild<QTableView *>("ledgerIdFacts");
            auto *confirm = dialog.findChild<QCheckBox *>("ledgerIdConfirm");
            auto *put = dialog.findChild<QPushButton *>("ledgerIdSubmit");
            check(facts->model() && facts->model()->rowCount() > 0 && confirm->isEnabled() && *d.project() == before,
                  "identity preview failure/mutation");
            confirm->setChecked(true);
            reason->setText("변경 근거 재확인");
            check(!confirm->isChecked() && !put->isEnabled(), "draft mutation retained approval");
            analyze->click();
            wait([&] { return analyze->isEnabled(); });
            confirm->setChecked(true);
            dialog.grab().save(small ? "ledger-id-qa/preview-small.png" : "ledger-id-qa/preview.png");
            dialog.findChild<QTabWidget *>("ledgerIdTabs")->setCurrentWidget(table);
            dialog.grab().save(small ? "ledger-id-qa/editor-small.png" : "ledger-id-qa/editor.png");
            bool raw = false;
            QTimer::singleShot(20, [&] {
                auto *v = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                check(v && v->objectName() == "ledgerIdOriginals", "original inspector absent");
                raw = true;
                v->grab().save("ledger-id-qa/originals.png");
                v->reject();
            });
            dialog.findChild<QPushButton *>("ledgerIdInspect")->click();
            check(raw, "original inspector");
            if (publish) {
                put->click();
                records(before, *d.project(), oldTask, target, oldR, targetR);
                LedgerIdentityDialog history(d);
                history.show();
                auto *h = history.findChild<QTableWidget *>("ledgerIdHistory");
                check(h->rowCount() == 1, "identity history absent");
                QMetaObject::invokeMethod(h, "cellDoubleClicked", Qt::DirectConnection, Q_ARG(int, 0), Q_ARG(int, 0));
                check(history.findChild<QTableView *>("ledgerIdFacts")->model()->rowCount() ==
                          facts->model()->rowCount(),
                      "history facts missing");
                history.grab().save("ledger-id-qa/history.png");
                history.reject();
            } else {
                dialog.reject();
                check(*d.project() == before, "cancel submitted identity change");
            }
        };
        uiRun(true, false);
        uiRun(false, true);
        LedgerIdentityDialog readonly(ro);
        check(!readonly.findChild<QPushButton *>("ledgerIdAnalyze")->isEnabled(), "read-only identity UI active");
        MainWindow window(ro);
        auto *action = window.findChild<QAction *>("migrateLedgerIds");
        check(action && action->isEnabled(), "identity menu absent");
        bool shown = false;
        QTimer timer;
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, [&] {
            auto *v = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (v && v->objectName() == "ledgerIdentityDialog") {
                shown = true;
                v->reject();
            }
        });
        timer.start();
        action->trigger();
        timer.stop();
        check(shown, "identity menu not connected");
        std::cout << "PASS: explicit task/resource identity migration, original/provenance/amount/date preservation, "
                     "baseline/closed periods/Steps/external EVM/XER relink and schedule archives, fresh/merge IDs, "
                     "schema33/replay/recovery/copy, sequential/cancel/stale/readonly, export and native UI\n";
    } catch (const std::exception &e) {
        std::cerr << errorText(e) << '\n';
        return 1;
    }
    return 0;
}
