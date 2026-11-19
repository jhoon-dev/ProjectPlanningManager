#include "application/Document.h"
#include "calculation/Resources.h"
#include "calculation/LevelingFloat.h"
#include "calculation/Tracking.h"
#include "calculation/NetworkDiagram.h"
#include "persistence/ProjectJson.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "ProgressLevelingFixture.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include "calculation/WorkCalendar.h"
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
using namespace ppm;
namespace {
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
Project fixture(const char *profile = "native-fs0-workminutes-v1", int capacity = 100) {
    Project p;
    p.documentId = "leveling-float";
    p.name = "자원 순서 여유";
    p.startDate = "2026-11-02";
    p.calculationProfile = profile;
    p.tasks = {{"A", "A", 480, {}}, {"B", "B", 480, {}}, {"C", "C", 480, {}}};
    Resource r;
    r.id = "R";
    r.name = "인력";
    r.maxUnits = capacity;
    r.rates = {{"2026-11-02", 60}};
    p.resources = {r};
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    return p;
}
LevelingSelection selection(const Document &d) {
    LevelingSelection s;
    s.policy = levelingPolicyFor(*d.project());
    s.reason = "자원 순서 검토";
    s.adoptedAt = "2026-11-03T00:00:00Z";
    s.sourceDocumentId = d.project()->documentId;
    s.sourceRevision = d.project()->revision;
    return s;
}
void contracts() {
    LevelingFloatScope historicalWindowContracts(2);
    // With ample capacity, resource-order float must reduce to the logical CPM result.
    for (const auto *profile : {pdmProfile, constraintProfile, effortProfile})
        for (const auto type : {DependencyType::FS, DependencyType::SS, DependencyType::FF, DependencyType::SF})
            for (const auto lag : {-120, 120}) {
                auto p = fixture(profile, 200);
                p.tasks[1].predecessors = {"A"};
                p.tasks[1].dependencyRules = {{"A", type, lag}};
                p.tasks[2].durationMinutes = 1440;
                const auto logical = calculatePlan(p);
                const auto leveled = levelResources(p, "task-order", 260).leveled;
                check(leveled.levelingFloatCalculated, "ample-capacity relationship float unavailable");
                for (std::size_t i = 0; i < p.tasks.size(); ++i)
                    check(leveled.tasks[i].start == logical.tasks[i].start &&
                              leveled.tasks[i].finish == logical.tasks[i].finish &&
                              leveled.tasks[i].totalFloatMinutes == logical.tasks[i].totalFloatMinutes &&
                              leveled.tasks[i].freeFloatMinutes == logical.tasks[i].freeFloatMinutes &&
                              leveled.tasks[i].critical == logical.tasks[i].critical,
                          "ample-capacity float differs from logical CPM");
            }
    for (const auto *profile : {"native-fs0-workminutes-v1", pdmProfile, constraintProfile, effortProfile}) {
        const auto p = fixture(profile);
        const auto before = p;
        const auto level = levelResources(p, "task-order", 260);
        const auto &r = level.leveled;
        check(r.status == CalculationStatus::Valid && r.levelingFloatCalculated, "float unavailable for basic profile");
        check(r.tasks[0].critical && r.tasks[1].critical && r.tasks[0].totalFloatMinutes == 0 &&
                  r.tasks[1].freeFloatMinutes == 0,
              "resource critical chain");
        check(r.tasks[2].totalFloatMinutes == 480 && r.tasks[2].freeFloatMinutes == 480 && !r.tasks[2].critical,
              "independent resource float");
        check(r.tasks[2].start == "2026-11-02 09:00" && r.projectFinish == "2026-11-03 18:00" && p == before,
              "float moved source dates");
        check(r.resourceOrderLinks.size() == 1 && r.resourceOrderLinks[0].predecessorId == "A" &&
                  r.resourceOrderLinks[0].taskId == "B" && r.resourceOrderLinks[0].driving,
              "resource driver missing across nonworking night");
        auto parallel = fixture(profile, 200);
        parallel.assignments.push_back({"C", "R", 100});
        const auto two = levelResources(parallel, "task-order", 260).leveled;
        check(two.levelingFloatCalculated && two.tasks[0].critical && two.tasks[2].critical &&
                  two.tasks[1].totalFloatMinutes == 480,
              "parallel lane flow");
        auto deadline = p;
        deadline.resources[0].exceptions = {{"2026-11-04", 0}};
        check(levelResources(deadline, "task-order", 260).leveled.levelingFloatCalculated, "capacity reduction fence");
        auto combined = combined_fixture::project(profile);
        const auto withPolicy = levelResources(combined, "task-order", 260).leveled;
        check(withPolicy.levelingFloatCalculated && withPolicy.tasks[0].totalFloatMinutes == 240 &&
                  withPolicy.tasks[1].critical && withPolicy.tasks[2].critical && withPolicy.tasks[3].critical,
              "combined float/critical");
        check(withPolicy.tasks[4].completedActual && !hasScheduleFloat(withPolicy, withPolicy.tasks[4]),
              "actual completion future float");
        check(withPolicy.tasks[1].start == "2026-11-02 14:00" && withPolicy.tasks[3].finish == "2026-11-04 14:00",
              "combined dates changed");
    }
    // A resource deadline at Tuesday midnight consumes C's otherwise available day.
    auto fence = fixture();
    fence.assignments = {{"C", "R", 100}};
    fence.tasks[0].durationMinutes = 960;
    fence.resources[0].exceptions = {{"2026-11-03", 0}};
    const auto fenced = levelResources(fence, "task-order", 260).leveled;
    check(fenced.levelingFloatCalculated && fenced.tasks[2].totalFloatMinutes == 0,
          "capacity drop float crossed fence");
    auto split = fixture(effortProfile);
    split.tasks[0].breaks = {{"2026-11-02 12:00", "2026-11-03 09:00"}};
    split.tasks[1].durationMinutes = 240;
    {
        LevelingFloatScope historical(1);
        const auto interleaved = levelResources(split, "task-order", 260).leveled;
        check(interleaved.status == CalculationStatus::Valid && !interleaved.levelingFloatCalculated &&
                  !interleaved.levelingFloatReason.empty() && !hasScheduleFloat(interleaved, interleaved.tasks[0]),
              "1.1 interleaving replay changed");
    }
    for (const auto type : {TaskType::FixedDuration, TaskType::FixedWork, TaskType::FixedUnits}) {
        auto candidate = split;
        candidate.tasks[0].taskType = type;
        if (type != TaskType::FixedDuration) {
            candidate.tasks[0].workDriven = true;
            candidate.tasks[0].workUnitMinutes = 48000;
        }
        const auto saved = candidate;
        const auto result = levelResources(candidate, "task-order", 260).leveled;
        if (!result.levelingFloatCalculated)
            throw std::runtime_error(result.levelingFloatReason);
        check(result.tasks[0].totalFloatMinutes == 0 && result.tasks[0].critical &&
                  result.tasks[1].totalFloatMinutes == 60 && result.tasks[1].freeFloatMinutes == 60 &&
                  result.tasks[2].totalFloatMinutes == 300 && candidate == saved,
              "cross-split numeric float or source preservation");
        check(result.tasks[0].finish == "2026-11-03 15:00" && result.tasks[1].start == "2026-11-02 13:00" &&
                  result.resourceOrderLinks.size() == 2 &&
                  result.calculationPolicy.ends_with("resource-segment-float-v2"),
              "cross-split dates, segment handovers or policy");
        auto slack = candidate;
        slack.tasks[2].durationMinutes = 1440;
        const auto withSlack = levelResources(slack, "task-order", 260).leveled;
        check(withSlack.levelingFloatCalculated && withSlack.tasks[0].totalFloatMinutes == 179 &&
                  withSlack.tasks[0].freeFloatMinutes == 179 && !withSlack.tasks[0].critical,
              "cross-split internal work was incorrectly frozen");
    }
    auto multi = split;
    auto criticalSplit = split;
    criticalSplit.tasks[1].durationMinutes = 300;
    const auto criticalSegments = levelResources(criticalSplit, "task-order", 260).leveled;
    check(criticalSegments.levelingFloatCalculated && criticalSegments.tasks[0].critical &&
              criticalSegments.tasks[1].critical &&
              std::any_of(
                  criticalSegments.resourceOrderLinks.begin(), criticalSegments.resourceOrderLinks.end(),
                  [](const auto &link) { return link.predecessorId == "B" && link.taskId == "A" && link.driving; }),
          "critical resource path into a resumed segment");
    auto second = multi.resources.front();
    second.id = "S";
    multi.resources.push_back(second);
    multi.assignments.push_back({"A", "S", 100});
    multi.assignments.push_back({"B", "S", 100});
    multi.resources[0].maxUnits = 200;
    const auto shared = levelResources(multi, "task-order", 260).leveled;
    check(shared.levelingFloatCalculated && shared.tasks[1].totalFloatMinutes == 60,
          "multiple-resource cross-split float");
    auto progress = split;
    progress.statusAt = "2026-11-02 10:00";
    progress.progress = {progress_fixture::entry(progress, "A", 480, 60, 420, "2026-11-02 09:00")};
    progress.tasks[0].predecessors = {"C"};
    progress.progressPolicy = ProgressPolicySelection{
        overrideProgressPolicy, "PM", "2026-11-03T00:00:00Z", "교차 분할 잔여", progress.documentId, progress.revision};
    const auto future = levelResources(progress, "task-order", 260).leveled;
    check(future.levelingFloatCalculated && future.tasks[0].start == "2026-11-02 09:00" &&
              future.tasks[0].remainingStart == "2026-11-02 10:00" && future.tasks[1].freeFloatMinutes == 60,
          "cross-split progress override actual-start preservation");
    auto capacitySplit = split;
    capacitySplit.resources[0].exceptions = {{"2026-11-02", 150}};
    const auto capacityResult = levelResources(capacitySplit, "task-order", 260).leveled;
    check(capacityResult.levelingFloatCalculated && capacityResult.tasks[1].totalFloatMinutes == 60,
          "cross-split capacity reduction");
    auto p = fixture(pdmProfile);
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    p.tasks[0].predecessors = {"B"};
    p.tasks[0].dependencyRules = {{"B", DependencyType::SF, 0}};
    const auto cycle = levelResources(p, "task-order", 260).leveled;
    check(cycle.levelingFloatCalculated, "zero-weight point/resource cycle not solved");
    QTemporaryDir temp;
    JsonProjectStore store;
    {
        const auto path = temp.filePath("split.ppm").toStdString();
        store.save(path, split, {});
        Document cross(store);
        cross.open(path);
        cross.adoptLeveling(selection(cross));
        const auto graph = buildNetworkGraph(*cross.project());
        check(graph.cycleComponents == 0 && graph.edges.size() == 2 && graph.edges[0].releaseAt == "2026-11-02 12:00" &&
                  graph.edges[1].acquireAt == "2026-11-03 09:00",
              "segment handovers became logical cycles or lost timestamps");
        cross.captureBaseline("구간 기준선", "PM", "검증", "2026-11-03T00:00:00Z");
        auto oldCross = *cross.project();
        auto &baseline = oldCross.baselines.front();
        Project input;
        static_cast<PlanInput &>(input) = baseline.input;
        input.documentId = baseline.sourceDocumentId;
        input.revision = baseline.inputRevision;
        const auto v11 = calculatePlan(input, "ppm-adopted-leveling-1.1");
        check(!v11.levelingFloatCalculated && v11.tasks[0].finish == "2026-11-03 15:00",
              "historical cross-split baseline replay");
        baseline.engineVersion = v11.engineVersion;
        baseline.schedule = v11.tasks;
        baseline.projectFinish = *v11.projectFinish;
        verifyBaseline(baseline);
        check(json::decodeProject(json::encodeProject(oldCross), nativeSchemaFor(oldCross)).baselines.front() ==
                  baseline,
              "1.1 split baseline archive changed");
        cross.save();
        Document reopened(store);
        reopened.open(path, true);
        check(calculate(*reopened.project()).tasks == calculate(*cross.project()).tasks, "cross-split native reopen");
        cross.clearLeveling();
        cross.undo();
        check(calculate(*cross.project()).levelingFloatCalculated, "cross-split undo");
    }
    Document d(store);
    d.create("자원 순서 여유", "2026-11-02", {"A", "B", "C"});
    for (int i = 0; i < 3; ++i)
        d.setDuration(i, 480);
    auto resource = fixture().resources[0];
    resource.id.clear();
    const auto rid = d.putResource(resource);
    d.setAssignments(d.project()->tasks[0].id, {{d.project()->tasks[0].id, rid, 100}});
    d.setAssignments(d.project()->tasks[1].id, {{d.project()->tasks[1].id, rid, 100}});
    d.adoptLeveling(selection(d));
    check(d.calculation()->levelingFloatCalculated, "selected schedule discarded float");
    const auto id = d.captureBaseline("새 평준화 기준선", "PM", "검토", "2026-11-03T00:00:00Z");
    (void)id;
    check(d.project()->baselines.back().engineVersion == "ppm-adopted-leveling-1.2", "new baseline engine identity");
    auto old = *d.project();
    auto &b = old.baselines[0];
    Project bp;
    static_cast<PlanInput &>(bp) = b.input;
    bp.documentId = b.sourceDocumentId;
    bp.revision = b.inputRevision;
    const auto legacy = calculatePlan(bp, "ppm-adopted-leveling-1.0");
    check(!legacy.levelingFloatCalculated && legacy.engineVersion == "ppm-adopted-leveling-1.0",
          "old baseline replay capability");
    b.engineVersion = legacy.engineVersion;
    b.schedule = legacy.tasks;
    b.projectFinish = legacy.projectFinish.value();
    verifyBaseline(b);
    check(json::decodeProject(json::encodeProject(old), nativeSchemaFor(old)).baselines[0] == b,
          "legacy baseline native roundtrip");
    d.saveAs(temp.filePath("selected.ppm").toStdString());
    Document reopened(store);
    reopened.open(d.path(), true);
    check(calculate(*reopened.project()).tasks == calculate(*d.project()).tasks, "reopen float changed");
    const auto graph = buildNetworkGraph(*d.project());
    check(std::any_of(graph.edges.begin(), graph.edges.end(),
                      [](const auto &e) { return !e.resourceId.empty() && e.driving == NetworkDriving::Yes; }),
          "network resource edge");
    NetworkQuery query;
    query.focus = d.project()->tasks[1].id;
    query.mode = NetworkMode::DrivingAncestors;
    const auto window = networkWindow(graph, query);
    check(!window.drivingUnavailable && window.nodes.size() == 2, "resource critical-path traversal");
    d.clearLeveling();
    check(!calculate(*d.project()).resourceLeveled, "clear leveling");
    d.undo();
    check(calculate(*d.project()).levelingFloatCalculated, "undo float replay");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope eval(control);
        (void)levelResources(fixture(), "task-order", 260);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "float cancellation");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        contracts();
        QJsonArray metrics;
        for (int count : {1000, 10000}) {
            auto p = fixture();
            p.tasks.clear();
            p.assignments.clear();
            for (int i = 0; i < count; ++i) {
                const auto id = "T" + std::to_string(i);
                p.tasks.push_back({id, id, 1, {}});
                p.assignments.push_back({id, "R", 100});
            }
            auto r = calculatePlan(p);
            WorkCalendar cal(p);
            for (int i = 0; i < count; ++i) {
                auto &row = r.tasks[i];
                row.start = cal.boundary(i, false);
                row.finish = cal.boundary(i + 1, true);
                row.earlyStartMinutes = i;
                row.earlyFinishMinutes = i + 1;
            }
            r.projectFinish = cal.boundary(count, true);
            r.resourceLeveled = true;
            QElapsedTimer timer;
            timer.start();
            calculateLevelingFloat(p, r);
            check(r.levelingFloatCalculated && r.resourceOrderLinks.size() == std::size_t(count - 1) &&
                      std::all_of(r.tasks.begin(), r.tasks.end(),
                                  [](const auto &row) { return row.critical && row.totalFloatMinutes == 0; }),
                  "large resource-flow chain");
            metrics.append(QJsonObject{{"tasks", count},
                                       {"derived_float_ms", double(timer.nsecsElapsed()) / 1000000},
                                       {"resource_links", count - 1}});
        }
        QDir().mkpath("leveling-float-qa");
        QFile file("leveling-float-qa/core-metrics.json");
        check(file.open(QIODevice::WriteOnly), "metrics write");
        file.write(QJsonDocument(metrics).toJson());
        QJsonArray segmentMetrics;
        for (int count : {1000, 10000}) {
            auto p = fixture(effortProfile);
            p.tasks[0].breaks = {{"2026-11-02 12:00", "2026-11-03 09:00"}};
            p.tasks[1].durationMinutes = 240;
            while (p.tasks.size() < std::size_t(count)) {
                const auto id = "I" + std::to_string(p.tasks.size());
                p.tasks.push_back({id, id, 1, {}});
            }
            auto r = calculatePlan(p);
            r.tasks[1].start = "2026-11-02 13:00";
            r.tasks[1].finish = "2026-11-02 17:00";
            WorkCalendar clock(p);
            r.tasks[1].earlyStartMinutes = clock.offset(r.tasks[1].start);
            r.tasks[1].earlyFinishMinutes = clock.offset(r.tasks[1].finish);
            r.resourceLeveled = true;
            QElapsedTimer timer;
            timer.start();
            calculateLevelingFloat(p, r);
            check(r.levelingFloatCalculated && r.resourceOrderLinks.size() == 2 && r.tasks[0].critical &&
                      r.tasks[1].totalFloatMinutes == 60,
                  "large cross-split analysis");
            segmentMetrics.append(QJsonObject{
                {"tasks", count}, {"resource_links", 2}, {"derived_float_ms", double(timer.nsecsElapsed()) / 1000000}});
        }
        QDir().mkpath("segment-leveling-float-qa");
        QFile segmentFile("segment-leveling-float-qa/core-metrics.json");
        check(segmentFile.open(QIODevice::WriteOnly), "segment metrics write");
        segmentFile.write(QJsonDocument(segmentMetrics).toJson());
        std::cout << "leveling float: engines, resource flow, capacity fences, cycles, actuals, baseline versions, "
                     "lifecycle and drivers passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
