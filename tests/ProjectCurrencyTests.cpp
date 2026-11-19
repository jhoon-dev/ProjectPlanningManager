#include "domain/NativeSchema.h"
#include "application/Document.h"
#include "domain/ProjectCurrency.h"
#include "domain/Classification.h"
#include "domain/LedgerIdentity.h"
#include "domain/RemainingWork.h"
#include "calculation/Costs.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/XerFinancialJson.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/ProjectXml.h"
#include "reporting/ReportExport.h"
#include "ui/ProjectCurrencyDialog.h"
#include "ui/MainWindow.h"
#include "formatting/DiagnosticText.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
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
    check(bad, "expected currency conversion rejection");
}
void write(const QString &name, const QByteArray &bytes) {
    QFile f(name);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write currency evidence");
}
QByteArray envelope(const Project &p, int version = 34) {
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
CurrencyConversionSubmission submission(std::string from = "USD", std::string to = "KRW", std::int64_t n = 200,
                                        std::int64_t d = 1) {
    CurrencyConversionSubmission s;
    s.rate = {from, to, "2026-11-08", "명시 환율 검토 근거", "half-away-from-zero-v1", n, d};
    s.reporter = "환산 검토자";
    s.reason = "프로젝트 전체 통화 변경";
    s.recordedAt = "2026-11-08T13:00:00Z";
    s.confirmed = true;
    return s;
}
auto prepare(Document &d, const CurrencyConversionSubmission &s,
             std::shared_ptr<CalculationControl> c = std::make_shared<CalculationControl>()) {
    return Document::prepareEdit(d.makeEditRequest(CurrencyConversionEdit{s}), c);
}
void wait(const std::function<bool()> &done) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < 30000) {
        QApplication::processEvents();
        QTest::qWait(10);
    }
    check(done(), "currency UI timeout");
}
void preserved(const Project &a, const Project &b) {
    check(a.tasks == b.tasks && a.assignments == b.assignments && a.progress == b.progress &&
              a.stepReports == b.stepReports && a.remainingWork == b.remainingWork &&
              a.xerScheduleAdoptions == b.xerScheduleAdoptions && a.ledgerIdMigrations == b.ledgerIdMigrations,
          "nonmonetary/current/archive values changed");
    check(a.projectAttributes == b.projectAttributes && a.projectFields == b.projectFields,
          "arbitrary numeric UDF reinterpreted");
    for (std::size_t i = 0; i < a.costEntries.size(); ++i) {
        auto value = a.costEntries[i];
        value.actualMinor = b.costEntries[i].actualMinor;
        value.currency = b.costEntries[i].currency;
        check(value == b.costEntries[i], "cumulative cost metadata changed");
    }
    for (std::size_t i = 0; i < a.periodActuals.size(); ++i) {
        auto value = a.periodActuals[i];
        value.actualMinor = b.periodActuals[i].actualMinor;
        value.currency = b.periodActuals[i].currency;
        check(value == b.periodActuals[i], "dated metadata/work changed");
    }
    for (std::size_t i = 0; i < a.baselines.size(); ++i)
        check(a.baselines[i].schedule == b.baselines[i].schedule && a.baselines[i].id == b.baselines[i].id &&
                  a.baselines[i].inputRevision == b.baselines[i].inputRevision,
              "frozen baseline dates/provenance changed");
    for (std::size_t i = 0; i < a.xerFinancialRevisions.size(); ++i)
        if (a.xerFinancialRevisions[i].relink)
            check(a.xerFinancialRevisions[i].relink->incoming == b.xerFinancialRevisions[i].relink->incoming,
                  "raw incoming XER changed");
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setFont(QFont("Malgun Gothic", 9));
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, [] { std::_Exit(3); });
    guard.start(170000);
    try {
        QDir().mkpath("project-currency-qa");
        QTemporaryDir temp;
        JsonProjectStore store;
        auto original =
            importPrimaveraXer(readPrimaveraXer(QString::fromUtf8(PPM_CURRENCY_FIXTURE)), "original.xer").project;
        original.resourcePlan.materials = {{"M", "보존 자재", "개", 200, 2000}};
        original.resourcePlan.uses = {{"U", original.tasks[0].id, "M", 1000, "start"}};
        original.resourcePlan.expenses = {{"E", original.tasks[0].id, "운반", "비용", 100, "uniform"}};
        original.projectFields = {{"nominal", "수동 금액 메모", CustomFieldType::Number, "USD", {}}};
        original.projectAttributes = {{"field:nominal", "123.456"}};
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
        relink.incoming.currency = "EUR";
        XerSourceConversion originalFx;
        originalFx.targetCurrency = "USD";
        originalFx.effectiveDate = "2026-11-08";
        originalFx.reference = "기존 원본의 명시 1:1 환율";
        originalFx.resources = {{"labor", "labor", 1, 1}};
        relink.incoming.resources[0].type = "labor";
        relink.incomingConversion = originalFx;
        XerSourceConversion previousFx;
        previousFx.targetCurrency = "USD";
        previousFx.effectiveDate = "2026-11-08";
        previousFx.reference = "이전 원본 단위 유지";
        relink.previousConversion = previousFx;
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

        setup.migrateLedgerIds({{{"task", oldTask, "observed-task"}, {"resource", oldR, "observed-resource"}},
                                "PM",
                                "기존 ID 이력 보존",
                                "2026-11-08T09:00:00Z",
                                true});
        XerScheduleSubmission schedule;
        schedule.source.input = *setup.project();
        schedule.source.sourceName = "계획.xer";
        schedule.source.sourceSha256 = std::string(64, 'a');
        schedule.source.projectId = "plan";
        schedule.source.importedAt = "2026-11-08T09:00:00Z";
        schedule.source.reportJson = "{}";
        schedule.schedule = false;
        schedule.wbs = true;
        schedule.calendars = false;
        schedule.confirmed = true;
        schedule.reporter = "PM";
        schedule.reason = "과거 계획 보존";
        schedule.recordedAt = "2026-11-08T09:01:00Z";
        for (const auto &t : schedule.source.input.tasks)
            schedule.links.push_back({t.id, t.id, false});
        setup.recordXerSchedule(schedule);
        const auto before = *setup.project();
        const auto s = submission();
        auto ready = prepare(setup, s);
        if (ready->failure())
            std::cerr << diagnosticMessage(*ready->failure()) << '\n';
        check(ready->state() == EditState::Ready && *setup.project() == before,
              "currency preparation failed/changed source");
        const auto preview = *ready->previewProject();
        preserved(before, preview);
        const auto beforeRows = currencyMoneyRows(before), afterRows = currencyMoneyRows(preview);
        check(beforeRows.size() == afterRows.size() && !beforeRows.empty(), "currency preview coverage");
        for (std::size_t i = 0; i < beforeRows.size(); ++i) {
            check(beforeRows[i].path == afterRows[i].path && afterRows[i].currency == "KRW", "currency row mapping");
            check(beforeRows[i].amount ? afterRows[i].amount == *beforeRows[i].amount * 2 : !afterRows[i].amount,
                  "rich fixture currency scale");
        }
        const auto a = analyzeCosts(before, baseline), b = analyzeCosts(preview, baseline);
        check(a.evm.bac ? b.evm.bac == *a.evm.bac * 2 : !b.evm.bac, "unknown aggregate BAC changed");
        const auto selectedBefore =
            analyzeCosts(before, baseline, "budget-remaining-v1", std::vector<std::string>{"observed-task"});
        const auto selectedAfter =
            analyzeCosts(preview, baseline, "budget-remaining-v1", std::vector<std::string>{"observed-task"});
        check(selectedBefore.evm.bac && selectedAfter.evm.bac == *selectedBefore.evm.bac * 2,
              "selected EVM BAC conversion");
        check(setup.applyPreparedEdit(*ready) == EditState::Applied && !setup.canUndo() &&
                  nativeSchemaFor(*setup.project()) == 34,
              "currency commit/schema34/immutability");
        const auto after = *setup.project();
        const auto targetXer = exportPrimaveraXer(after);
        const auto targetReadback = importPrimaveraXer(targetXer.xer, "target-krw.xer").project;
        check(targetReadback.currency == "KRW" && targetReadback.costEntries.size() == after.costEntries.size(),
              "target currency XER precision/readback");
        for (std::size_t i = 0; i < after.costEntries.size(); ++i)
            check(targetReadback.costEntries[i].actualMinor == after.costEntries[i].actualMinor,
                  "target XER cumulative AC scale");
        const auto targetXml = exportProjectXml(after);
        check(importProjectXml(targetXml.xml, "target-krw.xml").project.currency == "KRW",
              "target currency XML precision/readback");
        write("project-currency-qa/target-krw.xer", targetXer.xer);
        write("project-currency-qa/target-krw.xml", targetXml.xml);
        ReportOptions reportOptions;
        reportOptions.baselineId = baseline;
        reportOptions.reportId = "currency-proof";
        reportOptions.generatedAt = "2026-11-08T13:30:00Z";
        auto convertedReport = buildReport(after, reportOptions);
        validateReportExport(convertedReport);
        check(convertedReport.currency == "KRW" &&
                  std::any_of(convertedReport.tables.begin(), convertedReport.tables.end(),
                              [](const auto &t) { return t.id == "projectCurrencyHistory"; }),
              "currency report provenance absent");
        exportXlsx(convertedReport, temp.path() + "/report.xlsx");
        PdfOptions pdf;
        pdf.font = QApplication::font();
        check(exportPdf(convertedReport, temp.path() + "/report.pdf", ReportFamily::All, pdf) > 0,
              "currency PDF export");
        for (const auto &name : {"report.xlsx", "report.pdf"}) {
            QFile artifact(temp.path() + "/" + name);
            check(artifact.open(QIODevice::ReadOnly), "read currency report evidence");
            write(QString("project-currency-qa/") + name, artifact.readAll());
        }

        setup.save();
        check(store.load(setup.path()).project == after, "currency save/replay equality");
        auto stripped = before;
        clearTransitionHistories(stripped);
        check(*after.currencyConversions[0].before == stripped, "original project snapshot lost");
        rejects([&] { decodeProjectFile(envelope(after, 33)); });
        check(decodeProjectFile(envelope(after, 52)) == after, "supported schema52");
        rejects([&] { decodeProjectFile(envelope(after, maximumNativeSchema + 1)); });
        auto cancelled = std::make_shared<CalculationControl>();
        cancelled->cancel();
        check(prepare(setup, submission("KRW", "USD", 1, 200), cancelled)->state() == EditState::Cancelled,
              "currency cancellation ignored");
        auto pending = prepare(setup, submission("KRW", "USD", 1, 200));
        check(pending->state() == EditState::Ready, "repeat currency preparation");
        setup.renameTask(0, "일반 편집");
        check(setup.applyPreparedEdit(*pending) == EditState::Stale, "stale currency applied");
        setup.undo();
        JsonRecoveryStore recovery(temp.path() + "/recovery");
        setup.renameTask(0, "복구 자료");
        auto session = recovery.newSession();
        session->checkpoint(setup.recoverySnapshot("2026-11-08T14:00:00Z"));
        session.reset();
        auto scan = recovery.scan();
        check(scan.candidates.size() == 1 && scan.candidates[0].valid, "currency recovery decode");
        auto claim = recovery.claim(scan.candidates[0]);
        Document restored(store);
        restored.recover(claim->snapshot);
        check(restored.project()->currencyConversions == setup.project()->currencyConversions,
              "recovery lost original currency receipt");
        setup.undo();
        setup.saveAs((temp.path() + "/copy.ppm").toStdString());
        check(store.load(setup.path()).project == *setup.project(), "currency independent copy");
        setup.recordXerFinancialMerge({{}, "검토자", "환산 후 재무 병합", "2026-11-08T14:10:00Z"},
                                      xerFinancialProjectionHash);
        check(xerFinancialsCurrent(*setup.project()), "post-currency financial merge");
        setup.migrateLedgerIds(
            {{{"task", "observed-task", "post-fx-task"}, {"resource", "observed-resource", "post-fx-resource"}},
             "PM",
             "환산 후 ID 이관",
             "2026-11-08T14:20:00Z",
             true});
        setup.save();
        check(store.load(setup.path()).project == *setup.project(), "currency then identity replay");
        const auto intermediate = *setup.project();
        setup.convertCurrency(submission("KRW", "USD", 1, 200));
        setup.save();
        check(setup.project()->currencyConversions.size() == 2 && store.load(setup.path()).project == *setup.project(),
              "sequential currency/identity replay");
        check(setup.project()->currencyConversions[0] == after.currencyConversions[0],
              "prior currency event rewritten");
        const auto exported = exportPrimaveraXer(*setup.project());
        check(exported.report["actualCosts"].toObject()["projectCurrencyConversionCount"].toInt() == 2,
              "XER currency receipt");
        check(importPrimaveraXer(exported.xer, "fx.xer").project.currency == "USD", "currency XER readback");
        const auto xml = exportProjectXml(*setup.project());
        check(importProjectXml(xml.xml, "fx.xml").project.currency == "USD", "currency XML readback");
        write("project-currency-qa/before.json", QJsonDocument(json::encodeProject(before)).toJson());
        write("project-currency-qa/converted.ppm", envelope(after));
        write("project-currency-qa/intermediate.ppm", envelope(intermediate));
        write("project-currency-qa/sequential.ppm", envelope(*setup.project()));
        write("project-currency-qa/export.xer", exported.xer);
        write("project-currency-qa/export.xml", xml.xml);
        for (int i = 0; i < 13; ++i) {
            auto bad = s;
            if (i == 0)
                bad.confirmed = false;
            if (i == 1)
                bad.rate.numerator = 0;
            if (i == 2)
                bad.rate.denominator = 0;
            if (i == 3)
                bad.rate.numerator = 1000000001;
            if (i == 4)
                bad.rate.fromCurrency = "EUR";
            if (i == 5)
                bad.rate.targetCurrency = "USD";
            if (i == 6)
                bad.rate.reference = " ";
            if (i == 7)
                bad.rate.effectiveDate = "2026-02-30";
            if (i == 8)
                bad.rate.rounding = "nearest";
            if (i == 9)
                bad.reporter = " ";
            if (i == 10)
                bad.reason = " ";
            if (i == 11)
                bad.recordedAt = "today";
            if (i == 12)
                bad.rate.targetCurrency = "GBP";
            const auto path = (temp.path() + QString("/reject-%1.ppm").arg(i)).toStdString();
            store.save(path, before, {});
            Document d(store);
            d.open(path);
            auto result = prepare(d, bad);
            check(result->state() == EditState::Failed && *d.project() == before,
                  "invalid currency request mutated source");
        }
        for (int i = 0; i < 11; ++i) {
            auto body = json::encodeProject(after);
            auto list = body["currencyConversions"].toArray();
            auto e = list[0].toObject();
            if (i == 0)
                e["confirmed"] = false;
            if (i == 1)
                e["recordedRevision"] = 1.5;
            if (i == 2)
                e["unknown"] = true;
            if (i == 3)
                e["policy"] = "future";
            if (i == 4) {
                auto v = e["before"].toObject();
                v["currencyConversions"] = QJsonArray{};
                e["before"] = v;
            }
            if (i == 5) {
                auto v = e["after"].toObject();
                v["ledgerIdMigrations"] = QJsonArray{};
                e["after"] = v;
            }
            if (i == 6) {
                auto v = e["rate"].toObject();
                v["numerator"] = 1.5;
                e["rate"] = v;
            }
            if (i == 7) {
                auto v = e["after"].toObject();
                v["currency"] = "EUR";
                e["after"] = v;
            }
            if (i == 8)
                e["sourceDocumentId"] = "wrong";
            if (i == 9)
                e["reporter"] = " ";
            list[0] = e;
            body["currencyConversions"] = list;
            if (i == 10)
                body["currency"] = "EUR";
            rejects([&] { json::decodeProject(body, 34); });
            write(QString("project-currency-qa/malformed-%1.json").arg(i), QJsonDocument(body).toJson());
        }
        QJsonArray arithmetic;
        for (const auto *from : {"KRW", "JPY", "USD", "EUR"})
            for (const auto *to : {"KRW", "JPY", "USD", "EUR"})
                for (auto [n, d] : {std::pair{1LL, 1LL}, std::pair{3LL, 2LL}, std::pair{1000000000LL, 999999937LL}})
                    for (auto amount : {-9000000000000LL, -101LL, -1LL, 0LL, 1LL, 2LL, 3LL, 99LL, 100LL, 101LL,
                                        900719925LL, 9000000000000LL}) {
                        auto rate = submission(from, to, n, d).rate;
                        QJsonObject row{{"from", from}, {"to", to}, {"n", n}, {"d", d}, {"amount", amount}};
                        try {
                            auto result = convertCurrencyMoney(amount, rate);
                            row["result"] = qint64(*result);
                        } catch (const AppError &) {
                            row["rejected"] = true;
                        }
                        arithmetic.append(row);
                    }
        write("project-currency-qa/arithmetic.json", QJsonDocument(arithmetic).toJson());
        auto exact = submission("KRW", "USD", 1, 200).rate;
        exact.rounding = "exact-minor-v1";
        rejects([&] { convertCurrencyMoney(1, exact); });
        check(convertCurrencyMoney(2, exact) == 1 && convertCurrencyMoney(std::nullopt, exact) == std::nullopt,
              "exact/unknown conversion");
        Document rounding(store);
        rounding.create("반올림 대사", "2026-11-02", {"관측 작업"});
        rounding.setDuration(0, 480);
        rounding.setStatusAt("2026-11-02 18:00");
        PeriodActualSubmission actual;
        actual.reporter = "PM";
        actual.reason = "완전 배분한 작은 금액";
        actual.recordedAt = "2026-11-08T12:00:00Z";
        PeriodActualEntry first;
        first.taskId = rounding.project()->tasks[0].id;
        first.date = "2026-11-01";
        first.actualMinor = 1;
        auto second = first;
        second.date = "2026-11-02";
        actual.entries = {first, second};
        actual.totals = {{first.taskId, 2}};
        rounding.recordPeriodActuals(actual);
        const auto balanced = *rounding.project();
        auto refused = prepare(rounding, submission("KRW", "JPY", 3, 5));
        check(refused->state() == EditState::Failed && *rounding.project() == balanced,
              "rounding manufactured ledger reconciliation");
        auto exactRequest = submission("KRW", "USD", 1, 100);
        exactRequest.rate.rounding = "exact-minor-v1";
        auto accepted = prepare(rounding, exactRequest);
        check(accepted->state() == EditState::Ready, "exact minor-unit restatement refused");
        Document capped(store);
        capped.create("통화 이력 한도", "2026-11-02", {"작업"});
        capped.setDuration(0, 60);
        capped.setTaskCost({capped.project()->tasks[0].id, 0, "uniform", "physical"});
        Resource worker;
        worker.name = "검토자";
        const auto workerId = capped.putResource(worker);
        capped.setAssignments(capped.project()->tasks[0].id, {{capped.project()->tasks[0].id, workerId, 100}});
        LevelingSelection level;
        level.policy = levelingPolicyFor(*capped.project());
        level.reason = "선택 이력 보존";
        level.adoptedAt = "2026-11-08T12:00:00Z";
        level.sourceDocumentId = capped.project()->documentId;
        level.sourceRevision = capped.project()->revision;
        level.horizonWorkDays = 30;
        capped.adoptLeveling(level);
        for (int i = 0; i < 16; ++i)
            capped.convertCurrency(
                submission(capped.project()->currency, capped.project()->currency == "KRW" ? "JPY" : "KRW", 1, 1));
        check(capped.project()->currencyConversions.size() == 16 && !capped.project()->planLeveling &&
                  capped.project()->levelingDecisions.back().action == "invalidate",
              "currency limit/leveling invalidation");
        const auto full = *capped.project();
        rejects([&] { capped.convertCurrency(submission(full.currency, "JPY", 1, 1)); });
        check(*capped.project() == full, "currency history limit mutated document");
        store.save((temp.path() + "/limit.ppm").toStdString(), full, {});
        check(store.load((temp.path() + "/limit.ppm").toStdString()).project == full, "currency limit/leveling replay");
        write("project-currency-qa/leveling.ppm", envelope(full));
        JsonProjectStore uiStore;
        auto uiRun = [&](bool small, bool submit) {
            const auto path = (temp.path() + (submit ? "/ui-submit.ppm" : "/ui-cancel.ppm")).toStdString();
            uiStore.save(path, before, {});
            Document d(uiStore);
            d.open(path);
            ProjectCurrencyDialog dialog(d);
            dialog.show();
            QApplication::processEvents();
            if (small)
                dialog.resize(800, 600);
            dialog.findChild<QComboBox *>("projectCurrencyTarget")->setCurrentText("KRW");
            dialog.findChild<QLineEdit *>("projectCurrencyNumerator")->setText("200");
            dialog.findChild<QLineEdit *>("projectCurrencyReference")->setText("명시 환율 근거");
            dialog.findChild<QLineEdit *>("projectCurrencyReporter")->setText("UI 검토자");
            auto *reason = dialog.findChild<QLineEdit *>("projectCurrencyReason");
            reason->setText("전체 통화 환산 검토");
            auto *analyze = dialog.findChild<QPushButton *>("projectCurrencyAnalyze");
            analyze->click();
            wait([&] { return analyze->isEnabled(); });
            auto *confirm = dialog.findChild<QCheckBox *>("projectCurrencyConfirm");
            auto *put = dialog.findChild<QPushButton *>("projectCurrencySubmit");
            auto *table = dialog.findChild<QTableView *>("projectCurrencyMoney");
            if (!confirm->isEnabled())
                std::cerr << dialog.findChild<QLabel *>("projectCurrencyMessage")->text().toStdString() << '\n';
            check(confirm->isEnabled() && table->model() && table->model()->rowCount() == int(beforeRows.size()) &&
                      *d.project() == before,
                  "currency UI preview failure");
            confirm->setChecked(true);
            reason->setText("변경 후 다시 검토");
            check(!confirm->isChecked() && !put->isEnabled(), "currency input retained approval");
            analyze->click();
            wait([&] { return analyze->isEnabled(); });
            confirm->setChecked(true);
            dialog.grab().save(small ? "project-currency-qa/preview-small.png" : "project-currency-qa/preview.png");
            bool inspected = false;
            QTimer::singleShot(20, [&] {
                auto *child = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                check(child && child->objectName() == "projectCurrencyOriginals", "currency originals absent");
                inspected = true;
                child->grab().save("project-currency-qa/originals.png");
                child->reject();
            });
            dialog.findChild<QPushButton *>("projectCurrencyInspect")->click();
            check(inspected, "currency original inspector");
            if (submit) {
                put->click();
                check(d.project()->currency == "KRW" && d.project()->currencyConversions.size() == 1,
                      "UI currency submit");
                ProjectCurrencyDialog history(d);
                history.show();
                auto *h = history.findChild<QTableWidget *>("projectCurrencyHistory");
                check(h->rowCount() == 1, "currency history absent");
                QMetaObject::invokeMethod(h, "cellDoubleClicked", Qt::DirectConnection, Q_ARG(int, 0), Q_ARG(int, 0));
                wait([&] { return !history.findChild<QPushButton *>("projectCurrencyCancel")->isEnabled(); });
                check(history.findChild<QTableView *>("projectCurrencyMoney")->model(),
                      "currency historical rows absent");
                history.grab().save("project-currency-qa/history.png");
                history.reject();
                d.save();
                Document ro(uiStore);
                ro.open(d.path(), true);
                ProjectCurrencyDialog readonly(ro);
                check(!readonly.findChild<QPushButton *>("projectCurrencyAnalyze")->isEnabled(),
                      "readonly currency UI active");
                rejects([&] { ro.convertCurrency(submission("KRW", "USD", 1, 200)); });
                MainWindow window(ro);
                auto *action = window.findChild<QAction *>("convertProjectCurrency");
                check(action && action->isEnabled(), "currency menu absent");
                bool seen = false;
                QTimer timer;
                timer.setInterval(20);
                QObject::connect(&timer, &QTimer::timeout, [&] {
                    auto *v = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    if (v && v->objectName() == "projectCurrencyDialog") {
                        seen = true;
                        v->reject();
                    }
                });
                timer.start();
                action->trigger();
                timer.stop();
                check(seen, "currency menu not connected");
            } else {
                dialog.reject();
                check(*d.project() == before, "UI cancel converted currency");
            }
        };
        uiRun(true, false);
        uiRun(false, true);
        std::cout << "PASS: project-wide currency rates/budgets/materials/expenses/native "
                     "observations/baselines/scenarios/closed periods/external EVM/XER projections, original archives "
                     "and FX chains, schema34/replay/recovery/copy, sequential identity/currency operations, numerical "
                     "cases, cancel/stale/readonly, export and native UI\n";
    } catch (const std::exception &e) {
        std::cerr << errorText(e) << '\n';
        return 1;
    }
    return 0;
}
