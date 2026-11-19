#include "persistence/PrimaveraXer.h"
#include "application/Document.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "domain/PeriodActuals.h"
#include "domain/CalculationControl.h"
#include "calculation/Costs.h"
#include "calculation/Scheduler.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>
namespace {
using namespace ppm;
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "expected XER rejection");
}
QByteArray change(const QByteArray &source, const QString &name, const QString &column, int row, const QString &value) {
    auto lines = QString::fromUtf8(source).split('\n');
    bool inside = false;
    QStringList fields;
    for (auto &line : lines) {
        const bool cr = line.endsWith('\r');
        auto cells = (cr ? line.chopped(1) : line).split('\t');
        if (cells[0] == "%T") {
            inside = cells[1] == name;
            continue;
        }
        if (!inside)
            continue;
        if (cells[0] == "%F")
            fields = cells.mid(1);
        if (cells[0] == "%R" && row-- == 0) {
            auto i = fields.indexOf(column);
            check(i >= 0, "fixture field missing");
            cells[i + 1] = value;
            line = cells.join('\t') + (cr ? "\r" : "");
            return lines.join('\n').toUtf8();
        }
    }
    throw std::runtime_error("fixture row missing");
}
bool diagnostic(const PrimaveraXerExchange &exchange, const QString &path) {
    for (const auto &value : exchange.report["diagnostics"].toArray())
        if (value.toObject()["path"].toString().contains(path))
            return true;
    return false;
}
void write(const QString &name, const QByteArray &bytes) {
    QFile file(name);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "fixture write");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto source = readPrimaveraXer(QString::fromUtf8(PPM_XER_ACTUAL_FIXTURE));
        const auto imported = importPrimaveraXer(source, "observed.xer");
        const auto &p = imported.project;
        check(p.statusAt == "2026-10-28 18:00" && p.progress.size() == 2, "status/progress snapshot");
        const auto *progress = progressAsOf(p, "xer-task-20");
        check(progress && progress->actualStart == "2026-10-26 09:00" && !progress->actualFinish &&
                  progress->remainingMinutes == 240 && progress->actualDurationMinutes == 960,
              "active actual calendar duration");
        check(progressAsOf(p, "xer-task-30")->actualFinish == "2026-10-28 12:00" &&
                  progressAsOf(p, "xer-task-30")->actualDurationMinutes == 120,
              "completed dates");
        const auto *cost = costAsOf(p, "xer-task-20", *p.statusAt);
        check(cost && cost->actualMinor == 12280 && cost->physicalBasisPoints == 3725,
              "cumulative AC/physical percent");
        check(p.resources[0].rates.size() == 1 && p.resources[0].rates[0].hourlyMinor == 2550, "effective price");
        check(p.taskCosts.size() == 1 && p.taskCosts[0].fixedMinor == 10000, "planned direct expense");
        auto allocations = actualCostAllocations(p, *p.statusAt);
        check(p.periodActuals.size() == 4 && allocations.at("xer-task-20").allocated == 10580 &&
                  allocations.at("xer-task-20").unallocated == 1700,
              "resource/expense allocations must not double AC");
        std::int64_t units = 0;
        for (const auto &e : p.periodActuals)
            units += e.workUnitMinutes.value_or(0);
        check(units == 19500, "only observed dated work is allocated");
        check(p.baselines.empty() && p.scenarios.empty(), "external earned value must not fabricate a baseline");
        const auto details = imported.report["actualCosts"].toObject();
        check(details["assignmentTotals"].toArray()[0].toObject()["actualWorkUnitMinutes"] == "22500" &&
                  details["financialPeriods"].toArray().size() == 4,
              "aggregate details retained");
        const auto costAnalysis = analyzeCosts(p, "");
        check(costAnalysis.tasks[0].evm.ac == 12280 && !costAnalysis.evm.bac && !costAnalysis.evm.ev,
              "native AC without invented P6 EVM");
        const auto forecast = calculate(p);
        check(forecast.status == CalculationStatus::Valid, "imported remaining schedule");
        check(primaveraXerPreview(imported).contains("실적·원가") &&
                  primaveraXerPreview(imported).contains("122.80 USD"),
              "reviewable actual cost preview");
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary root");
        JsonProjectStore store;
        const auto folder = publishPrimaveraXer(imported, temporary.filePath("import"));
        check(store.load(QDir(folder).filePath("project.ppm").toStdString()).project == p, "imported native roundtrip");
        check(readPrimaveraXer(QDir(folder).filePath("original.xer")) == source, "exact XER preserved");
        rejects([&] { publishPrimaveraXer(imported, folder); });
        JsonRecoveryStore recovery(temporary.filePath("recovery"));
        auto session = recovery.newSession();
        Document recoveryDocument(store);
        recoveryDocument.open(QDir(folder).filePath("project.ppm").toStdString());
        recoveryDocument.renameTask(0, "Recovery actual cost");
        const auto dirty = *recoveryDocument.project();
        session->checkpoint(recoveryDocument.recoverySnapshot("2026-11-06T00:00:00Z"));
        session.reset();
        auto scan = recovery.scan();
        check(scan.candidates.size() == 1 && scan.candidates[0].valid, "actual cost recovery checkpoint");
        auto claim = recovery.claim(scan.candidates[0]);
        check(claim && claim->snapshot.project == dirty, "actual cost recovery snapshot");
        const auto exported = exportPrimaveraXer(p);
        check(exported.project == p, "export changed snapshot");
        const auto back = importPrimaveraXer(exported.xer, "roundtrip.xer");
        const auto tid = back.project.tasks[0].id;
        const auto roundtrip = actualCostAllocations(back.project, *back.project.statusAt).at(tid);
        check(costAsOf(back.project, tid, *back.project.statusAt)->actualMinor == 12280 &&
                  roundtrip.allocated == 10580 && roundtrip.unallocated == 1700,
              "XER cumulative/allocated/unallocated invariance");
        check(back.project.periodActuals.size() == 4 && back.project.resources[0].rates[0].hourlyMinor == 2550,
              "XER dated work/rates");
        const auto exportFolder = publishPrimaveraXer(exported, temporary.filePath("export"));
        check(store.load(QDir(exportFolder).filePath("source.ppm").toStdString()).project == p, "full source snapshot");
        auto next = p;
        next.documentId = "second-project";
        std::vector<PrimaveraXerExchange> exchanges{exported, exportPrimaveraXer(next)};
        const auto combined = combinePrimaveraXer(exchanges);
        const auto projects = listPrimaveraXerProjects(combined);
        check(projects.size() == 2, "combined actual project count");
        for (const auto &info : projects) {
            const auto chosen = importPrimaveraXer(combined, "combined.xer", XerEncoding::Utf8, info.id);
            check(chosen.project.periodActuals.size() == 4 && chosen.project.taskCosts.size() == 1 &&
                      costAsOf(chosen.project, chosen.project.tasks[0].id, *chosen.project.statusAt)->actualMinor ==
                          12280,
                  "combined identities/financial ownership");
        }
        auto mismatched = change(combined, "TRSRCFIN", "task_id", 2, "1");
        rejects([&] { listPrimaveraXerProjects(mismatched); });
        rejects([&] { importPrimaveraXer(mismatched, "excluded.xer", XerEncoding::Utf8, projects[0].id); });
        for (const auto &bad : QStringList{"-1", "1e2", "NaN", "0.001", "90000000000000"})
            rejects([&] { importPrimaveraXer(change(source, "TASKRSRC", "act_reg_cost", 0, bad), "bad amount"); });
        for (const auto &bad : QStringList{"-1", "0.000001", "1e2"})
            rejects([&] { importPrimaveraXer(change(source, "TRSRCFIN", "act_qty", 0, bad), "bad units"); });
        rejects([&] { importPrimaveraXer(change(source, "PROJECT", "last_recalc_date", 0, ""), "missing status"); });
        rejects(
            [&] { importPrimaveraXer(change(source, "TASK", "status_code", 0, "TK_Complete"), "inconsistent state"); });
        rejects([&] {
            importPrimaveraXer(change(source, "TASK", "act_start_date", 0, "2026-11-01 09:00"), "future actual");
        });
        rejects(
            [&] { importPrimaveraXer(change(source, "TASK", "remain_drtn_hr_cnt", 0, "0"), "active zero duration"); });
        rejects([&] {
            importPrimaveraXer(change(source, "TRSRCFIN", "act_cost", 0, "999.00"), "overallocated assignment");
        });
        rejects([&] {
            importPrimaveraXer(change(source, "TASKFIN", "act_expense_cost", 0, "999.00"), "overallocated expense");
        });
        rejects([&] { importPrimaveraXer(change(source, "TRSRCFIN", "act_qty", 0, "9"), "overallocated units"); });
        rejects([&] { importPrimaveraXer(change(source, "TRSRCFIN", "fin_dates_id", 0, "999"), "missing period"); });
        rejects([&] { importPrimaveraXer(change(source, "TRSRCFIN", "taskrsrc_id", 0, "102"), "wrong assignment"); });
        rejects([&] {
            importPrimaveraXer(change(source, "FINDATES", "end_date", 0, "2026-10-25 23:59"), "reversed period");
        });
        rejects([&] {
            importPrimaveraXer(change(source, "FINDATES", "end_date", 1, "2026-10-29 23:59"), "future period");
        });
        rejects([&] {
            importPrimaveraXer(change(source, "RSRCRATE", "start_date", 0, "2026-10-01 09:00"), "intraday rate");
        });
        const auto wide =
            importPrimaveraXer(change(source, "FINDATES", "start_date", 0, "2026-10-25 00:00"), "wide period");
        check(wide.project.periodActuals.size() == 2 &&
                  costAsOf(wide.project, "xer-task-20", *wide.project.statusAt)->actualMinor == 12280 &&
                  diagnostic(wide, "FINDATES/1"),
              "multi-day actuals must not invent daily allocation");
        auto zero = change(source, "TASKRSRC", "act_reg_cost", 1, "0.00");
        const auto zeroBack = importPrimaveraXer(zero, "zero");
        check(costAsOf(zeroBack.project, "xer-task-30", *zeroBack.project.statusAt)->actualMinor == 0,
              "explicit zero is reported");
        auto duplicate = source;
        duplicate.replace("%R\t101\t20\t100\t1\t2\t70.12",
                          "%R\t101\t20\t100\t1\t2\t70.12\r\n%R\t101\t20\t100\t1\t2\t70.12");
        rejects([&] { listPrimaveraXerProjects(duplicate); });
        auto ratePlan = p;
        ratePlan.resources[0].rates.push_back({"2026-11-01", 3000});
        const auto priceBack = importPrimaveraXer(exportPrimaveraXer(ratePlan).xer, "prices");
        check(priceBack.project.resources[0].rates.size() == 2 && priceBack.project.resources[0].maxUnits == 150,
              "effective prices preserve constant capacity");
        ratePlan.resources[0].rates.push_back({"2026-12-01", std::nullopt});
        const auto clearedPrice = importPrimaveraXer(exportPrimaveraXer(ratePlan).xer, "cleared price");
        check(clearedPrice.project.resources[0].rates.size() == 3 &&
                  !clearedPrice.project.resources[0].rates.back().hourlyMinor,
              "effective unknown price was silently inherited");
        const auto revisedEstimate =
            importPrimaveraXer(change(source, "TASK", "remain_drtn_hr_cnt", 2, "3"), "not started estimate");
        check(progressAsOf(revisedEstimate.project, "xer-task-40") &&
                  !progressAsOf(revisedEstimate.project, "xer-task-40")->actualStart &&
                  progressAsOf(revisedEstimate.project, "xer-task-40")->remainingMinutes == 180,
              "not-started remaining estimate lost");
        auto unknownTotal = p;
        for (auto &e : unknownTotal.costEntries)
            if (e.taskId == "xer-task-20")
                e.actualMinor.reset();
        validate(unknownTotal);
        rejects([&] { exportPrimaveraXer(unknownTotal); });
        auto future = p;
        ++future.revision;
        auto futureCost = *cost;
        futureCost.previousEntryId = futureCost.id;
        futureCost.id = "future-cost";
        futureCost.statusAt = "2026-11-01 18:00";
        futureCost.recordedRevision = future.revision;
        futureCost.actualMinor = 50000;
        future.costEntries.push_back(futureCost);
        const auto futureBack = importPrimaveraXer(exportPrimaveraXer(future).xer, "as of");
        check(costAsOf(futureBack.project, futureBack.project.tasks[0].id, *futureBack.project.statusAt)->actualMinor ==
                  12280,
              "future reports leaked into current status export");
        auto correction = p;
        ++correction.revision;
        futureCost.statusAt = *p.statusAt;
        futureCost.actualMinor = 15000;
        futureCost.recordedRevision = correction.revision;
        correction.costEntries.push_back(futureCost);
        const auto correctionBack = importPrimaveraXer(exportPrimaveraXer(correction).xer, "latest correction");
        check(costAsOf(correctionBack.project, correctionBack.project.tasks[0].id, *correctionBack.project.statusAt)
                      ->actualMinor == 15000,
              "latest same-status correction not selected");
        auto removedAssignment = p;
        removedAssignment.assignments.clear();
        const auto removedExport = exportPrimaveraXer(removedAssignment);
        const auto removedBack = importPrimaveraXer(removedExport.xer, "removed assignment");
        check(removedBack.project.assignments.empty() && removedBack.project.periodActuals.size() == 2 &&
                  costAsOf(removedBack.project, removedBack.project.tasks[0].id, *removedBack.project.statusAt)
                          ->actualMinor == 12280 &&
                  diagnostic(removedExport, "TASKRSRC/actuals"),
              "removed historic assignment synthesized a plan or lost AC");
        const auto currencyChanged = change(exported.xer, "CURRTYPE", "curr_short_name", 0, "EUR");
        rejects([&] { importPrimaveraXer(currencyChanged, "foreign price"); });
        rejects(
            [&] { importPrimaveraXer(change(exported.xer, "RSRC", "cost_qty_type", 0, "QT_Day"), "nonhourly price"); });
        for (const auto &currency : {"KRW", "JPY", "EUR"}) {
            auto moneyProject = p;
            moneyProject.currency = currency;
            moneyProject.taskCosts[0].accrual = "start";
            for (auto &e : moneyProject.costEntries)
                e.currency = currency;
            for (auto &e : moneyProject.periodActuals)
                e.currency = currency;
            const auto moneyBack = importPrimaveraXer(exportPrimaveraXer(moneyProject).xer, "currency");
            check(moneyBack.project.currency == currency && moneyBack.project.taskCosts[0].accrual == "start" &&
                      costAsOf(moneyBack.project, moneyBack.project.tasks[0].id, *moneyBack.project.statusAt)
                              ->actualMinor == 12280,
                  "currency minimum unit or expense accrual changed");
        }
        // A valid native snapshot can have work-only reports; explicit AC remains distinct from unknown.
        auto workOnly = p;
        workOnly.costEntries.clear();
        for (auto &entry : workOnly.periodActuals) {
            entry.actualMinor.reset();
            entry.costEntryId.clear();
        }
        validate(workOnly);
        const auto workBack = importPrimaveraXer(exportPrimaveraXer(workOnly).xer, "work only");
        check(!costAsOf(workBack.project, workBack.project.tasks[0].id, *workBack.project.statusAt),
              "work-only input must not create AC");
        workOnly.periodActuals[0].workUnitMinutes = 1;
        const auto unitBack = importPrimaveraXer(exportPrimaveraXer(workOnly).xer, "one work unit");
        bool oneUnit = false;
        for (const auto &e : unitBack.project.periodActuals)
            oneUnit |= e.workUnitMinutes == 1;
        check(oneUnit, "one percent-minute was rounded to a whole minute");
        auto control = std::make_shared<CalculationControl>();
        control->cancel();
        bool cancelled = false;
        try {
            EvaluationScope scope(control);
            (void)exportPrimaveraXer(p);
        } catch (const CalculationCancelled &) {
            cancelled = true;
        }
        check(cancelled, "cooperative cancellation");
        QDir().mkpath("xer-actual-cost-qa");
        write("xer-actual-cost-qa/export.xer", exported.xer);
        write("xer-actual-cost-qa/import-report.json", QJsonDocument(imported.report).toJson());
        write("xer-actual-cost-qa/export-report.json", QJsonDocument(exported.report).toJson());
        write("xer-actual-cost-qa/combined.xer", combined);
        const auto artifact = QDir::current().filePath("xer-actual-cost-qa/imported.ppm").toStdString();
        store.save(artifact, p,
                   QFileInfo::exists(QString::fromStdString(artifact)) ? store.load(artifact).fingerprint : "");
        std::cout << "PASS: XER observed progress, AC allocation, costs, daily periods, losses, persistence, recovery, "
                     "combined ownership and cancellation\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        if (const auto *error = dynamic_cast<const AppError *>(&e))
            for (const auto &argument : error->diagnostic().arguments)
                std::cerr << argument << '\n';
        return 1;
    }
}
