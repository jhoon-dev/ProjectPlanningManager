#include "PeriodEvmFixture.h"
#include "ExternalEvmFixture.h"
#include "domain/FinancialPeriods.h"
#include "domain/Classification.h"
#include "calculation/FinancialPeriodReports.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/XerFinancialJson.h"
#include "reporting/ReportExport.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <future>
#include <iostream>
using namespace jobs;
namespace {
void check(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected financial-period rejection");
}
std::int64_t number(const ProjectReport &r, int c) {
    return std::get<std::int64_t>(r.tables[0].rows[0].at(std::size_t(c)).value);
}
void write(const QString &p, const QByteArray &b) {
    QFile f(p);
    check(f.open(QIODevice::WriteOnly) && f.write(b) == b.size(), "write QA");
}
QJsonObject reportJson(const ProjectReport &r) {
    QJsonArray tables;
    for (const auto &t : r.tables) {
        QJsonArray columns, rows;
        for (const auto &c : t.columns)
            columns.append(QString::fromStdString(c.title));
        for (const auto &row : t.rows) {
            QJsonArray cells;
            for (const auto &c : row) {
                if (const auto *n = std::get_if<std::int64_t>(&c.value))
                    cells.append(qint64(*n));
                else if (const auto *n = std::get_if<double>(&c.value))
                    cells.append(*n);
                else if (const auto *s = std::get_if<std::string>(&c.value))
                    cells.append(QString::fromStdString(*s));
                else
                    cells.append(QJsonValue::Null);
            }
            rows.append(cells);
        }
        tables.append(QJsonObject{{"id", QString::fromStdString(t.id)}, {"columns", columns}, {"rows", rows}});
    }
    return {{"policy", "custom-financial-period-v1"}, {"tables", tables}};
}
FinancialPeriodSubmission submission(const Project &p) {
    FinancialPeriodSubmission s;
    s.period.name = "제1 관리기간";
    s.period.baselineId = p.baselines[0].id;
    s.period.fromDate = "2026-10-26";
    s.period.toDate = "2026-10-27";
    s.period.values = {{p.tasks[0].id, 500, 450}};
    s.reporter = "기간 관리자";
    s.reason = "승인된 기간 금액 및 마감";
    s.recordedAt = "2026-11-08T04:00:00Z";
    s.confirmed = true;
    return s;
}
FinancialPeriodSubmission transition(const Project &p, const std::string &id, const std::string &action) {
    auto s = submission(p);
    s.period = financialPeriod(p, id).period;
    s.action = action;
    return s;
}
void contracts() {
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = (tmp.path() + "/source.ppm").toStdString();
    const auto before = period_evm_fixture::make(store, path);
    Document d(store);
    d.open(path);
    auto s = submission(before);
    const auto id = d.recordFinancialPeriod(s);
    check(d.dirty() && !d.canUndo() && nativeSchemaFor(*d.project()) == 30, "definition immutable/schema30/dirty");
    auto same = *d.project();
    same.financialPeriodEvents.clear();
    same.revision = before.revision;
    check(same == before, "definition changed native facts");
    auto o = period_evm_fixture::options(before);
    auto report = buildFinancialPeriodReport(*d.project(), o, id);
    check(number(report, 2) == 3500 && number(report, 3) == 2550 && number(report, 4) == 2500 &&
              number(report, 5) == 50 && number(report, 6) == -950,
          "manual/native interval oracle");
    check(number(report, 9) == 4000 && number(report, 10) == 2700 && number(report, 11) == 2000 &&
              number(report, 12) == 500,
          "native deltas/dated allocation");
    check(number(report, 13) == 4000 && number(report, 14) == 4000 && number(report, 15) == 2700 &&
              number(report, 16) == 2500 && number(report, 17) == 3800,
          "native cumulative estimate retained");
    check(std::abs(std::get<double>(report.tables[0].rows[0][7].value) - 1.02) < 1e-12, "interval ratio sum");
    auto scoped = o;
    scoped.wbsId = before.wbsNodes[1].id;
    auto sr = buildFinancialPeriodReport(*d.project(), scoped, id);
    check(number(sr, 2) == 500 && number(sr, 3) == 450 && number(sr, 4) == 800, "baseline WBS manual values");
    auto bad = s;
    bad.period.id = "overlap";
    rejects([&] { d.recordFinancialPeriod(bad); });
    bad = s;
    bad.period.id = id;
    bad.period.toDate = "2026-10-25";
    rejects([&] { d.recordFinancialPeriod(bad); });
    bad = s;
    bad.period.id = id;
    bad.period.values.push_back(bad.period.values[0]);
    rejects([&] { d.recordFinancialPeriod(bad); });
    bad = s;
    bad.period.id = id;
    bad.period.values[0].budget = -1;
    rejects([&] { d.recordFinancialPeriod(bad); });
    bad = s;
    bad.confirmed = false;
    rejects([&] { d.recordFinancialPeriod(bad); });
    auto close = transition(*d.project(), id, "close");
    d.recordFinancialPeriod(close);
    const auto firstClose = d.project()->financialPeriodEvents.back();
    check(firstClose.tasks.size() == 2 && firstClose.knowledgeRevision + 1 == firstClose.recordedRevision,
          "frozen close metadata");
    const auto frozen = buildFinancialPeriodReport(*d.project(), o, id);
    check(reportJson(frozen)["tables"].toArray()[0] == reportJson(report)["tables"].toArray()[0],
          "close changed computed values");
    const auto a = before.tasks[0].id;
    auto alternate = o;
    alternate.formula = "cpi-v1";
    check(number(buildFinancialPeriodReport(*d.project(), alternate, id), 17) == 3800,
          "closed formula restated by report selection");

    d.setStatusAt("2026-10-27 18:00");
    auto unchanged = *d.project();
    rejects([&] { d.recordCost(period_evm_fixture::cost(a, 900, 6500)); });
    check(*d.project() == unchanged, "rejected closed cost mutated document");
    ProgressEntry progress;
    progress.taskId = a;
    progress.remainingMinutes = 960;
    progress.reporter = "정정";
    progress.reason = "마감 진척 정정";
    progress.recordedAt = "2026-11-08T04:10:00Z";
    rejects([&] { d.recordProgress(progress); });
    ActivityDefinition activity;
    activity.taskId = a;
    activity.name = "검사 Steps";
    activity.physicalFromSteps = false;
    activity.steps = {{"S", "검사", 1}};
    d.putActivity(activity);
    StepReport steps;
    steps.taskId = a;
    steps.reporter = "정정";
    steps.reason = "마감 Steps 정정";
    steps.recordedAt = "2026-11-08T04:10:00Z";
    steps.steps = {{{"S", "검사", 1}, 1000}};
    rejects([&] { d.recordSteps(steps); });
    d.setStatusAt("2026-10-25 23:59");
    rejects([&] { d.recordCost(period_evm_fixture::cost(a, 0, 0)); });
    d.setStatusAt("2026-10-29 12:00");
    PeriodActualSubmission allocation;
    PeriodActualEntry entry;
    entry.taskId = a;
    entry.date = "2026-10-26";
    entry.actualMinor = 401;
    allocation.entries = {entry};
    allocation.totals = {{a, 751}};
    allocation.reporter = "정정";
    allocation.reason = "마감 정정 차단";
    allocation.recordedAt = "2026-11-08T04:10:00Z";
    rejects([&] { d.recordPeriodActuals(allocation); });
    d.recordCost(
        period_evm_fixture::cost(a, 1200, 7000)); // Later observation is permitted, archived close stays fixed.
    d.setTaskCost({a, 2000, "uniform", "physical"});
    check(reportJson(buildFinancialPeriodReport(*d.project(), o, id))["tables"].toArray()[0] ==
              reportJson(frozen)["tables"].toArray()[0],
          "later observations/plan restated close");
    bad = transition(*d.project(), id, "define");
    bad.period.name = "마감 수정";
    rejects([&] { d.recordFinancialPeriod(bad); });
    rejects([&] { d.recordFinancialPeriod(transition(*d.project(), id, "close")); });
    // A second close must be reopened first, otherwise earlier journal corrections remain locked.
    auto later = submission(*d.project());
    later.period.id.clear();
    later.period.name = "제2 관리기간";
    later.period.fromDate = "2026-10-28";
    later.period.toDate = "2026-10-29";
    later.period.values.clear();
    const auto id2 = d.recordFinancialPeriod(later);
    rejects([&] {
        d.recordFinancialPeriod(transition(*d.project(), id2, "close"));
    }); // Status is still inside the final date.
    d.setStatusAt("2026-10-29 23:59");
    d.recordFinancialPeriod(transition(*d.project(), id2, "close"));
    rejects([&] { d.recordFinancialPeriod(transition(*d.project(), id, "reopen")); });
    d.recordFinancialPeriod(transition(*d.project(), id2, "reopen"));
    d.recordFinancialPeriod(transition(*d.project(), id, "reopen"));
    d.setStatusAt("2026-10-27 18:00");
    d.recordCost(period_evm_fixture::cost(a, 900, 6500));
    d.setStatusAt("2026-10-29 23:59");
    const auto reopened = buildFinancialPeriodReport(*d.project(), o, id);
    check(number(reopened, 4) == 2600 && number(reopened, 5) == -50 && number(reopened, 10) == 2750,
          "reopen accepted correction");
    d.recordFinancialPeriod(transition(*d.project(), id, "close"));
    const auto reclosed = buildFinancialPeriodReport(*d.project(), o, id);
    const auto historical = buildFinancialPeriodReport(*d.project(), o, id, firstClose.id);
    check(number(reclosed, 4) == 2600 && number(historical, 4) == 2500, "reclose overwrote earlier close");
    auto body = json::encodeProject(*d.project());
    check(json::decodeProject(body, 30) == *d.project(), "schema30 roundtrip");
    rejects([&] { json::decodeProject(body, 29); });
    for (int i = 0; i < 12; ++i) {
        auto v = body;
        auto events = v["financialPeriodEvents"].toArray();
        auto e = events[1].toObject();
        auto def = e["period"].toObject();
        if (i == 0)
            e["policy"] = "future";
        if (i == 1)
            e["confirmed"] = false;
        if (i == 2)
            e["knowledgeRevision"] = 0;
        if (i == 3)
            e["recordedRevision"] = 1;
        if (i == 4)
            e["currency"] = "KRW";
        if (i == 5)
            e["action"] = "reopen";
        if (i == 6)
            e["statusAt"] = "2026-10-27 12:00";
        if (i == 7)
            def["baselineId"] = "missing";
        if (i == 8)
            def["fromDate"] = "2026-02-30";
        if (i == 9)
            e["tasks"] = QJsonArray{};
        if (i == 10)
            e["unexpected"] = true;
        if (i == 11)
            e["tasks"] = "bad";
        e["period"] = def;
        events[1] = e;
        v["financialPeriodEvents"] = events;
        rejects([&] { json::decodeProject(v, 30); });
    }
    const auto saved = (tmp.path() + "/closed.ppm").toStdString();
    d.saveAs(saved);
    check(store.load(saved).project == *d.project(), "save reopen");
    Document copy(store);
    copy.open(saved, true);
    copy.saveAs((tmp.path() + "/copy.ppm").toStdString());
    check(copy.project()->financialPeriodEvents == d.project()->financialPeriodEvents,
          "copy changed source close history");
    d.setStatusAt("2026-10-30 00:00"); // Recovery snapshots intentionally require unsaved edits.
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-08T05:00:00Z"));
    session.reset();
    const auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "recovery schema30");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(store);
    restored.recover(claim->snapshot);
    check(restored.project()->financialPeriodEvents == d.project()->financialPeriodEvents, "recovery changed close");
    d.renameTask(0, "이름 변경");
    d.undo();
    check(d.project()->financialPeriodEvents.size() == std::size_t(body["financialPeriodEvents"].toArray().size()),
          "undo removed immutable periods");
    QDir().mkpath("custom-financial-periods-qa");
    auto saveJson = [&](const QString &file, const QJsonObject &v) {
        write("custom-financial-periods-qa/" + file, QJsonDocument(v).toJson());
    };
    saveJson("before.json", json::encodeProject(before));
    saveJson("final.json", json::encodeProject(*d.project()));
    saveJson("open-report.json", reportJson(report));
    saveJson("closed-report.json", reportJson(historical));
    saveJson("reopened-report.json", reportJson(reopened));
    saveJson("reclosed-report.json", reportJson(reclosed));
    QFile::remove("custom-financial-periods-qa/closed.ppm");
    store.save("custom-financial-periods-qa/closed.ppm", *d.project(), {});
    for (const auto &artifact : {"custom-financial-periods-qa/closed.xlsx", "custom-financial-periods-qa/reclosed.xlsx",
                                 "custom-financial-periods-qa/closed.pdf"})
        QFile::remove(artifact);
    exportXlsx(historical, "custom-financial-periods-qa/closed.xlsx");
    exportXlsx(reclosed, "custom-financial-periods-qa/reclosed.xlsx");
    PdfOptions pdf;
    pdf.a3 = true;
    pdf.font = QFont("Malgun Gothic", 9);
    check(exportPdf(historical, "custom-financial-periods-qa/closed.pdf", ReportFamily::Costs, pdf) > 0, "PDF export");
    rejects([&] { d.setCurrency("KRW"); });
}
void externalIntegration() {
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = (tmp.path() + "/source.ppm").toStdString();
    const auto p = external_evm_fixture::make(store, path);
    Document d(store);
    d.open(path);
    auto adoption = external_evm_fixture::submission(p);
    d.recordExternalEvm(adoption);
    const auto projection = xerFinancialProjectionHash(*d.project());
    FinancialPeriodSubmission sub;
    sub.period = {"", "외부 채택 마감", p.baselines.back().id, "2026-10-19", "2026-10-26", {}};
    sub.reporter = "검토자";
    sub.reason = "외부 정책을 포함한 당시 값";
    sub.recordedAt = "2026-11-08T05:00:00Z";
    sub.confirmed = true;
    const auto id = d.recordFinancialPeriod(sub);
    sub.period.id = id;
    sub.action = "close";
    d.recordFinancialPeriod(sub);
    const auto o = external_evm_fixture::options(p);
    const auto frozen = buildFinancialPeriodReport(*d.project(), o, id);
    check(number(frozen, 2) == 1000 && number(frozen, 3) == 1200, "external source amounts not used in period close");
    check(xerFinancialProjectionHash(*d.project()) == projection && d.project()->xerFinancials == p.xerFinancials,
          "period changes affected source/projection");
    adoption.withdrawn = true;
    adoption.useEv = adoption.usePv = false;
    adoption.rows.clear();
    d.recordExternalEvm(adoption);
    check(reportJson(buildFinancialPeriodReport(*d.project(), o, id))["tables"].toArray()[0] ==
              reportJson(frozen)["tables"].toArray()[0],
          "later external policy restated close");
    check(json::decodeProject(json::encodeProject(*d.project()), 30) == *d.project(), "mixed schema29/30 histories");
}
void workers() {
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = (tmp.path() + "/source.ppm").toStdString();
    period_evm_fixture::make(store, path);
    Document d(store);
    d.open(path);
    auto sub = submission(*d.project());
    auto request = d.makeEditRequest(FinancialPeriodEdit{sub});
    auto c = std::make_shared<CalculationControl>();
    c->cancel();
    auto cancelled = Document::prepareEdit(std::move(request), c);
    check(cancelled->state() == EditState::Cancelled, "cancel worker");
    auto ready =
        Document::prepareEdit(d.makeEditRequest(FinancialPeriodEdit{sub}), std::make_shared<CalculationControl>());
    check(ready->state() == EditState::Ready, "prepare period");
    d.renameTask(0, "stale");
    check(d.applyPreparedEdit(*ready) == EditState::Stale, "stale period accepted");
    ready = Document::prepareEdit(d.makeEditRequest(FinancialPeriodEdit{sub}), std::make_shared<CalculationControl>());
    d.save();
    check(d.applyPreparedEdit(*ready) == EditState::Applied && d.dirty(),
          "same-input save lost correct dirty bookkeeping");
    sub.period.id = d.project()->financialPeriodEvents.back().period.id;
    bool authorized = true;
    d.setWriteAuthorization([&] { return authorized; });
    ready = Document::prepareEdit(d.makeEditRequest(FinancialPeriodEdit{sub}), std::make_shared<CalculationControl>());
    authorized = false;
    check(d.applyPreparedEdit(*ready) != EditState::Applied, "revoked write accepted");
    authorized = true;
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.recordFinancialPeriod(sub); });
    auto p = *d.project();
    p.financialPeriodEvents.resize(513);
    rejects([&] { validate(p); });
    // Explicit zero, signed EV and unknown native opening remain distinct.
    sub.period.values = {{p.tasks[0].id, 0, -100}, {p.tasks[1].id, {}, 0}};
    const auto id = d.recordFinancialPeriod(sub);
    auto o = period_evm_fixture::options(*d.project());
    const auto r = buildFinancialPeriodReport(*d.project(), o, id);
    check(number(r, 2) == 3000 && number(r, 3) == -100, "zero/signed period EV");
    auto unknown = transition(*d.project(), id, "define");
    unknown.period.fromDate = "2026-10-24";
    unknown.period.values.clear();
    d.recordFinancialPeriod(unknown);
    const auto u = buildFinancialPeriodReport(*d.project(), o, id);
    check(std::holds_alternative<std::monostate>(u.tables[0].rows[0][3].value), "unknown opening became zero");
    d.setStatusAt("2026-10-26 10:00");
    auto premature = transition(*d.project(), id, "define");
    premature.period.values = {{d.project()->tasks[0].id, 100, 100}};
    rejects([&] { d.recordFinancialPeriod(premature); });

    rejects([&] { d.recordFinancialPeriod(transition(*d.project(), id, "close")); });
    for (const auto &date : {"0001-01-01", "9999-12-31"}) {
        auto edge = submission(*d.project());
        edge.period.fromDate = edge.period.toDate = date;
        edge.period.values.clear();
        const auto edgeId = d.recordFinancialPeriod(edge);
        const auto er = buildFinancialPeriodReport(*d.project(), o, edgeId);
        check(number(er, 2) == 0, "civil date endpoint PV");
        if (std::string(date) == "9999-12-31")
            check(std::holds_alternative<std::monostate>(er.tables[0].rows[0][3].value), "future EV was predicted");
    }
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const int font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/malgun.ttf");
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        contracts();
        workers();
        externalIntegration();
        std::cout
            << "PASS custom periods, manual/native EVM, immutable close/reopen/reclose, historical revision, backdated "
               "locks, schema30/copy/recovery/undo, corruption, worker cancel/stale/permission and exports\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
