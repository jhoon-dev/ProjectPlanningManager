#include "domain/ResourcePlan.h"
#include "domain/MaterialActuals.h"
#include "calculation/MaterialInventory.h"
#include "calculation/MaterialReservations.h"
#include "calculation/CapacityTimeline.h"
#include "domain/Diagnostics.h"
#include "calculation/Resources.h"
#include "calculation/LevelingFloat.h"
#include "calculation/WorkCalendar.h"
#include "calculation/PdmResources.h"
#include "calculation/Effort.h"
#include "calculation/EnginePolicy.h"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <stdexcept>

namespace ppm {
std::vector<ResourceOccupancy> resourceOccupancies(const Project &project, const ScheduleResult &result,
                                                   std::size_t limit, const std::set<std::string> *includedResources) {
    EvaluationScope evaluation;
    if (result.status != CalculationStatus::Valid || result.documentId != project.documentId ||
        result.inputRevision != project.revision || result.profile != project.calculationProfile ||
        (result.isForecast && result.statusAt != project.statusAt))
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    std::unordered_map<std::string, const TaskSchedule *> rows;
    for (const auto &row : result.tasks)
        rows.emplace(row.taskId, &row);
    if (rows.size() != project.tasks.size())
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    std::unordered_map<std::string, std::vector<const Assignment *>> assigned;
    for (const auto &assignment : project.assignments)
        if (!includedResources || includedResources->contains(assignment.resourceId))
            assigned[assignment.taskId].push_back(&assignment);
    ExecutionInputs inputs(project);
    WorkCalendar clock(project);
    std::vector<ResourceOccupancy> out;
    const auto append = [&](const std::string &resource, const std::string &task, const std::string &start,
                            const std::string &finish, int units) {
        calculationCheckpoint();
        if (includedResources && !includedResources->contains(resource))
            return;
        if (start >= finish)
            return;
        if (out.size() >= limit)
            throw AppError(ErrorCode::WORKSPACE_LIMIT);
        out.push_back({resource, task, start, finish, units});
    };
    for (const auto &task : project.tasks) {
        calculationCheckpoint();
        if (!assigned.contains(task.id))
            continue;
        const auto found = rows.find(task.id);
        if (found == rows.end())
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        const auto &row = *found->second;
        if (result.isForecast && row.completedActual)
            continue;
        if (isEffort(project)) {
            const auto remaining = result.isForecast ? pendingTask(project, task) : task;
            for (const auto &span : TaskExecution(project, remaining, &inputs).spans(row))
                append(span.resourceId, task.id, clock.boundary(span.start, false), clock.boundary(span.finish, true),
                       span.units);
        } else {
            const auto start = result.isForecast ? row.remainingStart.value_or(row.start) : row.start;
            WorkCalendar calendar(project, task);
            for (const auto &[begin, end] : calendar.segments(start, row.finish))
                for (const auto *assignment : assigned.at(task.id))
                    append(assignment->resourceId, task.id, begin, end, assignment->units);
        }
    }
    return out;
}
namespace {
using Minute = std::int64_t;
using Events = std::map<Minute, Minute>;
struct Span {
    Minute start, finish;
    std::string taskId;
    int units;
};
std::pair<Minute, Minute> span(const TaskSchedule &row, const ScheduleResult &result, const WorkCalendar &calendar) {
    if (result.isForecast && row.completedActual)
        return {0, 0};
    return {result.isForecast ? calendar.offset(row.remainingStart.value_or(row.start)) : row.earlyStartMinutes,
            row.earlyFinishMinutes};
}
void reserve(Events &events, Minute start, Minute finish, int units) {
    if (finish <= start)
        return;
    events[start] += units;
    events[finish] -= units;
}
// Visit every interval where either demand or daily capacity can change.
template <class F>
void intervals(const Events &events, Minute start, Minute finish, F visit, int dayMinutes = 480,
               const CapacityTimeline *capacity = nullptr) {
    Minute used = 0;
    auto it = events.begin();
    while (it != events.end() && it->first <= start) {
        used += it->second;
        ++it;
    }
    for (auto at = start; at < finish;) {
        auto end = std::min(finish, (at / dayMinutes + 1) * dayMinutes);
        if (it != events.end())
            end = std::min(end, it->first);
        if (capacity)
            end = capacity->next(at, end);
        if (!visit(at, end, used))
            return;
        at = end;
        while (it != events.end() && it->first <= at) {
            used += it->second;
            ++it;
        }
    }
}
} // namespace
ResourceLoad resourceLoad(const Project &p, const ScheduleResult &result) {
    EvaluationScope evaluation;
    ResourceLoad load;
    load.totalTasks = p.tasks.size();
    if (result.documentId != p.documentId || result.inputRevision != p.revision || result.statusAt != p.statusAt ||
        result.profile != p.calculationProfile || result.status != CalculationStatus::Valid) {
        load.diagnostics.push_back({"NO_CURRENT_SCHEDULE", {}, {}, "NO_CURRENT_SCHEDULE"});
        return load;
    }
    WorkCalendar calendar(p);
    std::unordered_map<std::string, const TaskSchedule *> rows;
    for (const auto &r : result.tasks)
        rows.emplace(r.taskId, &r);
    std::set<std::string> assigned;
    std::unordered_map<std::string, std::vector<Span>> spans;
    std::map<std::string, WorkCalendar> calendars;
    if (isEffort(p)) {
        ExecutionInputs inputs(p);
        for (const auto &a : p.assignments)
            assigned.insert(a.taskId);
        for (const auto &t : p.tasks) {
            if (!assigned.contains(t.id))
                continue;
            const auto pending = result.isForecast ? pendingTask(p, t) : t;
            for (const auto &a : TaskExecution(p, pending, &inputs).spans(*rows.at(t.id)))
                spans[a.resourceId].push_back({a.start, a.finish, t.id, a.units});
        }
    }
    for (const auto &a : p.assignments) {
        if (isEffort(p))
            continue;
        assigned.insert(a.taskId);
        const auto [s, e] = span(*rows.at(a.taskId), result, calendar);
        if (e > s) {
            if (isPdm(p)) {
                const auto task =
                    std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &t) { return t.id == a.taskId; });
                if (!calendars.contains(task->calendarId))
                    calendars.emplace(task->calendarId, WorkCalendar(p, *task));
                for (const auto &[begin, end] :
                     calendars.at(task->calendarId).segments(calendar.boundary(s, false), calendar.boundary(e, true)))
                    spans[a.resourceId].push_back({calendar.offset(begin), calendar.offset(end), a.taskId, a.units});
            } else
                spans[a.resourceId].push_back({s, e, a.taskId, a.units});
        }
    }
    load.assignedTasks = assigned.size();
    load.status = CalculationStatus::Valid;
    for (const auto &r : p.resources) {
        Events events;
        for (const auto &s : spans[r.id])
            reserve(events, s.start, s.finish, s.units);
        if (events.empty())
            continue;
        CapacityTimeline capacity(r, calendar);
        std::map<Minute, ResourceDay> days;
        intervals(
            events, events.begin()->first, events.rbegin()->first,
            [&](Minute s, Minute e, Minute used) {
                if (!used)
                    return true;
                auto day = s / calendar.dayMinutes();
                auto &d = days[day];
                const auto cap = capacity.at(s);
                if (d.resourceId.empty())
                    d.capacity = cap;
                else if (d.capacity != cap)
                    d.capacityVaries = true;
                if (!cap)
                    d.capacity.reset();
                else if (d.capacity)
                    d.capacity = std::min(*d.capacity, *cap);
                d.resourceId = r.id;
                d.date = calendar.boundary(day * calendar.dayMinutes(), false).substr(0, 10);
                d.peakUnits = std::max(d.peakUnits, used);
                d.demandUnitMinutes += used * (e - s);
                if (!cap)
                    load.status = CalculationStatus::Incomplete;
                else if (used > *cap) {
                    d.overloadMinutes += e - s;
                    d.excessUnitMinutes += (used - *cap) * (e - s);
                }
                return true;
            },
            calendar.dayMinutes(), &capacity);
        for (auto &[day, d] : days) {
            for (const auto &s : spans[r.id])
                if (s.start < (day + 1) * calendar.dayMinutes() && s.finish > day * calendar.dayMinutes())
                    d.taskIds.push_back(s.taskId);
            std::sort(d.taskIds.begin(), d.taskIds.end());
            d.taskIds.erase(std::unique(d.taskIds.begin(), d.taskIds.end()), d.taskIds.end());
            load.days.push_back(std::move(d));
        }
    }
    const bool unknown = load.status == CalculationStatus::Incomplete;
    const bool overload =
        std::any_of(load.days.begin(), load.days.end(), [](const auto &d) { return d.overloadMinutes > 0; });
    if (overload && !unknown)
        load.status = CalculationStatus::Conflict;
    if (unknown)
        load.diagnostics.push_back({"UNKNOWN_CAPACITY", {}, {}, "UNKNOWN_CAPACITY"});
    if (overload)
        load.diagnostics.push_back({"RESOURCE_OVERLOAD", {}, {}, "RESOURCE_OVERLOAD"});
    if (assigned.size() < p.tasks.size())
        load.diagnostics.push_back({"UNASSIGNED_TASKS", {}, {}, "UNASSIGNED_TASKS"});
    return load;
}
static LevelingResult levelResourcesImpl(const Project &p, const std::string &priority, int horizonWorkDays,
                                         const std::string &policy) {
    if (p.planLeveling || p.remainingLeveling) {
        auto logical = p;
        logical.levelingDecisions.clear();
        logical.planLeveling.reset();
        logical.remainingLeveling.reset();
        return levelResourcesImpl(logical, priority, horizonWorkDays, policy);
    }
    if (!policy.empty()) {
        if (!supportsLevelingPolicy(p, policy))
            throw AppError(ErrorCode::SCENARIO_METADATA_INVALID, {});
        EnginePolicyScope scope(isPdm(p) && policy == legacyLevelingPolicyFor(p));
        return levelResourcesImpl(p, priority, horizonWorkDays, {});
    }
    if ((priority != "task-order" && priority != "shortest-first" && priority != "longest-first" &&
         !advancedPriority(priority)) ||
        horizonWorkDays < 1 || horizonWorkDays > 5200)
        throw AppError(ErrorCode::LEVEL_OPTIONS_INVALID);
    if (advancedPriority(priority)) {
        if (horizonWorkDays < 1 || horizonWorkDays > 5200)
            throw AppError(ErrorCode::LEVEL_OPTIONS_INVALID);
        auto base = calculate(p);
        LevelingResult failed;
        failed.original = base;
        failed.leveled = base;
        failed.priority = priority;
        failed.horizonWorkDays = horizonWorkDays;
        if (base.status != CalculationStatus::Valid)
            return failed;
        std::map<std::string, std::int64_t> floats;
        for (const auto &r : base.tasks) {
            if (priority == "least-float-first" && !hasScheduleFloat(base, r) && !r.completedActual) {
                failed.leveled.status = CalculationStatus::Incomplete;
                failed.leveled.tasks.clear();
                failed.leveled.projectFinish.reset();
                failed.diagnostics.push_back(
                    {"WORKSPACE_INVALID", {"여유 우선순위에 필요한 여유가 미산정입니다."}, {}, "WORKSPACE_INVALID"});
                return failed;
            }
            floats[r.taskId] = r.totalFloatMinutes;
        }
        auto ordered = p;
        std::stable_sort(ordered.tasks.begin(), ordered.tasks.end(), [&](const auto &a, const auto &b) {
            if (priority == "least-float-first")
                return floats.at(a.id) < floats.at(b.id);
            return a.deadlineAt.value_or("9999-12-31 23:59") < b.deadlineAt.value_or("9999-12-31 23:59");
        });
        auto result = levelResourcesImpl(ordered, "task-order", horizonWorkDays, {});
        auto restore = [&](auto &rows) {
            std::map<std::string, TaskSchedule> byId;
            for (const auto &r : rows)
                byId.emplace(r.taskId, r);
            rows.clear();
            for (const auto &t : p.tasks)
                if (byId.contains(t.id))
                    rows.push_back(byId.at(t.id));
        };
        restore(result.original.tasks);
        restore(result.leveled.tasks);
        restore(result.leveled.plannedTasks);
        result.original = std::move(base);
        result.priority = priority;
        result.leveled.calculationPolicy = "native-priority-v1/" + priority + "/" + result.leveled.calculationPolicy;
        return result;
    }
    const auto stock = (p.resourcePlan.autoDelayForReceipts || p.resourcePlan.autoDelayRemainingForReceipts)
                           ? materialAutoReadiness(p)
                           : materialReadiness(p);
    if (!stock.empty()) {
        LevelingResult out;
        out.original = calculate(p);
        out.leveled = out.original;
        out.priority = priority;
        out.horizonWorkDays = horizonWorkDays;
        out.leveled.status = CalculationStatus::Conflict;
        out.leveled.tasks.clear();
        out.leveled.projectFinish.reset();
        out.diagnostics = stock;
        out.leveled.diagnostics.insert(out.leveled.diagnostics.end(), stock.begin(), stock.end());
        return out;
    }
    if ((priority != "task-order" && priority != "shortest-first" && priority != "longest-first") ||
        horizonWorkDays < 1 || horizonWorkDays > 5200)
        throw AppError(ErrorCode::LEVEL_OPTIONS_INVALID, {});
    if (p.progressPolicy)
        return levelProgressPolicy(p, priority, horizonWorkDays);
    if (isEffort(p))
        return levelEffort(p, priority, horizonWorkDays);
    if (isPdm(p))
        return levelPdmResources(p, priority, horizonWorkDays);
    LevelingResult out;
    out.priority = priority;
    out.horizonWorkDays = horizonWorkDays;
    out.original = calculate(p);
    out.before = resourceLoad(p, out.original);
    out.leveled = out.original;
    out.leveled.calculationPolicy = "serial-no-split-v1/" + priority;
    out.leveled.forecastFloatCalculated = false;
    if (out.original.status != CalculationStatus::Valid)
        return out;
    WorkCalendar calendar(p);
    MaterialReservations materials(p, true, out.original.isForecast);
    const Minute horizon = Minute(horizonWorkDays) * 480;
    std::unordered_map<std::string, std::size_t> indexes;
    std::unordered_map<std::string, const Resource *> resources;
    std::unordered_map<std::string, std::vector<const Assignment *>> assignments;
    for (std::size_t i = 0; i < p.tasks.size(); ++i)
        indexes.emplace(p.tasks[i].id, i);
    for (const auto &r : p.resources)
        resources.emplace(r.id, &r);
    for (const auto &a : p.assignments)
        assignments[a.taskId].push_back(&a);
    std::unordered_map<std::string, Events> reserved;
    std::map<std::string, CapacityTimeline> capacities;
    for (const auto &r : p.resources)
        capacities.emplace(r.id, CapacityTimeline(r, calendar));
    std::vector<bool> done(p.tasks.size(), false);
    std::vector<TaskSchedule> rows = out.original.tasks;
    // Engine output uses input order; look up IDs explicitly at this boundary.
    for (const auto &r : out.original.tasks)
        rows[indexes.at(r.taskId)] = r;
    std::size_t count = 0;
    while (count < p.tasks.size()) {
        std::optional<std::size_t> chosen;
        for (std::size_t i = 0; i < p.tasks.size(); ++i) {
            if (done[i])
                continue;
            bool ready = true;
            for (const auto &pred : p.tasks[i].predecessors)
                if (!done[indexes.at(pred)])
                    ready = false;
            if (!ready)
                continue;
            auto length = [&](std::size_t n) {
                const auto [s, e] = span(rows[n], out.original, calendar);
                return e - s;
            };
            if (!chosen || (priority == "shortest-first" && length(i) < length(*chosen)) ||
                (priority == "longest-first" && length(i) > length(*chosen)))
                chosen = i;
        }
        if (!chosen)
            throw AppError(ErrorCode::LEVEL_DEPENDENCY_CYCLE, {}, {}, "LEVEL_DEPENDENCY_CYCLE_2");
        auto i = *chosen;
        auto &row = rows[i];
        const auto &task = p.tasks[i];
        if (row.completedActual) {
            done[i] = true;
            ++count;
            continue;
        }
        auto [start, finish] = span(row, out.original, calendar);
        const Minute duration = finish - start, originalStart = start;
        std::vector<std::string> causes{task.id};
        for (const auto &pred : task.predecessors) {
            if (rows[indexes.at(pred)].earlyFinishMinutes > originalStart)
                causes.push_back(pred);
            start = std::max(start, rows[indexes.at(pred)].earlyFinishMinutes);
        }
        std::map<std::string, Diagnostic> blocked;
        auto explain = [&](Diagnostic diagnostic) {
            for (const auto &[id, issue] : blocked) {
                diagnostic.resourceIds.push_back(id);
                diagnostic.causes.push_back(issue);
            }
            return diagnostic;
        };
        bool unknown = false, found = false;
        try {
            while (start + duration <= horizon) {
                calendar.boundary(start + duration, duration > 0);
                Minute next = start;
                for (const auto *a : assignments[task.id]) {
                    const auto &r = *resources.at(a->resourceId);
                    const auto &capacity = capacities.at(r.id);
                    intervals(
                        reserved[r.id], start, start + duration,
                        [&](Minute s, Minute, Minute used) {
                            auto cap = capacity.at(s);
                            if (!cap)
                                unknown = true;
                            if (!cap || used + a->units > *cap) {
                                if (!blocked.contains(r.id))
                                    blocked.emplace(r.id, Diagnostic{"RESOURCE_WAIT",
                                                                     {r.name, r.id, calendar.boundary(s, false),
                                                                      std::to_string(used + a->units),
                                                                      cap ? std::to_string(*cap) : "?"},
                                                                     {task.id}});
                                // The demand/capacity stays infeasible until its next real change,
                                // even when the trial task finishes earlier. Avoid one-minute retries.
                                Minute change = capacity.next(s, (s / 480 + 1) * 480);
                                if (cap && a->units <= *cap) {
                                    auto event = reserved[r.id].upper_bound(s);
                                    if (event != reserved[r.id].end())
                                        change = std::min(change, event->first);
                                }
                                next = std::max(next, change);
                                return false;
                            }
                            return true;
                        },
                        calendar.dayMinutes(), &capacity);
                }
                if (next == start) {
                    auto trial = row;
                    trial.start = calendar.boundary(start, false);
                    trial.finish = calendar.boundary(start + duration, duration > 0);
                    if (out.original.isForecast)
                        trial.remainingStart = calendar.boundary(start, false);
                    trial.earlyStartMinutes = start;
                    trial.earlyFinishMinutes = start + duration;
                    if (p.resourcePlan.autoDelayForReceipts && trial.finish > calendar.boundary(horizon, true))
                        break;
                    if (!materials.fits(trial)) {
                        const auto bound = materials.nextStart(trial);
                        next = bound ? std::max(start + 1, calendar.startAtLeast(*bound)) : start + 1;
                    }
                }
                if (next == start) {
                    found = true;
                    break;
                }
                start = next;
            }
        } catch (const AppError &error) {
            if (p.resourcePlan.autoDelayForReceipts || p.resourcePlan.autoDelayRemainingForReceipts) {
                out.leveled.status = CalculationStatus::Conflict;
                out.leveled.tasks.clear();
                out.leveled.projectFinish.reset();
                out.diagnostics.push_back(error.diagnostic());
                return out;
            }
            found = false;
        } catch (const std::exception &) {
            found = false;
        }
        if (!found) {
            out.leveled.status = unknown ? CalculationStatus::Incomplete : CalculationStatus::Conflict;
            out.leveled.tasks.clear();
            out.leveled.projectFinish.reset();
            out.diagnostics.push_back(
                explain({unknown ? "LEVEL_UNKNOWN_CAPACITY" : "LEVEL_HORIZON_EXHAUSTED", {}, causes}));
            return out;
        }
        finish = start + duration;
        for (const auto *a : assignments[task.id])
            reserve(reserved[a->resourceId], start, finish, a->units);
        if (start != originalStart) {
            if (!out.original.isForecast || !progressAsOf(p, task.id) || !progressAsOf(p, task.id)->actualStart)
                row.start = calendar.boundary(start, false);
            row.finish = calendar.boundary(finish, duration > 0);
            if (out.original.isForecast)
                row.remainingStart = calendar.boundary(start, false);
            out.diagnostics.push_back(
                explain({"RESOURCE_DELAY", {std::to_string(start - originalStart)}, causes, "RESOURCE_DELAY_3"}));
        }
        row.earlyStartMinutes =
            out.original.isForecast && progressAsOf(p, task.id) && progressAsOf(p, task.id)->actualStart
                ? row.earlyStartMinutes
                : start;
        row.earlyFinishMinutes = finish;
        row.lateStartMinutes = row.lateFinishMinutes = row.totalFloatMinutes = row.freeFloatMinutes = 0;
        row.critical = false;
        row.drivingPredecessors.clear();
        materials.commit(row);
        if (auto delay = materials.delay(row))
            out.diagnostics.push_back(*delay);
        done[i] = true;
        ++count;
    }
    out.leveled.tasks = std::move(rows);
    if (!out.leveled.tasks.empty()) {
        out.leveled.projectFinish =
            std::max_element(out.leveled.tasks.begin(), out.leveled.tasks.end(), [](const auto &a, const auto &b) {
                return a.finish < b.finish;
            })->finish;
    }
    out.after = resourceLoad(p, out.leveled);
    return out;
}
LevelingResult levelResources(const Project &p, const std::string &priority, int horizonWorkDays,
                              const std::string &policy) {
    EnginePolicyScope scope(
        (p.resourcePlan.autoDelayForReceipts || (p.statusAt && p.resourcePlan.autoDelayRemainingForReceipts))
            ? false
            : (policy.empty() ? legacyMilestones() : isPdm(p) && policy == legacyLevelingPolicyFor(p)));
    auto result = levelResourcesImpl(p, priority, horizonWorkDays, policy);
    if (result.leveled.status == CalculationStatus::Valid) {
        const auto issues = materialInventoryDiagnostics(p, result.leveled);
        if (!issues.empty()) {
            result.leveled.status = CalculationStatus::Conflict;
            result.leveled.tasks.clear();
            result.leveled.projectFinish.reset();
            result.after = {};
            result.diagnostics.insert(result.diagnostics.end(), issues.begin(), issues.end());
            result.leveled.diagnostics.insert(result.leveled.diagnostics.end(), issues.begin(), issues.end());
        }
    }
    result.leveled.resourceLeveled = true;
    if (p.statusAt && p.resourcePlan.autoDelayRemainingForReceipts)
        result.leveled.calculationPolicy = std::string(remainingMaterialPolicy) + "/" + priority;
    else if (p.resourcePlan.autoDelayForReceipts)
        result.leveled.calculationPolicy = std::string(materialArrivalPolicy) + "/" + priority;
    if (hasMaterialInventory(p)) {
        if (levelingFloatVersion() >= 9 && result.leveled.status == CalculationStatus::Valid &&
            result.after.status == CalculationStatus::Valid)
            calculateLevelingFloat(p, result.leveled);
        else {
            result.leveled.levelingFloatCalculated = false;
            result.leveled.levelingFloatReason = "입고 시점의 자재 재고를 포함한 자원 여유는 미산정입니다.";
        }
    } else if (result.leveled.status == CalculationStatus::Valid && result.after.status == CalculationStatus::Valid)
        calculateLevelingFloat(p, result.leveled);
    return result;
}
Project scenarioProject(const LevelingScenario &s) {
    Project p;
    static_cast<PlanInput &>(p) = s.input;
    p.documentId = s.sourceDocumentId;
    p.revision = s.inputRevision;
    p.statusAt = s.statusAt;
    p.progress = s.progress;
    p.remainingWork = s.remainingWork;
    p.materialActuals = s.materialActuals;
    p.progressPolicy = s.progressPolicy;
    validate(p);
    return p;
}
} // namespace ppm
