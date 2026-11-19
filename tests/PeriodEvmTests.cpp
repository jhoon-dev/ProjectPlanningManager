#include "PeriodEvmFixture.h"
#include "domain/Activities.h"
#include "domain/Classification.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/PrimaveraXer.h"
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
#include <algorithm>
#include <functional>
#include <iostream>
using namespace ppm;
namespace {
void check(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
template <class F> void rejects(F call) {
    try {
        call();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected period EVM rejection");
}
std::int64_t number(const ReportTable &t, std::size_t row, std::size_t col) {
    return std::get<std::int64_t>(t.rows.at(row).at(col).value);
}
bool unknown(const ReportTable &t, std::size_t row, std::size_t col) {
    return std::holds_alternative<std::monostate>(t.rows.at(row).at(col).value);
}
void write(const QString &p, const QByteArray &b) {
    QFile f(p);
    check(f.open(QIODevice::WriteOnly) && f.write(b) == b.size(), "write");
}
QJsonObject reportJson(const ProjectReport &r) {
    QJsonArray tables;
    for (const auto &t : r.tables) {
        QJsonArray rows, columns;
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
    return {{"policy", "native-period-evm-v1"}, {"tables", tables}};
}
void contracts() {
    QTemporaryDir tmp;
    JsonProjectStore store;
    auto p = period_evm_fixture::make(store, (tmp.path() + "/source.ppm").toStdString());
    const auto before = p;
    auto o = period_evm_fixture::options(p);
    auto r = buildPeriodEvmReport(p, o, "day");
    validateReportExport(r);
    const auto &s = r.tables[0];
    check(s.rows.size() == 5 && r.tables[1].rows.size() == 10 && r.tables[2].rows.size() == 10, "row counts");
    check(number(s, 0, 5) == 4000 && number(s, 0, 8) == 2000 && number(s, 0, 11) == 1900 && number(s, 0, 14) == 1800 &&
              number(s, 0, 15) == 100 && number(s, 0, 16) == -100,
          "first day values");
    check(std::abs(std::get<double>(s.rows[0][17].value) - 1900.0 / 1800) < 1e-12, "period ratio");
    check(number(s, 1, 8) == 2000 && number(s, 1, 11) == 800 && number(s, 1, 14) == 700, "second day differences");
    check(number(s, 2, 11) == -50 && number(s, 2, 14) == -50 && unknown(s, 2, 17) && unknown(s, 2, 18),
          "negative correction");
    check(number(s, 0, 21) == 1400 && number(s, 0, 22) == 400 && number(s, 1, 21) == 600 && number(s, 2, 21) == 50 &&
              number(s, 2, 22) == -100,
          "late dated allocation vs reporting delta");
    check(unknown(s, 4, 10) && unknown(s, 4, 11) && unknown(s, 4, 14) && unknown(s, 4, 21), "future actual predicted");
    check(number(s, 3, 10) == 2650 && number(s, 3, 13) == 2450 && number(s, 3, 19) == 3800,
          "closing EAC uses cumulative");
    auto latest = analyzeCosts(p, o.baselineId);
    check(number(s, 3, 7) == *latest.evm.pv && number(s, 3, 10) == *latest.evm.ev && number(s, 3, 13) == *latest.evm.ac,
          "existing native parity");
    for (const auto &f : {"cpi-v1", "cpi-spi-v1"}) {
        auto opts = o;
        opts.formula = f;
        auto alternative = buildPeriodEvmReport(p, opts, "day");
        check(number(alternative.tables[0], 3, 19) == *analyzeCosts(p, o.baselineId, f).evm.eac,
              "closing formula mismatch");
    }
    auto points = nativeEvmCurves(p, o.baselineId, o.formula,
                                  {{"2026-10-27 00:00", false, true}, {"2026-10-27 00:00", true, true}});
    check(points[0].points[0].value.ac == 600 && points[0].points[1].value.ac == 650 &&
              points[0].points[0].value.ev == 400 && points[0].points[1].value.ev == 450,
          "midnight half-open reports");
    auto scoped = o;
    scoped.wbsId = p.wbsNodes[1].id;
    auto onlyA = buildPeriodEvmReport(p, scoped, "day");
    check(number(onlyA.tables[0], 0, 5) == 1000 && number(onlyA.tables[0], 0, 11) == 400 &&
              onlyA.tables[2].rows.size() == 10,
          "scope intersection/baseline hierarchy");
    auto span = o;
    span.toDate = "2026-11-08";
    auto weeks = buildPeriodEvmReport(p, span, "week");
    check(weeks.tables[0].rows.size() == 2 && std::get<std::string>(weeks.tables[0].rows[0][1].value) == "2026-11-01",
          "Monday week");
    auto months = buildPeriodEvmReport(p, span, "month");
    check(months.tables[0].rows.size() == 2 && std::get<std::string>(months.tables[0].rows[0][1].value) == "2026-10-31",
          "month clipping");
    span.fromDate = "2028-02-28";
    span.toDate = "2028-03-01";
    check(std::get<std::string>(buildPeriodEvmReport(p, span, "month").tables[0].rows[0][1].value) == "2028-02-29",
          "leap month");
    auto midday = p;
    midday.statusAt = "2026-10-26 12:00";
    auto noon = buildPeriodEvmReport(midday, o, "day");
    // Per-task half-up: round(187.5)+round(562.5)=751, not round(750).
    check(number(noon.tables[0], 0, 7) == 751 && number(noon.tables[0], 0, 10) == 700 &&
              number(noon.tables[0], 0, 13) == 300 && number(noon.tables[0], 0, 21) == 0,
          "status cutoff/later date reports leaked");
    auto missing = p;
    missing.costEntries.clear();
    missing.periodActuals.clear();
    auto absent = buildPeriodEvmReport(missing, o, "day");
    check(unknown(absent.tables[0], 0, 11) && unknown(absent.tables[0], 0, 14) &&
              number(absent.tables[0], 0, 8) == 2000,
          "missing zero-filled");
    auto unset = missing;
    unset.statusAt.reset();
    check(unknown(buildPeriodEvmReport(unset, o, "day").tables[0], 0, 13), "missing status");
    auto opening = p;
    opening.periodActuals.clear();
    opening.costEntries[0].actualMinor.reset();
    opening.costEntries[0].physicalBasisPoints.reset();
    auto incomplete = buildPeriodEvmReport(opening, o, "day");
    check(unknown(incomplete.tables[0], 0, 11) && unknown(incomplete.tables[0], 0, 14) &&
              number(incomplete.tables[0], 0, 10) == 1900 && number(incomplete.tables[0], 0, 13) == 1800,
          "unknown opening with known closing");
    check(p == before, "calculation modified source");
    check(nativeSchemaFor(p) == 11 && store.load((tmp.path() + "/source.ppm").toStdString()).project == p,
          "schema/roundtrip changed");
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    {
        Document d(store);
        d.open((tmp.path() + "/source.ppm").toStdString());
        d.renameTask(0, "복구 대상 이름");
        session->checkpoint(d.recoverySnapshot("2026-11-08T03:00:00Z"));
        auto copied = (tmp.path() + "/copy.ppm").toStdString();
        d.saveAs(copied);
        check(d.project()->costEntries == p.costEntries && d.project()->periodActuals == p.periodActuals,
              "copy provenance");
        check(number(buildPeriodEvmReport(*d.project(), o, "day").tables[0], 0, 11) == 1900, "copy calculation");
    }
    session.reset();
    auto scan = recovery.scan();
    check(scan.candidates.size() == 1, "recovery scan");
    auto claim = recovery.claim(scan.candidates[0]);
    Document recovered(store);
    recovered.recover(claim->snapshot);
    check(number(buildPeriodEvmReport(*recovered.project(), o, "day").tables[0], 0, 14) == 1800,
          "recovery calculation");
    auto bad = o;
    bad.fromDate = "2026-10-31";
    rejects([&] { buildPeriodEvmReport(p, bad); });
    bad = o;
    bad.toDate = "bad";
    rejects([&] { buildPeriodEvmReport(p, bad); });
    rejects([&] { buildPeriodEvmReport(p, o, "quarter"); });
    bad = o;
    bad.baselineId = "missing";
    rejects([&] { buildPeriodEvmReport(p, bad); });
    bad = o;
    bad.formula = "invalid";
    rejects([&] { buildPeriodEvmReport(p, bad); });
    bad = o;
    bad.wbsId = "missing";
    rejects([&] { buildPeriodEvmReport(p, bad); });
    bad = o;
    bad.fromDate = "2020-01-01";
    bad.toDate = "2031-01-01";
    rejects([&] { buildPeriodEvmReport(p, bad, "day"); });
    auto edge = o;
    edge.fromDate = edge.toDate = "9999-12-31";
    check(number(buildPeriodEvmReport(p, edge, "day").tables[0], 0, 8) == 0, "final supported minute");
    edge.fromDate = edge.toDate = "0001-01-01";
    check(number(buildPeriodEvmReport(p, edge, "day").tables[0], 0, 8) == 0, "earliest civil date");
    auto mismatch = missing;
    mismatch.currency = "KRW";
    rejects([&] { buildPeriodEvmReport(mismatch, o); });
    bool cancelled = false;
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    try {
        EvaluationScope run(control);
        buildPeriodEvmReport(p, o);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cancellation swallowed");
    QDir().mkpath("native-period-evm-qa");
    for (const auto &name : {"source.ppm", "report.xlsx", "report.pdf"})
        QFile::remove(QString("native-period-evm-qa/") + name);
    write("native-period-evm-qa/source.json", QJsonDocument(json::encodeProject(p)).toJson());
    write("native-period-evm-qa/daily.json", QJsonDocument(reportJson(r)).toJson());
    write("native-period-evm-qa/weekly.json", QJsonDocument(reportJson(weeks)).toJson());
    write("native-period-evm-qa/monthly.json", QJsonDocument(reportJson(months)).toJson());
    store.save("native-period-evm-qa/source.ppm", p, {});
    exportXlsx(r, "native-period-evm-qa/report.xlsx", ReportFamily::Costs);
    PdfOptions pdf;
    pdf.a3 = true;
    pdf.font = QApplication::font();
    check(exportPdf(r, "native-period-evm-qa/report.pdf", ReportFamily::Costs, pdf) > 0, "PDF pages");
}
void creditAndAccrual() {
    JsonProjectStore store;
    for (bool steps : {false, true}) {
        Document d(store);
        d.create("credit", "2026-10-26", {"検査"});
        d.setDuration(0, 480);
        const auto id = d.project()->tasks[0].id;
        d.setTaskCost({id, 1000, "start", steps ? "physical" : "zero-hundred"});
        if (steps)
            d.putActivity({{}, "Steps", "steps", id, {}, {}, {}, {}, true, {{"s1", "one", 1}, {"s2", "two", 3}}});
        auto baseline = d.captureBaseline("B", "PM", "credit", "2026-11-08T00:00:00Z");
        d.setStatusAt("2026-10-25 23:59");
        d.recordCost(period_evm_fixture::cost(id, 0, 0));
        if (steps) {
            StepReport r;
            r.taskId = id;
            r.reporter = "PM";
            r.reason = "zero";
            r.recordedAt = "2026-11-08T00:00:00Z";
            r.steps = {{{"s1", "one", 1}, 0}, {{"s2", "two", 3}, 0}};
            d.recordSteps(r);
        } else {
            ProgressEntry r;
            r.taskId = id;
            r.reporter = "PM";
            r.reason = "not started";
            r.recordedAt = "2026-11-08T00:00:00Z";
            r.remainingMinutes = 480;
            d.recordProgress(r);
        }
        d.setStatusAt("2026-10-26 18:00");
        d.recordCost(period_evm_fixture::cost(id, 500, 9999));
        if (steps) {
            StepReport r;
            r.taskId = id;
            r.reporter = "PM";
            r.reason = "weighted";
            r.recordedAt = "2026-11-08T00:00:00Z";
            r.steps = {{{"s1", "one", 1}, 10000}, {{"s2", "two", 3}, 5000}};
            d.recordSteps(r);
        } else {
            ProgressEntry r;
            r.taskId = id;
            r.reporter = "PM";
            r.reason = "done";
            r.recordedAt = "2026-11-08T00:00:00Z";
            r.actualStart = "2026-10-26 09:00";
            r.actualFinish = "2026-10-26 18:00";
            r.actualDurationMinutes = 480;
            d.recordProgress(r);
        }
        auto o = period_evm_fixture::options(*d.project());
        o.baselineId = baseline;
        o.toDate = o.fromDate;
        auto r = buildPeriodEvmReport(*d.project(), o, "day");
        check(number(r.tables[0], 0, 8) == 1000 && number(r.tables[0], 0, 11) == (steps ? 625 : 1000),
              "Steps/0-100 or start accrual");
        d.setTaskCost({id, 9000, "finish", "physical"});
        check(number(buildPeriodEvmReport(*d.project(), o, "day").tables[0], 0, 8) == 1000,
              "current plan revalued baseline");
    }
}
void frozenRatesAndLimits() {
    JsonProjectStore store;
    Document d(store);
    d.create("frozen rate/calendar", "2026-11-02", {"A"});
    d.setCurrency("USD");
    d.setDuration(0, 960);
    d.setHolidays({"2026-11-03"});
    const auto id = d.project()->tasks[0].id;
    auto resource = d.putResource({{}, "R", "labor", 100, {}, {{"2026-11-02", 50}, {"2026-11-04", 100}}});
    d.setAssignments(id, {{id, resource, 100}});
    d.setTaskCost({id, 101, "uniform", "physical"});
    auto b = d.captureBaseline("frozen", "PM", "rates/holiday", "2026-11-08T00:00:00Z");
    d.setStatusAt("2026-11-01 23:59");
    d.recordCost(period_evm_fixture::cost(id, 0, 0));
    d.setStatusAt("2026-11-04 18:00");
    d.recordCost(period_evm_fixture::cost(id, 900, 5000));
    auto o = period_evm_fixture::options(*d.project());
    o.baselineId = b;
    o.fromDate = "2026-11-02";
    o.toDate = "2026-11-04";
    const auto report = buildPeriodEvmReport(*d.project(), o, "day");
    check(number(report.tables[0], 0, 5) == 1301 && number(report.tables[0], 0, 8) == 451 &&
              number(report.tables[0], 1, 8) == 0 && number(report.tables[0], 2, 8) == 850 &&
              number(report.tables[0], 2, 11) == 651,
          "frozen rate/holiday/fixed half-up");
    auto changed = d.project()->resources.front();
    changed.rates = {{"2026-11-02", 9999}};
    d.putResource(changed);
    check(number(buildPeriodEvmReport(*d.project(), o, "day").tables[0], 2, 8) == 850,
          "current rate revalued frozen PV");
    Document limit(store);
    limit.create("row budget", "2026-10-26", std::vector<std::string>(20, "T"));
    for (std::size_t i = 0; i < 20; ++i) {
        limit.setDuration(i, 0);
        limit.setTaskCost({limit.project()->tasks[i].id, 0, "start", "physical"});
    }
    limit.captureBaseline("B", "PM", "budget", "2026-11-08T00:00:00Z");
    auto large = period_evm_fixture::options(*limit.project());
    large.toDate = "2029-10-26";
    rejects([&] { buildPeriodEvmReport(*limit.project(), large, "day"); });
    Document foreign(store);
    foreign.create("currency", "2026-10-26", {"T"});
    foreign.setDuration(0, 480);
    foreign.captureBaseline("KRW", "PM", "currency", "2026-11-08T00:00:00Z");
    foreign.setCurrency("USD");
    auto incompatible = period_evm_fixture::options(*foreign.project());
    rejects([&] { buildPeriodEvmReport(*foreign.project(), incompatible); });
    Document overflow(store);
    overflow.create("overflow", "2026-10-26", {"T", "T"});
    for (std::size_t i = 0; i < 2; ++i) {
        overflow.setDuration(i, 480);
        overflow.setTaskCost({overflow.project()->tasks[i].id, maxMoney, "start", "physical"});
    }
    overflow.captureBaseline("B", "PM", "limit", "2026-11-08T00:00:00Z");
    check(unknown(buildPeriodEvmReport(*overflow.project(), period_evm_fixture::options(*overflow.project())).tables[0],
                  0, 5),
          "money overflow wrapped");
    Document cohort(store);
    cohort.create("cohort", "2026-10-26", {"deleted", "kept"});
    for (std::size_t i = 0; i < 2; ++i) {
        cohort.setDuration(i, 480);
        cohort.setTaskCost({cohort.project()->tasks[i].id, 100, "uniform", "physical"});
    }
    cohort.captureBaseline("B", "PM", "scope", "2026-11-08T00:00:00Z");
    cohort.removeTasks({cohort.project()->tasks[0].id});
    const auto all = buildPeriodEvmReport(*cohort.project(), period_evm_fixture::options(*cohort.project()));
    check(number(all.tables[0], 0, 5) == 200 && all.tables[1].rows.size() == 2, "deleted baseline task lost");
}
void externalSeparation() {
    QTemporaryDir tmp;
    JsonProjectStore store;
    auto imported =
        importPrimaveraXer(readPrimaveraXer(QString::fromUtf8(PPM_PERIOD_XER_FIXTURE)), "source.xer").project;
    check(imported.xerFinancials.has_value() &&
              std::any_of(imported.xerFinancials->periods.begin(), imported.xerFinancials->periods.end(),
                          [](const auto &v) { return v.bcwp && *v.bcwp > 0; }),
          "external EV fixture missing");
    auto path = (tmp.path() + "/external.ppm").toStdString();
    store.save(path, imported, {});
    Document d(store);
    d.open(path);
    d.captureBaseline("native baseline", "PM", "separate external EV", "2026-11-08T00:00:00Z");
    auto p = *d.project();
    const auto original = p;
    const auto o = period_evm_fixture::options(p);
    const auto with = buildPeriodEvmReport(p, o, "day");
    p.xerFinancials.reset();
    check(reportJson(with) == reportJson(buildPeriodEvmReport(p, o, "day")), "external XER EV/PV entered native EVM");
    check(*d.project() == original, "native EVM altered imported source");
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
        creditAndAccrual();
        frozenRatesAndLimits();
        externalSeparation();
        std::cout << "PASS: native period EVM daily/weekly/monthly, boundaries/status, missing/zero/corrections, "
                     "cumulative formulas, dated reconciliation, WBS, Steps/0-100, frozen rates/holiday/rounding, "
                     "immutable save/copy/recovery, limits/cancel, external EV separation and XLSX/PDF\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
