#include "RepartitionFloatFixture.h"
#include "ProgressPolicyFixture.h"
#include "application/Document.h"
#include "calculation/LevelingFloat.h"
#include "calculation/Effort.h"
#include "calculation/Tracking.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/ProjectJson.h"
#include "domain/Classification.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
using namespace jobs;
namespace {
void check(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
ScheduleResult level(const Project &p) {
    auto r = levelResources(p, "task-order", 260).leveled;
    if (!r.levelingFloatCalculated)
        throw std::runtime_error(r.levelingFloatReason);
    return r;
}
void contracts() {
    auto p = repartition_fixture::project();
    const auto original = p;
    for (const auto type : {TaskType::FixedDuration, TaskType::FixedWork, TaskType::FixedUnits}) {
        auto variant = p;
        variant.tasks[0].taskType = type;
        if (type != TaskType::FixedDuration) {
            variant.tasks[0].workDriven = true;
            variant.tasks[0].workUnitMinutes = 48000;
        }
        const auto r = level(variant);
        check(r.tasks[0].totalFloatMinutes == 660 && r.tasks[0].freeFloatMinutes == 660 && !r.tasks[0].critical,
              "A disappearance/repartition float");
        check(r.tasks[1].totalFloatMinutes == 540 && r.tasks[1].freeFloatMinutes == 60,
              "total/free resource witness distinction");
        check(r.tasks[2].critical && r.tasks[2].totalFloatMinutes == 0, "independent critical task");
        check(r.tasks[0].start == "2026-11-02 09:00" && r.tasks[0].finish == "2026-11-03 15:00" && p == original,
              "forward dates or inputs changed");
        check(r.calculationPolicy.ends_with("resource-repartition-float-v3"), "repartition policy identity");
        WorkCalendar clock(variant);
        check(clock.boundary(r.tasks[0].lateStartMinutes, false) == "2026-11-04 09:00",
              "A latest start did not retire old windows");
        check(clock.boundary(r.tasks[1].lateStartMinutes, false) == "2026-11-03 14:00", "B latest witness");
        check(std::count_if(r.diagnostics.begin(), r.diagnostics.end(),
                            [](const auto &d) { return d.code == "LEVELING_REPARTITION_WITNESS"; }) == 2,
              "feasible witnesses missing");
    }
    auto capacity = p;
    capacity.resources[0].exceptions = {{"2026-11-04", 0}};
    const auto capped = level(capacity);
    check(capped.tasks[0].totalFloatMinutes == 180 && capped.tasks[0].freeFloatMinutes == 180,
          "non-monotone capacity domain");
    auto unknown = p;
    unknown.resources[0].exceptions = {{"2026-11-04", {}}};
    const auto uncertified = levelResources(unknown, "task-order", 260).leveled;
    check(uncertified.status == CalculationStatus::Valid && !uncertified.levelingFloatCalculated &&
              uncertified.levelingFloatReason.find("미정") != std::string::npos,
          "unknown future capacity became certified float");
    auto fs = p;
    fs.calculationProfile = "native-fs0-workminutes-v1";
    fs.tasks[0].breaks.clear();
    fs.resources[0].exceptions = {{"2026-11-04", 0}};
    const auto fsResult = level(fs);
    check(fsResult.tasks[0].totalFloatMinutes == 240 && fsResult.tasks[1].freeFloatMinutes == 240,
          "working-coordinate profile used civil-minute capacities incorrectly");
    auto successor = p;
    successor.tasks.push_back({"D", "후행 D", 240, {"B"}});
    successor.taskCosts.push_back({"D", 0});
    const auto logical = level(successor);
    check(logical.tasks[1].totalFloatMinutes == 540 && logical.tasks[1].freeFloatMinutes == 0,
          "logical successor free float");
    auto parallel = p;
    parallel.assignments[0].units = 50;
    parallel.assignments[1].units = 50;
    const auto both = level(parallel);
    check(both.tasks[0].totalFloatMinutes == 660 && both.tasks[1].totalFloatMinutes == 1200,
          "parallel fractional resource demand");
    auto future = p;
    future.statusAt = "2026-11-02 10:00";
    future.progress = {progress_fixture::entry(future, "A", 480, 60, 420, "2026-11-02 09:00")};
    future.tasks[0].predecessors = {"C"};
    future.progressPolicy = ProgressPolicySelection{
        overrideProgressPolicy, "PM", "2026-11-03T00:00:00Z", "재분할 잔여", future.documentId, future.revision};
    const auto forecast = level(future);
    check(forecast.tasks[0].start == "2026-11-02 09:00" && forecast.tasks[0].remainingStart == "2026-11-02 10:00" &&
              forecast.tasks[0].totalFloatMinutes > 179,
          "forecast actual/policy preservation");
    auto limit = p;
    for (int i = 3; i < 66; ++i) {
        const auto id = "T" + std::to_string(i);
        limit.tasks.push_back({id, id, 1, {}});
        limit.assignments.push_back({id, "R", 100});
    }
    LevelingFloatScope legacyLimit(3);
    const auto bounded = levelResources(limit, "task-order", 260).leveled;
    check(bounded.status == CalculationStatus::Valid && !bounded.levelingFloatCalculated &&
              bounded.levelingFloatReason.find("64") != std::string::npos,
          "bounded search invented float");
    LevelingFloatScope current(4);
    QTemporaryDir dir;
    JsonProjectStore store;
    const auto path = dir.filePath("repartition.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    LevelingSelection selection;
    selection.policy = levelingPolicyFor(*d.project());
    selection.reason = "재분할 검토";
    selection.adoptedAt = "2026-11-03T00:00:00Z";
    selection.sourceDocumentId = d.project()->documentId;
    selection.sourceRevision = d.project()->revision;
    d.adoptLeveling(selection);
    d.captureBaseline("새 기준선", "PM", "검토", "2026-11-03T00:00:00Z");
    check(d.project()->baselines[0].engineVersion == "jobs-adopted-leveling-1.4", "new baseline version");
    auto archive = *d.project();
    auto &baseline = archive.baselines[0];
    Project input;
    static_cast<PlanInput &>(input) = baseline.input;
    input.documentId = baseline.sourceDocumentId;
    input.revision = baseline.inputRevision;
    const auto old = calculatePlan(input, "jobs-adopted-leveling-1.2");
    check(old.tasks[0].totalFloatMinutes == 179 && old.tasks[1].totalFloatMinutes == 60,
          "1.2 baseline silently upgraded");
    baseline.engineVersion = old.engineVersion;
    baseline.schedule = old.tasks;
    baseline.projectFinish = *old.projectFinish;
    verifyBaseline(baseline);
    check(json::decodeProject(json::encodeProject(archive), nativeSchemaFor(archive)).baselines[0] == baseline,
          "legacy archive roundtrip");
    d.save();
    Document reopened(store);
    reopened.open(path, true);
    check(calculate(*reopened.project()).tasks == calculate(*d.project()).tasks, "native repartition reopen");
    d.clearLeveling();
    d.undo();
    check(calculate(*d.project()).tasks[0].totalFloatMinutes == 660, "repartition undo");
    auto result = level(p);
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope scope(control);
        calculateRepartitionLevelingFloat(p, result);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled && result.tasks[0].totalFloatMinutes == 660, "repartition cancellation partial result");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QElapsedTimer timer;
        timer.start();
        contracts();
        QDir().mkpath("repartition-float-qa");
        QFile file("repartition-float-qa/core-metrics.json");
        check(file.open(QIODevice::WriteOnly), "metrics write");
        file.write(QJsonDocument(QJsonObject{{"contracts_ms", double(timer.nsecsElapsed()) / 1000000},
                                             {"A_total", 660},
                                             {"B_total", 540},
                                             {"B_free", 60}})
                       .toJson());
        std::cout << "Repartition float: disappearing windows, exact capacity, witnesses, policy, legacy baseline and "
                     "lifecycle passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
