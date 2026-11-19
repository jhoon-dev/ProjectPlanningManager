#include "ProgressLevelingFixture.h"
#include "application/Document.h"
#include "calculation/ProgressPolicies.h"
#include "calculation/Usage.h"
#include "calculation/NetworkDiagram.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonArray>
#include <algorithm>
#include <future>
#include <iostream>
using namespace ppm;
namespace {
void check(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
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
LevelingSelection selection(const Document &d) {
    LevelingSelection s;
    s.policy = levelingPolicyFor(*d.project());
    s.adoptedAt = "2026-11-03T00:00:00Z";
    s.reason = "진척 정책에 자원 용량 적용";
    s.sourceDocumentId = d.project()->documentId;
    s.sourceRevision = d.project()->revision;
    return s;
}
auto prepare(Document &d, EditCommand c,
             std::shared_ptr<CalculationControl> ctrl = std::make_shared<CalculationControl>()) {
    return std::async(std::launch::async, [r = d.makeEditRequest(std::move(c)),
                                           ctrl]() mutable { return Document::prepareEdit(std::move(r), ctrl); })
        .get();
}
void engines() {
    for (const auto *profile : {"native-fs0-workminutes-v1", pdmProfile, constraintProfile, effortProfile}) {
        std::cout << "engine " << profile << std::endl;
        const auto p = combined_fixture::project(profile);
        const auto original = p;
        const auto result = levelResources(p, "task-order", 260);
        check(result.original.status == CalculationStatus::Valid && result.leveled.status == CalculationStatus::Valid &&
                  result.after.status == CalculationStatus::Valid,
              "combined engine status");
        check(result.original.tasks[1].finish == "2026-11-03 14:00" &&
                  result.leveled.tasks[1].finish == "2026-11-03 14:00",
              "masked incoming relation restored by leveling");
        check(result.leveled.tasks[3].remainingStart == "2026-11-03 14:00" &&
                  result.leveled.tasks[3].finish == "2026-11-04 14:00",
              "shared-resource delay");
        check(result.leveled.tasks[1].start == "2026-11-02 14:00" &&
                  result.leveled.tasks[4].finish == "2026-11-02 18:00",
              "actual dates moved");
        check(result.leveled.tasks[2].remainingStart == "2026-11-03 14:00" && p == original,
              "unstarted edge lost or source mutated");
        check(result.leveled.resourceLeveled && hasScheduleFloat(result.leveled, result.leveled.tasks[1]),
              "false CPM float");
        check(std::all_of(result.after.days.begin(), result.after.days.end(),
                          [](const auto &v) { return v.overloadMinutes == 0; }),
              "capacity violation");
        auto retained = p;
        retained.progressPolicy->policy = retainedProgressPolicy;
        const auto a = levelResources(retained, "task-order", 260);
        check(a.leveled.status == CalculationStatus::Valid && a.leveled.tasks[1].finish == "2026-11-04 14:00",
              "A lost incoming dependency");
        auto unknown = p;
        unknown.resources[0].maxUnits.reset();
        check(levelResources(unknown, "task-order", 5).leveled.status == CalculationStatus::Incomplete,
              "unknown capacity accepted");
        check(levelResources(p, "task-order", 1).leveled.status != CalculationStatus::Valid, "horizon ignored");
        auto cycle = p;
        cycle.tasks[0].predecessors = {"C"};
        check(levelResources(cycle, "task-order", 260).leveled.status == CalculationStatus::Conflict,
              "mask hid original cycle");
        for (const auto &priority : {"shortest-first", "longest-first"}) {
            const auto alt = levelResources(p, priority, 260);
            check(alt.leveled.status == CalculationStatus::Valid && alt.after.status == CalculationStatus::Valid,
                  "priority combination failed");
        }
    }
    std::cout << "relation matrix" << std::endl;
    for (auto type : {DependencyType::FS, DependencyType::SS, DependencyType::FF, DependencyType::SF})
        for (int lag : {-120, 120}) {
            auto p = combined_fixture::project(pdmProfile);
            p.tasks[1].dependencyRules = {{"A", type, lag}};
            const auto result = levelResources(p, "task-order", 260);
            check(result.leveled.status == CalculationStatus::Valid, "PDM relation mask status");
            check(result.leveled.tasks.at(1).finish == "2026-11-03 14:00", "PDM relation mask/lag");
        }
    auto constrained = combined_fixture::project(constraintProfile);
    constrained.tasks[3].constraintType = ConstraintType::MSO;
    constrained.tasks[3].constraintAt = "2026-11-03 09:00";
    check(levelResources(constrained, "task-order", 260).leveled.status == CalculationStatus::Conflict,
          "leveling moved mandatory start");
    auto work = combined_fixture::project(effortProfile);
    work.tasks[1].taskType = TaskType::FixedWork;
    work.tasks[1].workDriven = true;
    work.tasks[1].workUnitMinutes = 48000;
    work.progress[1].remainingWorkUnitMinutes = 24000;
    const auto fixedWork = levelResources(work, "task-order", 260);
    check(fixedWork.leveled.status == CalculationStatus::Valid, "remaining fixed work status");
    check(fixedWork.leveled.tasks.at(1).finish == "2026-11-03 14:00", "remaining fixed work");
    work.tasks[3].breaks = {{"2026-11-03 14:00", "2026-11-03 18:00"}};
    const auto split = levelResources(work, "task-order", 260);
    check(split.leveled.status == CalculationStatus::Valid && split.after.status == CalculationStatus::Valid,
          "manual split combination");
}
void lifecycle() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("project.ppm").toStdString();
    const auto p = combined_fixture::project();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    const auto baseline = d.captureBaseline("원본 계획", "PM", "동결", "2026-11-03T00:00:00Z");
    const auto frozen = d.project()->baselines;
    const auto before = *d.project();
    auto prepared = prepare(d, AdoptLevelingEdit{selection(d)});
    check(prepared->state() == EditState::Ready && *d.project() == before, "worker isolation");
    check(d.applyPreparedEdit(*prepared) == EditState::Applied, "combined adoption");
    const auto combined = *d.project();
    const auto schedule = calculate(combined);
    check(nativeSchemaFor(combined) == 15 && d.calculation()->tasks == schedule.tasks &&
              schedule.calculationPolicy.find("progress-leveling-v1/native-progress-override-v1") != std::string::npos,
          "combined provenance/publish/schema");
    check(combined.tasks == p.tasks && combined.progress == p.progress && combined.baselines == frozen,
          "source facts mutated");
    check(calculatePlan(combined).tasks == calculatePlan(before).tasks, "plan changed");
    auto usage = buildUsage(combined, {"2026-11-02", "2026-11-06", baseline, {}, UsagePeriod::Day});
    const auto rows = usageRows(usage, UsageHierarchy::Tasks);
    check(usageTotal(rows[0].values[2]).labor == 72000 && usageTotal(rows[0].values[2]).cost == 960,
          "combined remaining cost/work");
    check(analyzeCosts(combined, baseline).plan == analyzeCosts(before, baseline).plan, "plan budget revalued");
    const auto graph = buildNetworkGraph(combined);
    check(graph.edges[0].driving == NetworkDriving::Inactive &&
              hasScheduleFloat(graph.calculation, graph.calculation.tasks[1]),
          "network inactive/float");
    const auto comparison = compareProgressPolicies(combined);
    check(comparison.computed && comparison.retained.tasks[1].finish == "2026-11-04 14:00" &&
              comparison.overridden.tasks[3].finish == "2026-11-03 18:00",
          "comparison must precede leveling");
    d.clearLeveling();
    check(d.project()->progressPolicy && !d.project()->remainingLeveling &&
              calculate(*d.project()).tasks[3].finish == "2026-11-03 18:00",
          "clear leveling changed policy");
    d.undo();
    check(d.project()->remainingLeveling && d.project()->progressPolicy, "undo combination");
    d.redo();
    d.undo();
    d.setDuration(3, 240);
    check(!d.project()->remainingLeveling && d.project()->progressPolicy, "input invalidation");
    d.undo();
    d.setStatusAt("2026-11-04 09:00");
    check(!d.project()->remainingLeveling && d.project()->progressPolicy, "status invalidation");
    d.undo();
    auto next = *d.project()->progressPolicy;
    next.policy = retainedProgressPolicy;
    next.sourceRevision = d.project()->revision;
    d.adoptProgressPolicy(next);
    check(!d.project()->remainingLeveling && d.project()->progressPolicy->policy == retainedProgressPolicy,
          "policy switch reused level selection");
    d.undo();
    check(d.project()->remainingLeveling && d.project()->progressPolicy->policy == overrideProgressPolicy,
          "atomic policy/level undo");
    d.clearProgressPolicy();
    check(!d.project()->progressPolicy && !d.project()->remainingLeveling, "clear policy must release level");
    d.undo();
    d.save();
    Document ro(store);
    ro.open(path, true);
    check(calculate(*ro.project()).tasks == calculate(*d.project()).tasks, "reopen combination");
    rejects([&] { ro.clearLeveling(); });
    d.saveAs(temp.filePath("copy.ppm").toStdString());
    check(d.project()->remainingLeveling && d.project()->progressPolicy, "SaveAs combination");
    d.renameTask(0, "복구"); // Name edits release leveling by the existing input rule.
    d.adoptLeveling(selection(d));
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-03T02:00:00Z"));
    session.reset();
    auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "recovery14");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(store);
    restored.recover(claim->snapshot);
    check(calculate(*restored.project()).tasks == calculate(*d.project()).tasks &&
              restored.project()->progressPolicy == d.project()->progressPolicy,
          "recovery changed combination");
    const auto selected = *d.project();
    auto ctrl = std::make_shared<CalculationControl>();
    auto cancel = prepare(d, ClearLevelingEdit{}, ctrl);
    ctrl->cancel();
    check(d.applyPreparedEdit(*cancel) == EditState::Cancelled && *d.project() == selected, "cancel commit");
    auto stale = prepare(d, AdoptLevelingEdit{selection(d)});
    d.renameTask(0, "stale");
    check(d.applyPreparedEdit(*stale) == EditState::Stale, "stale source commit");
    d.adoptLeveling(selection(d));
    const auto effective = calculate(*d.project());
    const auto id = d.saveScenario("조합 동결", "task-order", 260, "2026-11-03T02:00:00Z", d.project()->documentId,
                                   d.project()->revision);
    (void)id;
    const auto frozenScenario = d.project()->scenarios.back();
    check(frozenScenario.progressPolicy == d.project()->progressPolicy && nativeSchemaFor(*d.project()) == 15,
          "scenario lost policy");
    d.clearProgressPolicy();
    check(!d.project()->progressPolicy && nativeSchemaFor(*d.project()) == 15 &&
              levelResources(scenarioProject(frozenScenario), frozenScenario.priority, frozenScenario.horizonWorkDays,
                             frozenScenario.policy)
                      .leveled.tasks == effective.tasks,
          "scenario replay changed with current policy");
    const auto body = json::encodeProject(*d.project());
    check(json::decodeProject(body, 15) == *d.project(), "scenario JSON15");
    rejects([&] { json::decodeProject(body, 14); });
    rejects([&] { json::decodeProject(body, 13); });
    auto malformed = body;
    auto scenarios = malformed["scenarios"].toArray();
    auto s = scenarios[0].toObject();
    s["progressPolicy"] = QJsonValue::Null;
    scenarios[0] = s;
    malformed["scenarios"] = scenarios;
    rejects([&] { json::decodeProject(malformed, 15); });
    const auto both = json::encodeProject(combined);
    rejects([&] { json::decodeProject(both, 13); });
    check(json::decodeProject(both, 15) == combined, "combined15 roundtrip");
    d.setStatusAt({});
    auto dormant = *d.project();
    check(dormant.scenarios.back().progressPolicy.has_value(), "frozen policy discarded");
    auto inactive = combined_fixture::project();
    inactive.statusAt.reset();
    const auto dormantPath = temp.filePath("dormant.ppm").toStdString();
    store.save(dormantPath, inactive, {});
    Document plan(store);
    plan.open(dormantPath);
    plan.adoptLeveling(selection(plan));
    check(plan.project()->planLeveling && plan.project()->progressPolicy && !calculate(*plan.project()).isForecast &&
              nativeSchemaFor(*plan.project()) == 15,
          "dormant plan combination");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        engines();
        std::cout << "lifecycle" << std::endl;
        lifecycle();
        std::cout
            << "combined progress/leveling engines, active graph, facts, lifecycle, schema14 and scenarios passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
