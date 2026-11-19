#include "RepartitionFloatFixture.h"
#include "calculation/LevelingFloat.h"
#include "calculation/Resources.h"
#include "calculation/Tracking.h"
#include "calculation/WorkCalendar.h"
#include "persistence/ProjectJson.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <iostream>

using namespace ppm;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
Project chain(int count, int slack, int fixedPrefix = 0, bool logical = true) {
    Project p;
    p.documentId = "scaled-repartition";
    p.name = "연결 묶음 재분할 여유";
    p.startDate = "2026-11-02";
    p.calculationProfile = constraintProfile;
    Resource r;
    r.id = "R";
    r.name = "공유 인력";
    r.exceptions = {{"2026-11-30", 100}};
    p.resources = {r};
    WorkCalendar clock(p, Task{});
    for (int i = 0; i < count; ++i) {
        const auto id = "T" + std::to_string(i);
        Task task{id, id, 1,
                  logical && i ? std::vector<std::string>{"T" + std::to_string(i - 1)} : std::vector<std::string>{}};
        if (i < fixedPrefix || !logical) {
            task.constraintType = i < fixedPrefix ? ConstraintType::MSO : ConstraintType::SNET;
            task.constraintAt = clock.boundary(i, false);
        }
        p.tasks.push_back(task);
        p.assignments.push_back({id, "R", 100});
    }
    p.tasks.push_back({"H", "독립 완료일", count + slack, {}});
    return p;
}
ScheduleResult analyze(const Project &p, int version = 4) {
    LevelingFloatScope scope(version);
    auto result = calculatePlan(p);
    check(result.status == CalculationStatus::Valid, "invalid fixture schedule");
    result.resourceLeveled = true;
    calculateLevelingFloat(p, result);
    return result;
}
QJsonObject verifyChain(int count, int slack, int fixedPrefix = 0, bool logical = true) {
    std::cout << "Checking chain " << count << " slack " << slack << " fixed " << fixedPrefix << " logical " << logical
              << std::endl;
    const auto p = chain(count, slack, fixedPrefix, logical);
    const auto original = calculatePlan(p);
    QElapsedTimer timer;
    timer.start();
    const auto result = analyze(p);
    const double elapsed = double(timer.nsecsElapsed()) / 1000000;
    check(result.levelingFloatCalculated, result.levelingFloatReason);
    check(result.resourceOrderLinks.size() == std::size_t(count - 1), "resource chain links");
    QJsonArray rows;
    for (int i = 0; i <= count; ++i) {
        const auto &row = result.tasks[i];
        const auto total = i == count || i < fixedPrefix ? 0 : slack;
        const auto free = i == count - 1 && i >= fixedPrefix ? slack : 0;
        check(row.totalFloatMinutes == total && row.freeFloatMinutes == free && row.critical == (total == 0),
              "chain total/free/critical " + row.taskId);
        check(row.start == original.tasks[i].start && row.finish == original.tasks[i].finish,
              "analysis changed forward dates");
        rows.append(QJsonObject{{"id", QString::fromStdString(row.taskId)},
                                {"start", QString::fromStdString(row.start)},
                                {"finish", QString::fromStdString(row.finish)},
                                {"total", double(row.totalFloatMinutes)},
                                {"free", double(row.freeFloatMinutes)},
                                {"critical", row.critical}});
    }
    const auto diagnostic = std::find_if(result.diagnostics.begin(), result.diagnostics.end(),
                                         [](const auto &d) { return d.code == "LEVELING_REPARTITION_SEARCH_POLICY"; });
    check(diagnostic != result.diagnostics.end() && diagnostic->arguments[0] == std::to_string(count),
          "search size provenance");
    const auto fixed = slack ? fixedPrefix : count;
    check(diagnostic->arguments[1] == std::to_string(fixed) &&
              diagnostic->arguments[2] == std::to_string(count - fixed),
          "fixed/movable provenance");
    const auto witnesses = std::count_if(result.diagnostics.begin(), result.diagnostics.end(),
                                         [](const auto &d) { return d.code == "LEVELING_REPARTITION_WITNESS"; });
    check(witnesses == count, "witness count");
    QJsonArray proofs;
    for (const auto &d : result.diagnostics)
        if (d.code == "LEVELING_REPARTITION_WITNESS")
            proofs.append(QJsonObject{{"task", QString::fromStdString(d.arguments[0])},
                                      {"total", std::stoi(d.arguments[1])},
                                      {"dates", QString::fromStdString(d.arguments[2])}});
    return {{"tasks", count},         {"slack", slack}, {"fixed_prefix", fixedPrefix}, {"logical_chain", logical},
            {"analysis_ms", elapsed}, {"rows", rows},   {"witnesses", proofs}};
}
void compatibility() {
    const auto p = repartition_fixture::project();
    for (int capacity : {100, 200}) {
        auto variant = p;
        variant.resources[0].maxUnits = capacity;
        LevelingFloatScope legacy(3);
        const auto before = levelResources(variant, "task-order", 260).leveled;
        LevelingFloatScope current(4);
        const auto after = levelResources(variant, "task-order", 260).leveled;
        check(before.levelingFloatCalculated && after.levelingFloatCalculated && before.tasks == after.tasks,
              "small repartition numerical compatibility");
    }
    for (const auto type : {DependencyType::FS, DependencyType::SS, DependencyType::FF, DependencyType::SF})
        for (const auto lag : {-1, 1}) {
            auto variant = chain(3, 3);
            variant.resources[0].maxUnits = 300;
            variant.tasks[1].dependencyRules = {{"T0", type, lag}};
            variant.tasks[2].dependencyRules = {{"T1", type, lag}};
            const auto old = analyze(variant, 3), now = analyze(variant);
            check(old.levelingFloatCalculated && now.levelingFloatCalculated && old.tasks == now.tasks,
                  "four-relation/signed-lag propagation compatibility");
        }
    auto aggregate = chain(3, 0, 0, false);
    for (int i = 0; i < 3; ++i) {
        aggregate.tasks[i].constraintType = ConstraintType::ASAP;
        aggregate.tasks[i].constraintAt.reset();
        aggregate.assignments[i].units = 50;
    }
    const auto summed = levelResources(aggregate, "task-order", 260).leveled;
    check(summed.levelingFloatCalculated && summed.tasks[0].totalFloatMinutes == 1 &&
              summed.tasks[1].totalFloatMinutes == 2 && summed.tasks[2].totalFloatMinutes == 1 &&
              summed.tasks[0].freeFloatMinutes == 0 && summed.tasks[1].freeFloatMinutes == 2 &&
              summed.tasks[2].freeFloatMinutes == 1,
          "three-way aggregate demand bypassed by pairwise propagation");
    auto split = p;
    for (int i = 0; i < 65; ++i) {
        const auto id = "F" + std::to_string(i);
        Task fixed{id, id, 1, {}};
        fixed.constraintType = ConstraintType::MSO;
        fixed.constraintAt = "2026-11-02 09:00";
        split.tasks.push_back(fixed);
        split.tasks[0].predecessors.push_back(id);
        split.tasks[0].dependencyRules.push_back({id, DependencyType::SS, 0});
    }
    // Extend the already prepared interleaved placement without changing forward priority.
    auto expanded = levelResources(p, "task-order", 260).leveled;
    const auto extraRows = calculatePlan(split);
    expanded.tasks.insert(expanded.tasks.end(), extraRows.tasks.begin() + 3, extraRows.tasks.end());
    calculateLevelingFloat(split, expanded);
    check(expanded.levelingFloatCalculated && expanded.tasks[0].totalFloatMinutes == 660 &&
              expanded.tasks[1].totalFloatMinutes == 540 && expanded.tasks[1].freeFloatMinutes == 60,
          "split repartition with more than 64 fixed peers: " + expanded.levelingFloatReason +
              " A=" + std::to_string(expanded.tasks[0].totalFloatMinutes) +
              " B=" + std::to_string(expanded.tasks[1].totalFloatMinutes));
    auto large = chain(129, 2);
    const auto unsupported = analyze(large);
    check(unsupported.status == CalculationStatus::Valid && !unsupported.levelingFloatCalculated &&
              unsupported.levelingFloatReason.find("128") != std::string::npos &&
              unsupported.resourceOrderLinks.empty(),
          "movable limit did not fail atomically");
    check(std::none_of(unsupported.diagnostics.begin(), unsupported.diagnostics.end(),
                       [](const auto &d) {
                           return d.code == "LEVELING_REPARTITION_SEARCH_POLICY" ||
                                  d.code == "LEVELING_REPARTITION_WITNESS";
                       }),
          "partial proof leaked after limit");
    const auto old = analyze(chain(65, 0), 3);
    check(!old.levelingFloatCalculated && old.levelingFloatReason.find("64") != std::string::npos,
          "legacy 1.3 group limit changed");
    auto archive = chain(65, 0);
    LevelingSelection selection;
    selection.policy = levelingPolicyFor(archive);
    selection.adoptedAt = "2026-11-04T00:00:00Z";
    selection.reason = "연결 묶음 검토";
    selection.sourceDocumentId = archive.documentId;
    selection.sourceRevision = archive.revision;
    archive.planLeveling = selection;
    auto legacy = calculatePlan(archive, "ppm-adopted-leveling-1.3");
    check(legacy.engineVersion == "ppm-adopted-leveling-1.3" && !legacy.levelingFloatCalculated,
          "legacy selected baseline replay");
    LevelingFloatScope legacyBaseline(3);
    auto baseline = prepareBaseline(archive, "기존 기준선", "PM", "보존", "2026-11-04T00:00:00Z");
    LevelingFloatScope current(4);
    baseline.id = "legacy-baseline";
    baseline.version = 1;
    archive.baselines.push_back(baseline);
    ++archive.revision;
    verifyBaseline(baseline);
    const auto reopened = json::decodeProject(json::encodeProject(archive), 14);
    check(reopened.baselines[0] == baseline, "legacy unavailable baseline archive changed");
    const auto now = calculatePlan(reopened);
    check(now.engineVersion == "ppm-adopted-leveling-1.4" && now.levelingFloatCalculated && now.tasks != legacy.tasks,
          "current selected engine did not adopt expanded capability");
    auto result = analyze(chain(65, 2));
    const auto tasks = result.tasks;
    const auto policy = result.calculationPolicy;
    const auto diagnostics = result.diagnostics;
    calculateLevelingFloat(chain(65, 2), result);
    check(result.tasks == tasks && result.calculationPolicy == policy &&
              std::equal(result.diagnostics.begin(), result.diagnostics.end(), diagnostics.begin(), diagnostics.end(),
                         [](const auto &a, const auto &b) {
                             return a.code == b.code && a.arguments == b.arguments && a.taskIds == b.taskIds;
                         }),
          "repeated analysis changed witnesses or accumulated diagnostics");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope scope(control);
        calculateRepartitionLevelingFloat(chain(65, 2), result);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled && result.tasks == tasks, "scaled cancellation changed dates");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QJsonArray cases;
        cases.append(verifyChain(65, 0));
        cases.append(verifyChain(128, 2));
        cases.append(verifyChain(65, 2, 0, false));
        cases.append(verifyChain(256, 2, 192));
        cases.append(verifyChain(1000, 0));
        compatibility();
        QDir().mkpath("scaled-repartition-float-qa");
        QFile file("scaled-repartition-float-qa/metrics.json");
        check(file.open(QIODevice::WriteOnly), "metrics write failed");
        file.write(QJsonDocument(QJsonObject{{"engine", "ppm-adopted-leveling-1.4"}, {"cases", cases}}).toJson());
        std::cout << "Scaled repartition: fixed elimination, 128 movable tasks, iterative search, limits, legacy "
                     "replay passed\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
