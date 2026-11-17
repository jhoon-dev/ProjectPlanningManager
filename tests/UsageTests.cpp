#include "UsageFixture.h"
#include "calculation/Usage.h"
#include "calculation/Effort.h"
#include "calculation/Resources.h"
#include "persistence/JsonProjectStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
using namespace jobs;
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught, "expected rejection");
}
UsageOptions options(const Project &p) {
    return {"2026-11-02", "2026-11-15", p.baselines.front().id, {}, UsagePeriod::Day};
}
UsageValue root(const UsageResult &r, std::size_t s = 0, UsageHierarchy h = UsageHierarchy::Tasks) {
    return usageTotal(usageRows(r, h).front().values[s]);
}
void totals() {
    auto p = usage_fixture::project();
    const auto before = p;
    auto o = options(p);
    auto r = buildUsage(p, o);
    auto total = root(r);
    check(total.labor == 96000 && total.equipment == 24000 && total.cost == 212,
          "independent 16h/4h/212 budget mismatch");
    check(root(r, 1).cost == 212 && !root(r, 2).labor, "baseline or no-status unknown mismatch");
    check(root(r, 0, UsageHierarchy::Resources).cost == 212, "resource hierarchy double-counted fixed cost");
    auto rows = usageRows(r, UsageHierarchy::Tasks);
    check(rows.front().values[0][0].cost == 88 && rows.front().values[0][1].cost == 84 &&
              rows.front().values[0][2].cost == 40,
          "daily rounding/accrual/rate split mismatch");
    check(analyzeCosts(p, o.baselineId).plan == total.cost, "Usage and cost analysis disagree");
    auto load = resourceLoad(p, calculatePlan(p));
    std::int64_t demand = 0;
    for (const auto &d : load.days)
        demand += d.demandUnitMinutes;
    check(demand == *total.labor + *total.equipment, "Usage work disagrees with load integral");
    for (const auto period : {UsagePeriod::Week, UsagePeriod::Month}) {
        o.period = period;
        auto aggregate = buildUsage(p, o);
        check(root(aggregate).labor == total.labor && root(aggregate).cost == total.cost,
              "week/month lost daily values");
    }
    o.period = UsagePeriod::Week;
    o.from = "2026-11-04";
    o.to = "2026-11-10";
    r = buildUsage(p, o);
    check(r.buckets.size() == 2 && r.buckets[0].to == "2026-11-08" && r.buckets[1].from == "2026-11-09" &&
              root(r).cost == 40,
          "partial Monday week clipping mismatch");
    o.period = UsagePeriod::Month;
    o.from = "2026-10-31";
    o.to = "2026-11-01";
    r = buildUsage(p, o);
    check(r.buckets.size() == 2 && root(r).cost == 0, "partial month clipping mismatch");
    o = options(p);
    o.wbsId = "W";
    r = buildUsage(p, o);
    check(root(r).cost == 172 && root(r).labor == 48000 && r.taskOrder.size() == 2,
          "WBS scope or predecessor graph changed");
    check(p == before, "Usage mutated project");
    o = options(p);
    o.baselineId.clear();
    check(!root(buildUsage(p, o), 1).cost, "missing baseline became zero");
    p.resources[0].rates.clear();
    o = options(p);
    r = buildUsage(p, o);
    check(!root(r).cost && root(r).labor == 96000 && root(r, 1).cost == 212,
          "unknown rate contaminated work or frozen baseline");
    p = before;
    p.taskCosts[0].fixedMinor.reset();
    r = buildUsage(p, options(p));
    check(!root(r).cost && usageRows(r, UsageHierarchy::Tasks).front().values[0][2].cost == 40,
          "unknown fixed cost spread beyond task dates");
    p = before;
    p.tasks[0].durationMinutes.reset();
    r = buildUsage(p, options(p));
    check(!r.available[0] && !root(r).labor && root(r, 1).cost == 212, "incomplete schedule reported zero");
}
void tracking() {
    auto p = usage_fixture::project();
    usage_fixture::progress(p);
    auto r = buildUsage(p, options(p));
    check(root(r, 2).labor == 72000 && root(r, 2).equipment == 12000 && root(r, 2).cost == 74,
          "remaining 12h/2h/resource74 mismatch");
    check(usageRows(r, UsageHierarchy::Tasks).front().unallocatedActual == 80, "unallocated actual ledger lost");
    for (const auto &leaf : r.leaves)
        check(!leaf.unallocatedActual, "invented assignment actual cost");
    auto correction = p.costEntries[0];
    correction.id = "corrected";
    correction.previousEntryId = p.costEntries[0].id;
    correction.actualMinor = 75;
    correction.reason = "정정";
    correction.recordedAt = "2026-11-03T01:00:00Z";
    p.costEntries.push_back(correction);
    r = buildUsage(p, options(p));
    check(usageRows(r, UsageHierarchy::Tasks).front().unallocatedActual == 75, "latest correction not selected");
    auto later = options(p);
    later.from = "2026-11-10";
    r = buildUsage(p, later);
    check(root(r).cost == 0 && usageRows(r, UsageHierarchy::Tasks).front().unallocatedActual == 75,
          "cumulative actual was allocated to display period");
    auto completed = p;
    completed.statusAt = "2026-11-03 18:00";
    completed.progress[0].statusAt = *completed.statusAt;
    completed.progress[0].actualFinish = "2026-11-03 18:00";
    completed.progress[0].actualDurationMinutes = 960;
    completed.progress[0].remainingMinutes = 0;
    r = buildUsage(completed, options(completed));
    check(root(r, 2).labor == 48000 && root(r, 2).equipment == 0 && root(r, 2).cost == 40,
          "completed work reappeared as remaining demand");
    p.resources[0].rates = {{"2026-11-02", 100}};
    r = buildUsage(p, options(p));
    check(root(r, 1).cost == 212 && root(r).cost == 1740, "current rate revalued frozen baseline");
    p = usage_fixture::project();
    p.tasks.erase(p.tasks.begin() + 1);
    std::erase_if(p.assignments, [](const auto &a) { return a.taskId == "B"; });
    std::erase_if(p.taskCosts, [](const auto &c) { return c.taskId == "B"; });
    r = buildUsage(p, options(p));
    check(root(r).cost == 172 && root(r, 1).cost == 212 && r.taskNames.contains("B"),
          "deleted baseline task disappeared");
    const auto leaf = std::find_if(r.leaves.begin(), r.leaves.end(),
                                   [](const auto &row) { return row.taskId == "B" && row.resourceId == "R"; });
    check(leaf != r.leaves.end() && leaf->baselineOnly && usageTotal(leaf->values[0]).labor == 0 &&
              usageTotal(leaf->values[1]).labor == 48000,
          "deleted assignment stage membership wrong");
    JsonProjectStore store;
    QTemporaryDir temp;
    const auto path = (temp.path() + "/usage.ppm").toStdString();
    store.save(path, p, {});
    const auto loaded = store.load(path).project;
    check(root(buildUsage(loaded, options(loaded)), 1).cost == 212, "file roundtrip changed Usage");
}
void calendars() {
    auto p = usage_fixture::project();
    p.baselines.clear();
    p.tasks.resize(1);
    p.tasks[0].durationMinutes = 60;
    p.tasks[0].constraintType = ConstraintType::MSO;
    p.tasks[0].constraintAt = "2026-11-02 23:30";
    p.calculationProfile = effortProfile;
    p.assignments = {{"A", "R", 100}};
    p.calendars = {{"all", "24h", {1, 2, 3, 4, 5, 6, 7}, {{0, 1440}}, {}}};
    p.tasks[0].calendarId = "all";
    p.resources[0].calendarId = "all";
    p.taskCosts = {{"A", 1, "uniform", "physical"}};
    UsageOptions o{"2026-11-02", "2026-11-04", {}, {}, UsagePeriod::Day};
    auto r = buildUsage(p, o);
    auto rows = usageRows(r, UsageHierarchy::Tasks);
    check(rows[0].values[0][0].labor == 3000 && rows[0].values[0][1].labor == 3000, "midnight interval split wrong");
    p.calendars[0].holidays = {"2026-11-03"};
    r = buildUsage(p, o);
    rows = usageRows(r, UsageHierarchy::Tasks);
    check(rows[0].values[0][1].labor == 0 && rows[0].values[0][2].labor == 3000, "holiday demand invented");
    p.tasks[0].constraintType = ConstraintType::ASAP;
    p.tasks[0].constraintAt.reset();
    p.tasks[0].taskType = TaskType::FixedWork;
    p.tasks[0].workDriven = true;
    p.tasks[0].workUnitMinutes = 6001;
    p.assignments[0].units = 33;
    p.tasks[0].breaks = {{"2026-11-02 10:00", "2026-11-02 11:00"}};
    p.calendars[0].periods = {{540, 720}, {780, 1080}};
    p.resources[0].calendarId = "resource";
    p.calendars.push_back({"resource", "교집합", {1, 2, 3, 4, 5}, {{540, 660}}, {}});
    o.to = "2026-11-06";
    r = buildUsage(p, o);
    check(root(r).labor == 6001 && root(r).equipment == 0, "fractional final minute/intersection/break lost work");
    const auto schedule = calculatePlan(p);
    const auto demand = resourceLoad(p, schedule);
    std::int64_t work = 0;
    for (const auto &d : demand.days)
        work += d.demandUnitMinutes;
    check(work == 6001, "existing work integral mismatch");
}
void limits() {
    auto p = usage_fixture::project();
    auto o = options(p);
    o.to = "2026-02-30";
    rejects([&] { buildUsage(p, o); });
    o = options(p);
    o.to = "2027-12-31";
    rejects([&] { buildUsage(p, o); });
    o = options(p);
    o.wbsId = "absent";
    rejects([&] { buildUsage(p, o); });
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool canceled = false;
    try {
        buildUsage(p, options(p), control);
    } catch (const CalculationCancelled &) {
        canceled = true;
    }
    check(canceled, "cooperative cancellation lost");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        totals();
        tracking();
        calendars();
        limits();
        std::cout << "Usage: independent buckets/work/cost/frozen/ledger/limits passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
