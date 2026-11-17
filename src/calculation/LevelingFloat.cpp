#include "domain/ResourceAvailability.h"
#include "domain/ResourcePlan.h"
#include "calculation/LevelingFloat.h"
#include "calculation/ProgressPolicies.h"
#include "calculation/ForecastFloat.h"
#include "calculation/Constraints.h"
#include "calculation/Effort.h"
#include "calculation/Pdm.h"
#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
namespace jobs {
namespace {
thread_local int version = 9;
struct Unavailable {
    std::string reason;
    bool segmentCandidate = false;
};
using Minute = std::int64_t;
struct Edge {
    std::size_t from, to;
    Dependency rule;
    bool resource = false;
    std::size_t link = 0;
};
struct Event {
    std::string at;
    int kind;
    std::size_t task;
    int units;
}; // finish, capacity, start
struct Lot {
    int units;
    std::optional<std::size_t> owner;
};
void run(const Project &source, ScheduleResult &out) {
    auto p = progressPolicyGraph(source);
    const auto n = p.tasks.size();
    if (n > 10000)
        throw Unavailable{"작업 한도 10,000개 초과"};
    std::unordered_map<std::string, std::size_t> ids;
    for (std::size_t i = 0; i < n; ++i)
        ids.emplace(p.tasks[i].id, i);
    // Engine rows are addressed explicitly by ID, never by an external row order.
    std::vector<TaskSchedule> early(n);
    std::vector<bool> found(n);
    for (const auto &r : out.tasks) {
        if (!ids.contains(r.taskId))
            throw Unavailable{"작업 ID 불일치"};
        const auto i = ids.at(r.taskId);
        if (found[i])
            throw Unavailable{"중복 작업 결과"};
        early[i] = r;
        found[i] = true;
    }
    if (std::find(found.begin(), found.end(), false) != found.end())
        throw Unavailable{"누락 작업 결과"};
    WorkCalendar clock(p);
    ExecutionInputs inputs(p);
    std::vector<std::unique_ptr<WorkCalendar>> calendars;
    std::vector<std::unique_ptr<TaskExecution>> executions;
    std::vector<RemainingExecution> models;
    std::vector<Minute> first(n), last(n);
    std::vector<std::optional<std::string>> fences(n);
    const auto resume = [](const TaskSchedule &r) -> const std::string & {
        return r.remainingStart ? *r.remainingStart : r.start;
    };
    for (std::size_t i = 0; i < n; ++i) {
        calculationCheckpoint();
        const auto &t = p.tasks[i];
        if (isEffort(p)) {
            executions.push_back(
                std::make_unique<TaskExecution>(p, out.isForecast ? pendingTask(p, t) : t, &inputs, out.isForecast));
            const auto *m = executions.back().get();
            models.push_back({&m->calendar(), [m](Minute v) { return m->dates(v); },
                              [m](const auto &v) { return m->offset(v); },
                              [m](const auto &v, bool f) { return m->required(v, f); }});
        } else {
            calendars.push_back(std::make_unique<WorkCalendar>(p, t));
            const auto *cal = calendars.back().get();
            const auto *fact = out.isForecast ? progressAsOf(p, t.id) : nullptr;
            const auto duration = fact ? fact->remainingMinutes : t.durationMinutes.value_or(0);
            models.push_back(
                {cal,
                 [&, cal, duration](Minute v) {
                     TaskSchedule r;
                     pdmDates(clock, *cal, r, v, duration);
                     return r;
                 },
                 [cal, duration](const auto &v) { return duration ? cal->offset(v) : cal->pointOffset(v); },
                 [cal, duration](const auto &v, bool f) {
                     return duration ? (f ? cal->finishAtLeast(v) - duration : cal->startAtLeast(v))
                                     : (f ? cal->pointEndAtMost(v) : cal->pointStartAtLeast(v));
                 }});
        }
        first[i] = models[i].offset(resume(early[i]));
    }
    std::vector<Edge> edges;
    const auto add = [&](Edge e) {
        if (edges.size() >= 250000)
            throw Unavailable{"관계 한도 250,000개 초과"};
        edges.push_back(std::move(e));
    };
    for (std::size_t i = 0; i < n; ++i)
        if (!early[i].completedActual)
            for (const auto &d : dependencies(p.tasks[i]))
                add({ids.at(d.predecessorId), i, d, false});
    std::set<std::pair<std::string, std::string>> used;
    for (const auto &s : resourceOccupancies(p, out))
        used.emplace(s.resourceId, s.taskId);
    std::map<std::string, std::vector<Event>> events;
    for (const auto &a : p.assignments) {
        const auto i = ids.at(a.taskId);
        if (early[i].completedActual || !used.contains({a.resourceId, a.taskId}))
            continue;
        events[a.resourceId].push_back({resume(early[i]), 2, i, a.units});
        events[a.resourceId].push_back({early[i].finish, 0, i, a.units});
    }
    std::vector<ResourceOrderLink> links;
    for (const auto &r : p.resources) {
        auto &list = events[r.id];
        if (list.empty())
            continue;
        for (const auto &d : r.exceptions) {
            if (!d.units)
                throw Unavailable{"자원 가용량 미정: " + r.id};
            list.push_back({d.date + " 00:00", 1, 0, *d.units});
            const auto tomorrow = std::chrono::sys_days{std::chrono::year{std::stoi(d.date.substr(0, 4))} /
                                                        std::chrono::month{unsigned(std::stoi(d.date.substr(5, 2)))} /
                                                        std::chrono::day{unsigned(std::stoi(d.date.substr(8, 2)))}} +
                                  std::chrono::days{1};
            const auto ymd = std::chrono::year_month_day{tomorrow};
            char date[32];
            std::snprintf(date, sizeof(date), "%04d-%02u-%02u 00:00", int(ymd.year()), unsigned(ymd.month()),
                          unsigned(ymd.day()));
            auto restored = r.maxUnits;
            for (const auto &next : r.exceptions)
                if (next.date == std::string(date).substr(0, 10))
                    restored = next.units;
            if (!restored)
                throw Unavailable{"자원 가용량 미정: " + r.id};
            list.push_back({date, 1, 0, *restored});
        }
        if (!r.maxUnits)
            throw Unavailable{"기본 자원 가용량 미정: " + r.id};
        std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
            return std::tie(a.at, a.kind, a.task) < std::tie(b.at, b.kind, b.task);
        });
        int capacity = *r.maxUnits, busy = 0;
        std::deque<Lot> free{{capacity, {}}};
        for (const auto &e : list) {
            calculationCheckpoint();
            if (e.kind == 0) {
                busy -= e.units;
                free.push_back({e.units, e.task});
                continue;
            }
            if (e.kind == 1) {
                if (busy > e.units)
                    throw Unavailable{"분할/병렬 배정의 전체 실행 구간을 고정할 수 없음: " + r.id, true};
                if (e.units > capacity)
                    free.push_back({e.units - capacity, {}});
                auto remove = std::max(0, capacity - e.units);
                while (remove && !free.empty()) {
                    auto &lot = free.front();
                    const auto count = std::min(remove, lot.units);
                    if (count && lot.owner) {
                        auto &limit = fences[*lot.owner];
                        if (!limit || e.at < *limit)
                            limit = e.at;
                    }
                    lot.units -= count;
                    remove -= count;
                    if (!lot.units)
                        free.pop_front();
                }
                if (remove)
                    throw Unavailable{"자원 회수 흐름 불일치"};
                capacity = e.units;
                continue;
            }
            auto need = e.units;
            if (busy + need > capacity)
                throw Unavailable{"분할/병렬 배정의 전체 실행 구간을 고정할 수 없음: " + r.id, true};
            while (need && !free.empty()) {
                auto &lot = free.front();
                const auto count = std::min(need, lot.units);
                if (count && lot.owner && *lot.owner != e.task) {
                    add({*lot.owner, e.task, {p.tasks[*lot.owner].id, DependencyType::FS, 0}, true, links.size()});
                    links.push_back({p.tasks[*lot.owner].id, p.tasks[e.task].id, r.id, count, false});
                }
                lot.units -= count;
                need -= count;
                if (!lot.units)
                    free.pop_front();
            }
            if (need)
                throw Unavailable{"자원 예약 흐름 불일치"};
            busy += e.units;
        }
    }
    std::vector<std::vector<std::size_t>> successors(n), predecessors(n);
    for (std::size_t e = 0; e < edges.size(); ++e) {
        successors[edges[e].from].push_back(e);
        predecessors[edges[e].to].push_back(edges[e].from);
    }
    auto dates = [&](std::size_t i, Minute v) {
        if (v == first[i])
            return early[i];
        auto r = models[i].dates(v);
        r.taskId = p.tasks[i].id;
        if (out.isForecast)
            r.remainingStart = r.start;
        if (out.isForecast)
            if (const auto *fact = progressAsOf(p, p.tasks[i].id); fact && fact->actualStart)
                r.start = early[i].start;
        return r;
    };
    std::size_t checks = 0;
    auto fits = [&](std::size_t i, const TaskSchedule &trial, const std::vector<TaskSchedule> &targets,
                    bool relations) {
        calculationCheckpoint();
        if (++checks > 2000000)
            throw Unavailable{"후진 계산 한도 2,000,000회 초과"};
        if (trial.finish > *out.projectFinish || (fences[i] && trial.finish > *fences[i]) ||
            (hasConstraints(p) && timingViolation(p.tasks[i], trial)))
            return false;
        if (relations)
            for (const auto e : successors[i]) {
                const auto &edge = edges[e];
                const auto &target = targets[edge.to];
                if (target.completedActual)
                    continue;
                const auto bound =
                    edge.resource
                        ? trial.finish
                        : models[edge.to].calendar->shift(
                              dependencyFromFinish(edge.rule.type) ? trial.finish : trial.start, edge.rule.lagMinutes);
                if (bound > (edge.resource || !dependencyToFinish(edge.rule.type) ? resume(target) : target.finish))
                    return false;
            }
        return true;
    };
    auto latest = [&](std::size_t i, Minute upper, const auto &targets, bool relations) {
        auto lo = first[i], hi = std::max(lo, upper);
        if (!fits(i, early[i], targets, relations))
            throw Unavailable{"현재 배치와 고정 자원 순서/제약이 일치하지 않음"};
        while (lo < hi) {
            const auto mid = lo + (hi - lo + 1) / 2;
            bool feasible = false;
            try {
                feasible = fits(i, dates(i, mid), targets, relations);
            } catch (const CalculationCancelled &) {
                throw;
            } catch (const Unavailable &) {
                throw;
            } catch (const std::exception &) {
            }
            if (feasible)
                lo = mid;
            else
                hi = mid - 1;
        }
        return lo;
    };
    auto late = early;
    for (std::size_t i = 0; i < n; ++i)
        if (!early[i].completedActual) {
            last[i] = latest(i, models[i].offset(*out.projectFinish), late, false);
            late[i] = dates(i, last[i]);
        }
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return early[a].finish > early[b].finish; });
    std::deque<std::size_t> queue(order.begin(), order.end());
    std::vector<bool> queued(n, true);
    while (!queue.empty()) {
        const auto i = queue.front();
        queue.pop_front();
        queued[i] = false;
        if (early[i].completedActual)
            continue;
        const auto value = latest(i, last[i], late, true);
        if (value < last[i]) {
            last[i] = value;
            late[i] = dates(i, value);
            for (auto j : predecessors[i])
                if (!queued[j]) {
                    queue.push_back(j);
                    queued[j] = true;
                }
        }
    }
    auto lateCheck = out;
    lateCheck.tasks = late;
    if (resourceLoad(p, lateCheck).status != CalculationStatus::Valid)
        throw Unavailable{"최후 배치의 자원 가용량 검증 실패"};
    auto result = early;
    for (std::size_t i = 0; i < n; ++i)
        if (!early[i].completedActual) {
            auto &r = result[i];
            const auto free = latest(i, last[i], early, true);
            r.totalFloatMinutes =
                models[i].calendar->offset(resume(late[i])) - models[i].calendar->offset(resume(early[i]));
            const auto freeDates = dates(i, free);
            r.freeFloatMinutes =
                models[i].calendar->offset(resume(freeDates)) - models[i].calendar->offset(resume(early[i]));
            r.lateStartMinutes = clock.offset(resume(late[i]));
            r.lateFinishMinutes = clock.offset(late[i].finish);
            r.critical = r.totalFloatMinutes == 0;
            r.drivingPredecessors.clear();
        }
    for (const auto &edge : edges) {
        const auto &prev = early[edge.from];
        const auto &next = early[edge.to];
        if (prev.completedActual || next.completedActual)
            continue;
        const auto bound =
            edge.resource ? prev.finish
                          : models[edge.to].calendar->shift(
                                dependencyFromFinish(edge.rule.type) ? prev.finish : prev.start, edge.rule.lagMinutes);
        const bool binding = models[edge.to].required(
                                 bound, edge.resource ? false : dependencyToFinish(edge.rule.type)) == first[edge.to];
        if (edge.resource) {
            links.at(edge.link).driving = binding;
        } else if (binding && !(out.isForecast && !dependencyFromFinish(edge.rule.type) &&
                                progressAsOf(p, prev.taskId) && progressAsOf(p, prev.taskId)->actualStart))
            result[edge.to].drivingPredecessors.push_back(prev.taskId);
    }
    out.tasks = std::move(result);
    out.resourceOrderLinks = std::move(links);
    out.levelingFloatCalculated = true;
    out.calculationPolicy += "/resource-order-float-v1";
    out.diagnostics.push_back({"LEVELING_FLOAT_POLICY", {}, {}, "LEVELING_FLOAT_POLICY"});
}
} // namespace
LevelingFloatScope::LevelingFloatScope(int value) : previous_(version) {
    version = value;
}
LevelingFloatScope::~LevelingFloatScope() {
    version = previous_;
}
bool levelingFloatEnabled() {
    return version != 0;
}
void calculateWorkContourFloat(const Project &p, ScheduleResult &out) {
    out.logicalFloatCalculated = false;
    for (auto &r : out.tasks) {
        r.totalFloatMinutes = r.freeFloatMinutes = 0;
        r.critical = false;
    }
    try {
        auto candidate = out;
        LevelingFloatScope scope(8);
        calculateRepartitionLevelingFloat(p, candidate, true);
        std::erase_if(candidate.diagnostics, [](const auto &d) { return d.code.starts_with("LEVELING_"); });
        candidate.diagnostics.push_back({"WORK_CONTOUR_FLOAT_POLICY", {}, {}, "WORK_CONTOUR_FLOAT_POLICY"});
        out = std::move(candidate);
    } catch (const CalculationCancelled &) {
        throw;
    } catch (const std::exception &e) {
        out.logicalFloatReason = e.what();
        out.diagnostics.push_back(
            {"WORK_CONTOUR_FLOAT_UNAVAILABLE", {out.logicalFloatReason}, {}, "WORK_CONTOUR_FLOAT_UNAVAILABLE"});
    }
}
int levelingFloatVersion() {
    return version;
}
void calculateLevelingFloat(const Project &p, ScheduleResult &out) {
    EvaluationScope evaluation;
    if (version)
        std::erase_if(out.diagnostics, [](const auto &d) {
            return d.code == "FORECAST_FLOAT_POLICY" || d.code == "LEVELING_FLOAT_POLICY" ||
                   d.code == "LEVELING_SEGMENT_FLOAT_POLICY" || d.code == "LEVELING_REPARTITION_FLOAT_POLICY" ||
                   d.code == "LEVELING_REPARTITION_WITNESS" || d.code == "LEVELING_REPARTITION_SEARCH_POLICY" ||
                   d.code == "LEVELING_REPARTITION_COMPONENT_POLICY" ||
                   d.code == "LEVELING_REPARTITION_WITNESS_PREVIEW" ||
                   d.code == "LEVELING_REPARTITION_PROPAGATION_POLICY" ||
                   d.code == "LEVELING_NONMONOTONE_PROPAGATION_POLICY" ||
                   d.code == "LEVELING_PROPAGATION_COST_POLICY" || d.code == "LEVELING_DATED_FLOAT_POLICY" ||
                   d.code == "LEVELING_FLOAT_UNAVAILABLE" || d.code == "MATERIAL_FLOAT_POLICY";
        });
    out.levelingFloatCalculated = false;
    out.resourceOrderLinks.clear();
    out.levelingWitnesses.reset();
    out.levelingFloatReason.clear();
    for (auto &r : out.tasks) {
        r.totalFloatMinutes = r.freeFloatMinutes = 0;
        r.critical = false;
        r.drivingPredecessors.clear();
        r.lateStartMinutes = r.earlyStartMinutes;
        r.lateFinishMinutes = r.earlyFinishMinutes;
    }
    if (!version || out.status != CalculationStatus::Valid || !out.projectFinish)
        return;
    try {
        auto candidate = out;
        bool stripped = true;
        while (stripped) {
            stripped = false;
            for (const auto suffix :
                 {"/resource-order-float-v1", "/resource-segment-float-v2", "/resource-repartition-float-v3",
                  "/resource-dated-float-v1", "/resource-material-float-v1"})
                if (candidate.calculationPolicy.ends_with(suffix)) {
                    candidate.calculationPolicy.resize(candidate.calculationPolicy.size() - std::string(suffix).size());
                    stripped = true;
                }
        }
        auto previous = [&] {
            try {
                run(p, candidate);
            } catch (const Unavailable &e) {
                if (version < 2 || !e.segmentCandidate)
                    throw;
                calculateSegmentLevelingFloat(p, candidate);
            }
        };
        if ((version >= 9 && hasMaterialInventory(p)) || hasAssignmentPeriods(p) ||
            (out.isForecast && !p.remainingWork.empty()) ||
            std::any_of(p.resources.begin(), p.resources.end(), hasResourceAvailability)) {
            // Constant assignment envelopes cannot prove float for shaped demand.
            // Search each candidate with its actual, calendar-intersected contour.
            calculateRepartitionLevelingFloat(p, candidate);
        } else if (version < 3)
            previous();
        else {
            try {
                previous();
            } catch (const CalculationCancelled &) {
                throw;
            } catch (const Unavailable &e) {
                candidate.levelingFloatReason = e.reason;
            } catch (const std::exception &e) {
                candidate.levelingFloatReason = e.what();
            }
            calculateRepartitionLevelingFloat(p, candidate);
        }
        out = std::move(candidate);
    } catch (const CalculationCancelled &) {
        throw;
    } catch (const Unavailable &e) {
        out.levelingFloatReason = e.reason;
    } catch (const std::exception &e) {
        out.levelingFloatReason = e.what();
    }
    if (!out.levelingFloatCalculated)
        out.diagnostics.push_back(
            {"LEVELING_FLOAT_UNAVAILABLE", {out.levelingFloatReason}, {}, "LEVELING_FLOAT_UNAVAILABLE"});
}
} // namespace jobs
