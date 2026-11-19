#include "UsageFixture.h"
#include "application/Document.h"
#include "domain/PeriodActuals.h"
#include "domain/Classification.h"
#include "calculation/Usage.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectFile.h"
#include "persistence/PeriodActualJson.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <future>
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
    check(failed, "expected rejection");
}
PeriodActualEntry row(std::string resource, std::string date, std::optional<std::int64_t> work,
                      std::optional<std::int64_t> cost) {
    PeriodActualEntry e;
    e.taskId = "A";
    e.resourceId = std::move(resource);
    e.date = std::move(date);
    e.workUnitMinutes = work;
    e.actualMinor = cost;
    return e;
}
PeriodActualSubmission submission(std::vector<PeriodActualEntry> entries, std::optional<std::int64_t> total = 80,
                                  bool cost = true) {
    PeriodActualSubmission s;
    s.entries = std::move(entries);
    if (cost)
        s.totals = {{"A", total}};
    s.reporter = "PM";
    s.reason = "실제 관측 제출";
    s.recordedAt = "2026-11-03T01:00:00Z";
    return s;
}
std::shared_ptr<Document::PreparedEdit>
prepare(Document &doc, PeriodActualSubmission s,
        std::shared_ptr<CalculationControl> control = std::make_shared<CalculationControl>()) {
    auto request = doc.makeEditRequest(PeriodActualEdit{std::move(s)});
    return std::async(std::launch::async,
                      [request = std::move(request), control]() mutable {
                          return Document::prepareEdit(std::move(request), control);
                      })
        .get();
}
void contracts() {
    check(parseActualHours("8") == 48000 && parseActualHours("0.0055") == 33 && parseActualHours("6001u") == 6001,
          "exact work parser failed");
    for (const auto &bad : {"-1", "0.000001", "1e2", "NaN", "-1u", "1.5u", "90000000000"})
        rejects([&] { parseActualHours(bad); });
    QTemporaryDir temp;
    JsonProjectStore store;
    auto p = usage_fixture::project();
    usage_fixture::progress(p);
    p.statusAt = "2026-11-03 18:00";
    const auto path = (temp.path() + "/project.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    const auto before = *doc.project();
    const auto plan = calculatePlan(before).tasks, forecast = calculate(before).tasks;
    auto prepared = prepare(doc, submission({row("R", "2026-11-02", 48000, 30), row("E", "2026-11-03", 6000, 10),
                                             row({}, "2026-11-03", {}, 5)}));
    check(prepared->state() == EditState::Ready && *doc.project() == before, "preparation changed live input");
    check(doc.applyPreparedEdit(*prepared) == EditState::Applied && !doc.canUndo(), "submission not immutable/atomic");
    check(doc.project()->periodActuals.size() == 3 && doc.project()->costEntries.size() == 4 && doc.dirty(),
          "period/cumulative journal missing");
    auto a = actualCostAllocations(*doc.project(), *doc.project()->statusAt).at("A");
    check(a.cumulative == 80 && a.allocated == 45 && a.unallocated == 35 && a.costReports == 3,
          "80=45+35 reconciliation failed");
    check(analyzeCosts(*doc.project(), p.baselines.front().id).evm.ac == 80, "EVM double-counted allocations");
    check(costAsOf(*doc.project(), "A", *doc.project()->statusAt)->physicalBasisPoints == 0,
          "physical EV changed on actual submission");
    check(doc.project()->baselines == before.baselines && calculatePlan(*doc.project()).tasks == plan &&
              calculate(*doc.project()).tasks == forecast,
          "actual input changed plan/forecast/frozen baseline");
    const UsageOptions o{"2026-11-02", "2026-11-08", p.baselines.front().id, {}, UsagePeriod::Day};
    auto usage = buildUsage(*doc.project(), o);
    auto rows = usageRows(usage, UsageHierarchy::Tasks);
    auto actual = usageReportedTotal(rows[0].reportedActual);
    check(actual.labor == 48000 && actual.equipment == 6000 && actual.cost == 45 && rows[0].cumulativeActual == 80 &&
              rows[0].unallocatedActual == 35,
          "Usage partial actual totals wrong");
    check(usageReportedTotal(usageRows(usage, UsageHierarchy::Resources)[0].reportedActual).cost == 45,
          "resource actual double counted");
    const auto original = doc.project()->periodActuals.front();
    doc.recordPeriodActuals(submission({row("R", "2026-11-02", 36000, 25)}));
    check(doc.project()->periodActuals.front() == original &&
              doc.project()->periodActuals.back().previousEntryId == original.id,
          "correction destroyed original");
    a = actualCostAllocations(*doc.project(), *doc.project()->statusAt).at("A");
    check(a.allocated == 40 && a.unallocated == 40, "same-status replacement summed correction twice");
    doc.renameTask(0, "수정 이름");
    doc.undo();
    check(doc.project()->periodActuals.size() == 4, "plan undo deleted facts");
    doc.redo();
    const auto stable = *doc.project();
    rejects([&] { doc.recordPeriodActuals(submission({row("R", "2026-11-02", 1, 90)}, 80)); });
    rejects([&] { doc.recordPeriodActuals(submission({row("R", "2026-11-02", 1, 1), row("R", "2026-11-02", 2, 2)})); });
    rejects([&] { doc.recordPeriodActuals(submission({row("R", "2026-11-04", 1, 1)})); });
    rejects([&] { doc.recordPeriodActuals(submission({row({}, "2026-11-02", 1, 1)})); });
    rejects([&] { doc.recordPeriodActuals(submission({row("R", "2026-11-02", 1, 1)}, 80, false)); });
    check(*doc.project() == stable, "failed batch partially committed");
    doc.setStatusAt("2026-11-04 18:00");
    doc.recordPeriodActuals(submission({row("R", "2026-11-02", 42000, 35)}, 90));
    doc.setStatusAt("2026-11-03 18:00");
    a = actualCostAllocations(*doc.project(), *doc.project()->statusAt).at("A");
    check(a.cumulative == 80 && a.allocated == 40, "as-of lookup used future report");
    const auto past = *doc.project();
    rejects([&] { doc.recordPeriodActuals(submission({row("E", "2026-11-02", 6000, 60)}, 100)); });
    check(*doc.project() == past, "past correction inconsistent with future AC was accepted");
    doc.setStatusAt("2026-11-02 18:00");
    a = actualCostAllocations(*doc.project(), *doc.project()->statusAt).at("A");
    check(a.cumulative == 80 && a.allocated == 0 && a.unallocated == 80, "legacy AC was invented as dated allocation");
    doc.setStatusAt("2026-11-04 18:00");
    doc.recordPeriodActuals(submission({row("R", "2026-11-02", {}, {})}, 90));
    a = actualCostAllocations(*doc.project(), *doc.project()->statusAt).at("A");
    check(a.allocated == 15 && a.unallocated == 75, "withdrawal was treated as zero actual/delete");
    doc.recordPeriodActuals(submission({row("R", "2026-11-02", 0, 0)}, 90));
    check(periodActualsAsOf(*doc.project(), *doc.project()->statusAt).at({"A", "R", "2026-11-02"})->workUnitMinutes ==
              0,
          "explicit zero lost");
    doc.setAssignments("A", {});
    auto retyped = doc.project()->resources[1];
    retyped.type = "labor";
    doc.putResource(retyped);
    rejects([&] { doc.removeResource("E"); });
    rejects([&] { doc.removeTasks({"A"}); });
    rejects([&] { doc.setCurrency("USD"); });
    doc.recordPeriodActuals(submission({row("E", "2026-11-03", 12000, 10)}, 90));
    check(doc.project()->periodActuals.back().resourceType == "equipment",
          "retired assignment correction changed unit");
    doc.save();
    check(nativeSchemaFor(*doc.project()) == 11 && store.load(path).project == *doc.project(),
          "schema11 native roundtrip failed");
    doc.renameTask(0, "복구할 변경");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-03T02:00:00Z"));
    session.reset();
    const auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema11 recovery scan failed");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(store);
    restored.recover(claim->snapshot);
    check(restored.project()->periodActuals == doc.project()->periodActuals &&
              restored.project()->costEntries == doc.project()->costEntries,
          "recovery lost actual/cumulative links");
    doc.saveAs((temp.path() + "/copy.ppm").toStdString());
    check(store.load(doc.path()).project.periodActuals == doc.project()->periodActuals,
          "copy lost original source IDs");
    Document ro(store);
    ro.open(doc.path(), true);
    rejects([&] { ro.recordPeriodActuals(submission({row("E", "2026-11-03", 0, 0)}, 90)); });
    auto pending = prepare(doc, submission({row("E", "2026-11-03", 1, 10)}, 90));
    doc.open(doc.path());
    check(doc.applyPreparedEdit(*pending) == EditState::Stale, "same-ID/revision new session accepted stale actuals");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    auto canceled = prepare(doc, submission({row("E", "2026-11-03", 1, 10)}, 90), control);
    check(canceled->state() == EditState::Cancelled && doc.applyPreparedEdit(*canceled) == EditState::Cancelled,
          "canceled facts applied");
    auto array = json::encodePeriodActuals(doc.project()->periodActuals);
    auto bad = array[0].toObject();
    bad["surprise"] = true;
    array[0] = bad;
    rejects([&] { json::decodePeriodActuals(array); });
    bad = array[0].toObject();
    bad.remove("surprise");
    bad["workUnitMinutes"] = 1.5;
    array[0] = bad;
    rejects([&] { json::decodePeriodActuals(array); });
    UsageValue high;
    high.labor = maxWork;
    UsageValue one;
    one.labor = 1;
    const auto overflow = usageReportedTotal({high, one, one});
    check(!overflow.labor && overflow.reportOverflow[0], "reported overflow resurrected partial value");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        contracts();
        std::cout << "period actuals: atomic "
                     "ledger/precision/correction/asof/reconciliation/AC/frozen/schema/recovery/stale/cancel passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
