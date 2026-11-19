#include "calculation/ProgressPolicies.h"
#include "calculation/WorkCalendar.h"
#include <algorithm>
#include <unordered_map>
namespace ppm {
namespace {
void actualDatesMask(Project &p, std::vector<IgnoredProgressRelation> *historical = nullptr) {
    if (!p.statusAt)
        return;
    std::unordered_map<std::string, const ProgressEntry *> facts;
    for (const auto &entry : p.progress) {
        calculationCheckpoint();
        if (entry.statusAt <= *p.statusAt &&
            (!facts.contains(entry.taskId) || facts.at(entry.taskId)->statusAt <= entry.statusAt))
            facts[entry.taskId] = &entry;
    }
    for (auto &task : p.tasks) {
        calculationCheckpoint();
        const auto *fact = facts.contains(task.id) ? facts.at(task.id) : nullptr;
        if (!fact || !fact->actualStart)
            continue;
        std::vector<std::string> ignored;
        for (const auto &edge : dependencies(task))
            if (fact->actualFinish || !dependencyToFinish(edge.type)) {
                ignored.push_back(edge.predecessorId);
                if (historical)
                    historical->push_back({edge.predecessorId, task.id, edge.type, edge.lagMinutes});
            }
        std::erase_if(task.predecessors,
                      [&](const auto &id) { return std::find(ignored.begin(), ignored.end(), id) != ignored.end(); });
        std::erase_if(task.dependencyRules, [&](const auto &edge) {
            return std::find(ignored.begin(), ignored.end(), edge.predecessorId) != ignored.end();
        });
    }
}
} // namespace
ProgressPolicyComparison compareProgressPolicies(const Project &p, const std::shared_ptr<CalculationControl> &control) {
    EvaluationScope evaluation(control);
    calculationCheckpoint();
    ProgressPolicyComparison out;
    out.documentId = p.documentId;
    out.name = p.name;
    out.revision = p.revision;
    if (!p.statusAt) {
        out.blockingReason = "상태일을 먼저 설정하세요.";
        return out;
    }
    out.statusAt = *p.statusAt;
    std::size_t edges = 0;
    for (const auto &task : p.tasks)
        edges += task.predecessors.size();
    if (p.tasks.size() > 10000 || edges > 100000) {
        out.blockingReason = "진척 정책 비교 한도(10,000 작업 / 100,000 관계)를 초과했습니다.";
        return out;
    }
    out.originalPlan = calculatePlan(p);
    if (out.originalPlan.status != CalculationStatus::Valid) {
        out.blockingReason = "원본 전체 계획이 유효하지 않습니다. 미정/충돌/관계 오류를 먼저 해결하세요.";
        return out;
    }
    auto logical = p;
    logical.levelingDecisions.clear();
    logical.progressPolicy.reset();
    logical.planLeveling.reset();
    logical.remainingLeveling.reset();
    out.retained = calculate(logical);
    std::unordered_map<std::string, const ProgressEntry *> facts;
    for (const auto &entry : p.progress) {
        calculationCheckpoint();
        if (entry.statusAt <= *p.statusAt &&
            (!facts.contains(entry.taskId) || facts.at(entry.taskId)->statusAt <= entry.statusAt))
            facts[entry.taskId] = &entry;
    }
    // The private graph is the explicitly reported active-relationship mask.
    // Reusing the native engine keeps calendars, effort, constraints and the float pass consistent.
    auto overlay = logical;
    for (auto &task : overlay.tasks) {
        calculationCheckpoint();
        if (!facts.contains(task.id) || !facts.at(task.id)->actualStart || facts.at(task.id)->actualFinish)
            continue;
        for (const auto &d : dependencies(task))
            out.ignored.push_back({d.predecessorId, task.id, d.type, d.lagMinutes});
        task.predecessors.clear();
        task.dependencyRules.clear();
    }
    out.overridden = calculate(overlay);
    auto actual = logical;
    actualDatesMask(actual, &out.actualDatesIgnored);
    out.actualDates = calculate(actual);
    if (out.actualDates.status == CalculationStatus::Valid)
        out.actualDates.plannedTasks = out.originalPlan.tasks;
    out.actualDates.calculationPolicy = std::string(actualDatesProgressPolicy) + "/" + p.calculationProfile;
    out.overridden.calculationPolicy = "native-progress-override-preview-v1/" + p.calculationProfile +
                                       (out.overridden.forecastFloatCalculated ? "/remaining-float-v1" : "");
    if (out.overridden.status == CalculationStatus::Valid)
        out.overridden.plannedTasks = out.originalPlan.tasks;
    std::erase_if(out.overridden.diagnostics,
                  [](const auto &d) { return d.code == "FORECAST_POLICY" || d.code == "OUT_OF_SEQUENCE"; });
    for (const auto &d : out.retained.diagnostics)
        if (d.code == "OUT_OF_SEQUENCE") {
            auto fact = d;
            fact.textKey = "PROGRESS_OVERRIDE_OUT_OF_SEQUENCE";
            out.overridden.diagnostics.push_back(std::move(fact));
        }
    out.overridden.diagnostics.push_back({"PROGRESS_OVERRIDE_PREVIEW", {}, {}, "PROGRESS_OVERRIDE_PREVIEW"});
    std::unordered_map<std::string, const TaskSchedule *> retained, overridden, actualRows;
    for (const auto &c : out.actualDates.tasks)
        actualRows.emplace(c.taskId, &c);
    if (out.retained.status == CalculationStatus::Valid)
        for (const auto &row : out.retained.tasks)
            retained.emplace(row.taskId, &row);
    if (out.overridden.status == CalculationStatus::Valid)
        for (const auto &row : out.overridden.tasks)
            overridden.emplace(row.taskId, &row);
    const Task projectCalendarTask;
    WorkCalendar clock(p, projectCalendarTask);
    for (const auto &task : p.tasks) {
        calculationCheckpoint();
        ProgressPolicyTask row;
        row.id = task.id;
        row.name = task.name;
        if (actualRows.contains(task.id))
            row.actualDates = *actualRows.at(task.id);
        if (facts.contains(task.id)) {
            row.actualStart = facts.at(task.id)->actualStart;
            row.actualFinish = facts.at(task.id)->actualFinish;
        }
        if (retained.contains(task.id))
            row.retained = *retained.at(task.id);
        if (overridden.contains(task.id))
            row.overridden = *overridden.at(task.id);
        row.comparable = row.retained && row.overridden;
        if (row.comparable) {
            const auto &a = *row.retained, &b = *row.overridden;
            row.finishDeltaMinutes = clock.offset(b.finish) - clock.offset(a.finish);
            if (a.remainingStart && b.remainingStart)
                row.resumeDeltaMinutes = clock.offset(*b.remainingStart) - clock.offset(*a.remainingStart);
            row.changed = a.start != b.start || a.finish != b.finish || a.remainingStart != b.remainingStart ||
                          a.totalFloatMinutes != b.totalFloatMinutes || a.freeFloatMinutes != b.freeFloatMinutes ||
                          a.critical != b.critical || a.drivingPredecessors != b.drivingPredecessors;
            if (row.changed)
                ++out.changedTasks;
        } else
            ++out.uncomparedTasks;
        out.tasks.push_back(std::move(row));
    }
    out.computed = true;
    return out;
}
Project progressPolicyGraph(const Project &p) {
    auto active = p;
    active.levelingDecisions.clear();
    active.progressPolicy.reset();
    active.planLeveling.reset();
    active.remainingLeveling.reset();
    if (p.statusAt && p.progressPolicy && p.progressPolicy->policy == actualDatesProgressPolicy)
        actualDatesMask(active);
    if (p.statusAt && p.progressPolicy && p.progressPolicy->policy == overrideProgressPolicy)
        for (auto &task : active.tasks) {
            calculationCheckpoint();
            const auto *fact = progressAsOf(p, task.id);
            if (fact && fact->actualStart && !fact->actualFinish) {
                task.predecessors.clear();
                task.dependencyRules.clear();
            }
        }
    return active;
}
ScheduleResult selectedProgressSchedule(const Project &p) {
    auto original = calculatePlan(p); // Validate the complete input before removing the selection.
    if (original.status != CalculationStatus::Valid)
        return original;
    const auto &s = *p.progressPolicy;
    auto logical = p;
    logical.levelingDecisions.clear();
    logical.progressPolicy.reset();
    logical.planLeveling.reset();
    logical.remainingLeveling.reset();
    auto result = original;
    if (p.statusAt) {
        if (s.policy == actualDatesProgressPolicy) {
            auto active = progressPolicyGraph(p);
            result = calculate(active);
            if (result.status == CalculationStatus::Valid)
                result.plannedTasks = original.tasks;
            const auto retained = calculate(logical);
            for (const auto &d : retained.diagnostics)
                if (d.code == "OUT_OF_SEQUENCE")
                    result.diagnostics.push_back(d);
        } else if (s.policy == retainedProgressPolicy)
            result = calculate(logical);
        else {
            auto comparison = compareProgressPolicies(logical);
            if (!comparison.computed) {
                result.status = CalculationStatus::Conflict;
                result.tasks.clear();
                result.projectFinish.reset();
                result.diagnostics.push_back({"PROGRESS_POLICY_INVALID", {}, {}, "PROGRESS_POLICY_INVALID"});
                return result;
            }
            result = std::move(comparison.overridden);
            std::erase_if(result.diagnostics, [](const auto &d) { return d.code == "PROGRESS_OVERRIDE_PREVIEW"; });
        }
    }
    result.calculationPolicy =
        "adopted-progress-v1/" + s.policy + "/" + p.calculationProfile +
        (p.statusAt ? (result.forecastFloatCalculated ? "/remaining-float-v1" : "") : "/dormant");
    result.diagnostics.push_back(
        {"PROGRESS_POLICY_SELECTED", {s.policy, s.adoptedBy, s.reason, s.adoptedAt}, {}, "PROGRESS_POLICY_SELECTED"});
    return result;
}
} // namespace ppm
