#include "calculation/CapacityTimeline.h"
#include "calculation/MaterialFloatCheck.h"
#include "domain/ResourcePlan.h"
#include "domain/ResourceAvailability.h"
#include "calculation/LevelingFloat.h"
#include "calculation/ProgressPolicies.h"
#include "calculation/Effort.h"
#include "domain/RemainingWork.h"
#include "calculation/Constraints.h"
#include "calculation/Pdm.h"
#include "calculation/CandidateDomains.h"
#include "calculation/MandatoryDemand.h"
#include "calculation/PropagationCost.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace ppm {
namespace {
using Minute = std::int64_t;
const std::string &resume(const TaskSchedule &r) {
    return r.remainingStart ? *r.remainingStart : r.start;
}
[[noreturn]] void unavailable(const std::string &reason) {
    throw std::runtime_error("재분할 여유 계산: " + reason);
}
struct Budget {
    std::size_t checks = 0, candidates = 0, spans = 0, witnessEntries = 0;
    void check() {
        calculationCheckpoint();
        if (++checks > 2000000)
            unavailable("탐색 판정 한도 2,000,000회 초과");
    }
};
struct PropagationLimit {};
struct Span {
    std::size_t resource;
    Minute start, finish;
    int units;
};
struct Candidate {
    TaskSchedule row;
    std::vector<Span> spans;
};
std::optional<std::pair<Minute, Minute>> range(const Candidate &c, std::size_t resource) {
    std::optional<std::pair<Minute, Minute>> result;
    for (const auto &s : c.spans)
        if (s.resource == resource) {
            if (!result)
                result = {s.start, s.finish};
            else {
                result->first = std::min(result->first, s.start);
                result->second = std::max(result->second, s.finish);
            }
        }
    return result;
}
// A reversible exact demand timeline, including daily capacity and fractional last-minute units.
class Timeline {
    const Project &p_;
    const WorkCalendar &clock_;
    std::vector<std::map<Minute, Minute>> events_;
    std::vector<CapacityTimeline> capacities_;

public:
    Timeline(const Project &p, const WorkCalendar &clock) : p_(p), clock_(clock), events_(p.resources.size()) {
        for (const auto &resource : p.resources)
            capacities_.emplace_back(resource, clock);
    }
    int capacity(std::size_t r, Minute at) const {
        auto cap = capacities_[r].at(at);
        if (!cap)
            unavailable("가용량 미정: " + p_.resources[r].id + " / " + clock_.boundary(at, false));
        return *cap;
    }
    void add(const Candidate &c, int sign) {
        for (const auto &s : c.spans) {
            auto &e = events_[s.resource];
            e[s.start] += sign * s.units;
            if (!e[s.start])
                e.erase(s.start);
            e[s.finish] -= sign * s.units;
            if (!e[s.finish])
                e.erase(s.finish);
        }
    }
    bool fits(const Candidate &c, std::vector<Span> *conflicts = nullptr) const {
        bool valid = true;
        for (const auto &s : c.spans) {
            calculationCheckpoint();
            const auto &e = events_[s.resource];
            auto it = e.begin();
            Minute used = 0;
            while (it != e.end() && it->first <= s.start) {
                used += it->second;
                ++it;
            }
            for (auto at = s.start; at < s.finish;) {
                calculationCheckpoint();
                auto end = capacities_[s.resource].next(at, std::min(s.finish, (at / 1440 + 1) * 1440));
                if (it != e.end())
                    end = std::min(end, it->first);
                if (used + s.units > capacity(s.resource, at)) {
                    if (!conflicts)
                        return false;
                    valid = false;
                    conflicts->push_back({s.resource, at, end, s.units});
                }
                at = end;
                while (it != e.end() && it->first <= at) {
                    used += it->second;
                    ++it;
                }
            }
        }
        return valid;
    }
};
struct Rule {
    std::size_t from, to, resource = 0;
    std::optional<Dependency> logical;
    bool completionOrder = false;
};
} // namespace

void calculateRepartitionLevelingFloat(const Project &source, ScheduleResult &out, bool logicalOnly) {
    EvaluationScope evaluation;
    const bool material = !logicalOnly && levelingFloatVersion() >= 9 && hasMaterialInventory(source);
    const bool dated = out.isForecast && !source.remainingWork.empty();
    auto p = progressPolicyGraph(source);
    const bool curvedWork = hasWorkAssignmentPeriods(p) || (dated && hasDatedEffort(p));
    const bool previous = out.levelingFloatCalculated;
    const bool scalable = levelingFloatVersion() >= 4;
    const bool expanded = levelingFloatVersion() >= 5;
    const bool optimized = levelingFloatVersion() >= 6;
    const bool nonmonotone = levelingFloatVersion() >= 7;
    const bool costAware = levelingFloatVersion() >= 8;
    std::size_t savedBounds = 0, branchExcluded = 0, certificates = 0, reused = 0, pruneSkipped = 0;
    std::size_t mandatoryRemoved = 0, supportExcluded = 0, loadExcluded = 0, propagationRounds = 0;
    std::size_t propagationChecks = 0, propagationFallbacks = 0;
    bool propagationAvailable = true;
    std::size_t globalSkipped = 0, unitsSkipped = 0, predictedEnabled = 0, observedEnabled = 0, backoffs = 0;
    std::uint64_t estimatedChecks = 0;
    std::size_t estimatesRefined = 0;
    const bool trigger = material || hasAssignmentPeriods(p) ||
                         std::any_of(p.tasks.begin(), p.tasks.end(),
                                     [](const auto &t) { return !t.breaks.empty() || !t.calendarId.empty(); }) ||
                         std::any_of(p.resources.begin(), p.resources.end(), [](const auto &r) {
                             return !r.calendarId.empty() || (!r.exceptions.empty() || hasResourceAvailability(r));
                         });
    if (previous && !trigger)
        return;
    const auto n = p.tasks.size();
    if (n > 10000)
        unavailable("작업 한도 10,000개 초과");
    Budget budget;
    auto propagationCheck = [&] {
        if (propagationChecks >= 50000 || budget.checks >= 1900000)
            throw PropagationLimit{};
        ++propagationChecks;
        budget.check();
    };
    WorkCalendar nativeClock(p);
    PlanInput civilInput = p;
    civilInput.calculationProfile = pdmProfile;
    WorkCalendar clock(civilInput);
    ExecutionInputs inputs(p);
    std::unordered_map<std::string, std::size_t> ids, resources;
    for (std::size_t i = 0; i < n; ++i)
        ids.emplace(p.tasks[i].id, i);
    for (std::size_t i = 0; i < p.resources.size(); ++i)
        resources.emplace(p.resources[i].id, i);
    std::vector<Candidate> early(n);
    for (const auto &r : out.tasks)
        early.at(ids.at(r.taskId)).row = r;
    std::vector<std::unique_ptr<TaskExecution>> execution(n);
    std::vector<std::unique_ptr<WorkCalendar>> calendars(n);
    std::vector<Minute> first(n), upper(n), duration(n);
    std::vector<std::vector<const Assignment *>> assigned(n);
    for (const auto &a : p.assignments)
        assigned[ids.at(a.taskId)].push_back(&a);
    for (std::size_t i = 0; i < n; ++i) {
        budget.check();
        if (isEffort(p))
            execution[i] = std::make_unique<TaskExecution>(p, out.isForecast ? pendingTask(p, p.tasks[i]) : p.tasks[i],
                                                           &inputs, out.isForecast);
        else {
            calendars[i] = std::make_unique<WorkCalendar>(p, p.tasks[i]);
            const auto *fact = out.isForecast ? progressAsOf(p, p.tasks[i].id) : nullptr;
            duration[i] = fact ? fact->remainingMinutes : p.tasks[i].durationMinutes.value_or(0);
        }
        if (!early[i].row.completedActual)
            first[i] = execution[i]  ? execution[i]->offset(resume(early[i].row))
                       : duration[i] ? calendars[i]->offset(resume(early[i].row))
                                     : calendars[i]->pointOffset(resume(early[i].row));
    }
    auto calendar = [&](std::size_t i) -> const WorkCalendar & {
        return execution[i] ? execution[i]->calendar() : *calendars[i];
    };
    auto dates = [&](std::size_t i, Minute value) {
        if (value == first[i])
            return early[i].row;
        TaskSchedule r;
        if (execution[i])
            r = execution[i]->dates(value);
        else
            pdmDates(nativeClock, calendar(i), r, value, duration[i]);
        r.taskId = p.tasks[i].id;
        if (out.isForecast) {
            r.remainingStart = r.start;
            if (const auto *fact = progressAsOf(p, r.taskId); fact && fact->actualStart)
                r.start = early[i].row.start;
        }
        return r;
    };
    auto profile = [&](std::size_t i, TaskSchedule row) {
        Candidate c{std::move(row), {}};
        if (c.row.completedActual)
            return c;
        if (execution[i]) {
            for (const auto &s : execution[i]->spans(c.row))
                c.spans.push_back({resources.at(s.resourceId), s.start, s.finish, s.units});
        } else if (!assigned[i].empty()) {
            for (const auto &[a, b] : calendar(i).segments(resume(c.row), c.row.finish))
                for (const auto *r : assigned[i])
                    c.spans.push_back({resources.at(r->resourceId), clock.offset(a), clock.offset(b), r->units});
        }
        if (logicalOnly)
            c.spans.clear();
        return c;
    };
    for (std::size_t i = 0; i < n; ++i)
        early[i] = profile(i, early[i].row);
    std::optional<MaterialFloatCheck> inventory;
    if (material) {
        inventory.emplace(p, out);
        if (!inventory->fits(out.tasks))
            unavailable("현재 배치의 자재 재고 검증 실패");
    }
    // Components contain every shared resource and active logical relationship.
    std::vector<std::size_t> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    auto root = [&](std::size_t i) {
        auto r = i;
        while (parent[r] != r)
            r = parent[r];
        while (parent[i] != i) {
            const auto next = parent[i];
            parent[i] = r;
            i = next;
        }
        return r;
    };
    auto unite = [&](std::size_t a, std::size_t b) { parent[root(a)] = root(b); };
    std::vector<Rule> rules;
    std::vector<std::vector<std::size_t>> incident(n), successors(n), predecessors(n);
    auto addRule = [&](Rule r) {
        if (rules.size() >= 250000)
            unavailable("관계 한도 250,000개 초과");
        const auto index = rules.size();
        incident[r.from].push_back(index);
        incident[r.to].push_back(index);
        if (r.logical) {
            successors[r.from].push_back(index);
            predecessors[r.to].push_back(r.from);
        }
        rules.push_back(std::move(r));
    };
    for (std::size_t i = 0; i < n; ++i)
        if (!early[i].row.completedActual)
            for (const auto &d : dependencies(p.tasks[i])) {
                const auto from = ids.at(d.predecessorId);
                addRule({from, i, 0, d, false});
                if (!early[from].row.completedActual)
                    unite(from, i);
            }
    std::vector<std::optional<std::size_t>> owner(p.resources.size());
    for (std::size_t i = 0; i < n; ++i)
        if (!logicalOnly && !early[i].row.completedActual)
            for (const auto *a : assigned[i]) {
                auto &last = owner[resources.at(a->resourceId)];
                if (last)
                    unite(*last, i);
                last = i;
            }
    if (inventory) {
        std::map<std::string, std::size_t> owner;
        for (const auto &u : inventory->uses()) {
            const auto i = ids.at(u.taskId);
            if (early[i].row.completedActual)
                continue;
            const auto [it, inserted] = owner.emplace(u.materialId, i);
            if (!inserted)
                unite(it->second, i);
        }
    }
    std::map<std::size_t, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < n; ++i)
        if (!early[i].row.completedActual)
            groups[root(i)].push_back(i);
    std::vector<bool> affected(n, false);
    for (const auto &[key, group] : groups) {
        (void)key;
        bool dynamic = !previous, hasResource = false;
        for (const auto i : group) {
            dynamic = dynamic || !p.tasks[i].breaks.empty() || !p.tasks[i].calendarId.empty();
            hasResource = hasResource || !assigned[i].empty();
            for (const auto *a : assigned[i]) {
                const auto &r = p.resources[resources.at(a->resourceId)];
                dynamic = dynamic || !a->periods.empty() || !r.calendarId.empty() ||
                          (!r.exceptions.empty() || hasResourceAvailability(r));
            }
        }
        if (material || (dynamic && hasResource) || curvedWork || logicalOnly)
            for (const auto i : group)
                affected[i] = true;
    }
    if (previous && std::none_of(affected.begin(), affected.end(), [](bool b) { return b; }))
        return;
    auto logicalFits = [&](const Rule &r, const Candidate &a, const Candidate &b) {
        if (b.row.completedActual)
            return true;
        if (r.logical) {
            const auto &d = *r.logical;
            return calendar(r.to).shift(dependencyFromFinish(d.type) ? a.row.finish : a.row.start, d.lagMinutes) <=
                   (dependencyToFinish(d.type) ? b.row.finish : resume(b.row));
        }
        const auto x = range(a, r.resource), y = range(b, r.resource);
        return !x || !y || x->second <= (r.completionOrder ? y->second : y->first);
    };
    auto unary = [&](std::size_t i, const TaskSchedule &r) {
        return r.finish <= *out.projectFinish && (!hasConstraints(p) || !timingViolation(p.tasks[i], r));
    };
    auto keyFor = [&](const Rule &r, bool from, const Candidate &c) -> std::optional<std::string> {
        if (r.logical) {
            const auto &d = *r.logical;
            return from ? calendar(r.to).shift(dependencyFromFinish(d.type) ? c.row.finish : c.row.start, d.lagMinutes)
                   : dependencyToFinish(d.type) ? c.row.finish
                                                : resume(c.row);
        }
        const auto times = range(c, r.resource);
        if (!times)
            return {};
        return clock.boundary(from || r.completionOrder ? times->second : times->first, false);
    };
    // Logical latest bounds are monotone; resource feasibility is deliberately not binary searched.
    auto latest = [&](std::size_t i, Minute hi, const std::vector<Candidate> &targets, bool relations) {
        auto lo = first[i];
        hi = std::max(lo, hi);
        while (lo < hi) {
            budget.check();
            const auto mid = lo + (hi - lo + 1) / 2;
            bool fits = false;
            try {
                Candidate c{dates(i, mid), {}};
                fits = unary(i, c.row);
                if (fits && relations)
                    for (const auto e : successors[i])
                        if (!logicalFits(rules[e], c, targets[rules[e].to])) {
                            fits = false;
                            break;
                        }
            } catch (const CalculationCancelled &) {
                throw;
            } catch (const AppError &) {
            }
            if (fits)
                lo = mid;
            else
                hi = mid - 1;
        }
        return lo;
    };
    auto late = early;
    for (std::size_t i = 0; i < n; ++i)
        if (!early[i].row.completedActual) {
            const auto end = execution[i]  ? execution[i]->offset(*out.projectFinish)
                             : duration[i] ? calendar(i).offset(*out.projectFinish)
                                           : calendar(i).pointOffset(*out.projectFinish);
            upper[i] = curvedWork ? std::max(first[i], end) : latest(i, end, late, false);
            if (!curvedWork)
                late[i].row = dates(i, upper[i]);
        }
    std::deque<std::size_t> queue;
    std::vector<bool> queued(n, true);
    if (!curvedWork)
        for (std::size_t i = n; i > 0; --i)
            queue.push_back(i - 1);
    while (!queue.empty()) {
        const auto i = queue.front();
        queue.pop_front();
        queued[i] = false;
        if (early[i].row.completedActual)
            continue;
        const auto value = latest(i, upper[i], late, true);
        if (value < upper[i]) {
            upper[i] = value;
            late[i].row = dates(i, value);
            for (const auto j : predecessors[i])
                if (!queued[j]) {
                    queued[j] = true;
                    queue.push_back(j);
                }
        }
    }
    // Rebuild current FIFO handovers from real spans, allowing retirement/new capacity.
    struct Event {
        Minute at;
        int kind;
        std::size_t task;
        int units;
    };
    struct Lot {
        int units;
        std::optional<std::size_t> task;
        Minute release;
    };
    std::vector<std::vector<Event>> events(p.resources.size());
    for (std::size_t i = 0; i < n; ++i)
        for (const auto &s : early[i].spans) {
            if (++budget.spans > 100000)
                unavailable("원본 실행 구간 한도 100,000개 초과");
            events[s.resource].push_back({s.start, 2, i, s.units});
            events[s.resource].push_back({s.finish, 0, i, s.units});
        }
    Timeline empty(p, clock);
    std::vector<ResourceOrderLink> handovers;
    for (std::size_t r = 0; r < events.size(); ++r) {
        auto &list = events[r];
        if (list.empty() || std::none_of(list.begin(), list.end(), [&](const auto &e) { return affected[e.task]; }))
            continue;
        auto [min, max] =
            std::minmax_element(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.at < b.at; });
        const auto begin = min->at / 1440 * 1440, end = max->at / 1440 * 1440;
        for (auto day = begin; day <= end; day += 1440) {
            budget.check();
            list.push_back({day, 1, 0, empty.capacity(r, day)});
        }
        std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
            return std::tie(a.at, a.kind, a.task) < std::tie(b.at, b.kind, b.task);
        });
        int cap = empty.capacity(r, begin), busy = 0;
        std::deque<Lot> free{{cap, {}, 0}};
        for (const auto &e : list) {
            budget.check();
            if (e.kind == 0) {
                busy -= e.units;
                free.push_back({e.units, e.task, e.at});
                continue;
            }
            if (e.kind == 1) {
                if (busy > e.units)
                    unavailable("원본 자원 부하 불일치");
                if (e.units > cap)
                    free.push_back({e.units - cap, {}, 0});
                auto remove = std::max(0, cap - e.units);
                while (remove && !free.empty()) {
                    auto &lot = free.front();
                    const auto count = std::min(remove, lot.units);
                    remove -= count;
                    lot.units -= count;
                    if (!lot.units)
                        free.pop_front();
                }
                if (remove)
                    unavailable("용량 회수 불일치");
                cap = e.units;
                continue;
            }
            auto need = e.units;
            if (busy + need > cap)
                unavailable("원본 자원 부하 초과");
            while (need && !free.empty()) {
                auto &lot = free.front();
                const auto count = std::min(need, lot.units);
                if (count && lot.task && *lot.task != e.task) {
                    if (handovers.size() >= 250000)
                        unavailable("자원 인계 한도 250,000개 초과");
                    handovers.push_back({p.tasks[*lot.task].id, p.tasks[e.task].id, p.resources[r].id, count, false,
                                         clock.boundary(lot.release, true), clock.boundary(e.at, false)});
                }
                need -= count;
                lot.units -= count;
                if (!lot.units)
                    free.pop_front();
            }
            if (need)
                unavailable("슬롯 할당 불일치");
            busy += e.units;
        }
    }
    std::set<std::tuple<std::size_t, std::size_t, std::size_t, bool>> unique;
    for (const auto &link : handovers) {
        auto a = ids.at(link.predecessorId), b = ids.at(link.taskId);
        const auto r = resources.at(link.resourceId);
        const auto x = range(early[a], r).value(), y = range(early[b], r).value();
        const bool overlap = x.second > y.first;
        if (overlap && std::tie(x.second, a) > std::tie(y.second, b))
            std::swap(a, b);
        if (unique.emplace(a, b, r, overlap).second)
            addRule({a, b, r, {}, overlap});
    }
    if (optimized && !curvedWork) {
        // For positive duration demand on a matching calendar, resource range endpoints
        // equal resume/finish for EVERY candidate. Only that proved model is monotone.
        auto fullDemand = [&](std::size_t i, std::size_t r) {
            if (dated || early[i].row.completedActual)
                return false;
            const auto assignedHere = std::any_of(assigned[i].begin(), assigned[i].end(), [&](const auto *a) {
                return a->resourceId == p.resources[r].id && a->units > 0 && a->periods.empty();
            });
            if (!assignedHere)
                return false;
            if (!execution[i])
                return duration[i] > 0;
            const auto *fact = out.isForecast ? progressAsOf(p, p.tasks[i].id) : nullptr;
            const auto amount = fact ? fact->remainingMinutes : p.tasks[i].durationMinutes.value_or(0);
            return !usesWork(p.tasks[i]) && amount > 0 && p.tasks[i].calendarId == p.resources[r].calendarId;
        };
        std::vector<std::vector<std::size_t>> resourceBounds(n), boundPredecessors = predecessors;
        bool anyBounds = false;
        for (std::size_t e = 0; e < rules.size(); ++e) {
            const auto &r = rules[e];
            if (!r.logical && fullDemand(r.from, r.resource) && fullDemand(r.to, r.resource)) {
                resourceBounds[r.from].push_back(e);
                boundPredecessors[r.to].push_back(r.from);
                anyBounds = true;
            }
        }
        if (anyBounds) {
            std::deque<std::size_t> pending;
            std::vector<bool> enqueued(n, true);
            for (std::size_t k = n; k > 0; --k)
                pending.push_back(k - 1);
            while (!pending.empty()) {
                budget.check();
                const auto i = pending.front();
                pending.pop_front();
                enqueued[i] = false;
                if (early[i].row.completedActual)
                    continue;
                auto value = latest(i, upper[i], late, true);
                for (const auto e : resourceBounds[i]) {
                    const auto &r = rules[e];
                    const auto &target = late[r.to].row;
                    const auto bound = r.completionOrder ? target.finish : resume(target);
                    auto lo = first[i], hi = value;
                    while (lo < hi) {
                        budget.check();
                        const auto mid = lo + (hi - lo + 1) / 2;
                        if (dates(i, mid).finish <= bound)
                            lo = mid;
                        else
                            hi = mid - 1;
                    }
                    value = lo;
                }
                if (value < upper[i]) {
                    savedBounds += std::size_t(upper[i] - value);
                    upper[i] = value;
                    late[i].row = dates(i, value);
                    for (const auto before : boundPredecessors[i])
                        if (!enqueued[before]) {
                            enqueued[before] = true;
                            pending.push_back(before);
                        }
                }
            }
        }
    }
    auto result = out.tasks;
    if (material)
        for (std::size_t i = 0; i < n; ++i)
            result[i] = early[i].row;
    std::vector<Diagnostic> proofs;
    auto witnesses = std::make_shared<LevelingFloatWitnesses>();
    if (scalable && !logicalOnly && resourceLoad(p, out).status != CalculationStatus::Valid)
        unavailable("현재 배치의 전체 자원 검증 실패");
    for (const auto &[key, group] : groups) {
        (void)key;
        if (!affected[group.front()]) {
            if (!previous)
                for (const auto i : group) {
                    auto &row = result[i];
                    const auto free = dates(i, latest(i, upper[i], early, true));
                    row.totalFloatMinutes =
                        calendar(i).offset(resume(late[i].row)) - calendar(i).offset(resume(early[i].row));
                    row.freeFloatMinutes = calendar(i).offset(resume(free)) - calendar(i).offset(resume(early[i].row));
                    row.lateStartMinutes = nativeClock.offset(resume(late[i].row));
                    row.lateFinishMinutes = nativeClock.offset(late[i].row.finish);
                    row.critical = row.totalFloatMinutes == 0;
                }
            continue;
        }
        if (!scalable && group.size() > 64)
            unavailable("연결 작업 묶음 한도 64개 초과");
        std::vector<std::vector<Candidate>> domains(group.size());
        std::vector<std::size_t> position(n, n);
        for (std::size_t k = 0; k < group.size(); ++k)
            position[group[k]] = k;
        for (std::size_t k = 0; k < group.size(); ++k) {
            const auto i = group[k];
            std::pair<std::string, std::string> lastDates;
            for (auto v = first[i]; v <= upper[i]; ++v) {
                budget.check();
                if (++budget.candidates > 250000)
                    unavailable("후보 시작 한도 250,000개 초과");
                Candidate c;
                try {
                    auto row = dates(i, v);
                    const auto keyDates = std::pair{resume(row), row.finish};
                    if (keyDates == lastDates)
                        continue;
                    lastDates = keyDates;
                    c = profile(i, std::move(row));
                } catch (const AppError &e) {
                    // A fixed-date goal may disappear from the candidate window or
                    // require an impossible rate. It excludes this candidate only.
                    if (dated && e.diagnostic().textKey == "DATED_REMAINING_INVALID")
                        continue;
                    if (curvedWork &&
                        (e.code() == "WORK_HORIZON_EXHAUSTED" || e.code() == "WORK_CALENDAR_NO_INTERSECTION"))
                        continue;
                    throw;
                }
                budget.spans += c.spans.size();
                if (budget.spans > 2000000)
                    unavailable("후보 실행 구간 한도 2,000,000개 초과");
                if (unary(i, c.row) && empty.fits(c))
                    domains[k].push_back(std::move(c));
            }
            if (domains[k].empty() || resume(domains[k].front().row) != resume(early[i].row))
                unavailable("원본 후보 재현 실패");
        }
        std::vector<std::vector<std::size_t>> units;
        std::vector<std::size_t> unitOf(group.size(), group.size());
        // Only dynamic execution groups need the additional profile propagation.
        const bool propagateDemand =
            nonmonotone && std::any_of(group.begin(), group.end(), [&](auto i) {
                return execution[i] && (dated || usesWork(p.tasks[i]) || !p.tasks[i].breaks.empty() ||
                                        std::any_of(assigned[i].begin(), assigned[i].end(), [&](const auto *a) {
                                            const auto &r = p.resources[resources.at(a->resourceId)];
                                            return !a->periods.empty() || p.tasks[i].calendarId != r.calendarId ||
                                                   (!r.exceptions.empty() || hasResourceAvailability(r));
                                        }));
            });
        auto minimumDemand = [&](std::size_t k, const CandidateDomains *live) {
            Candidate lower;
            bool firstCandidate = true;
            for (std::size_t c = 0; c < domains[k].size(); ++c) {
                propagationCheck();
                if (live && !live->live(k, c))
                    continue;
                auto normalized = normalizeDemand(domains[k][c].spans, propagationCheck);
                if (firstCandidate) {
                    lower.spans = std::move(normalized);
                    firstCandidate = false;
                } else
                    lower.spans = intersectDemand(lower.spans, normalized, propagationCheck);
                if (lower.spans.empty())
                    break;
            }
            return lower;
        };
        auto estimate = [&](const std::vector<std::size_t> &work) {
            PropagationCost cost;
            std::set<std::size_t> edges;
            for (const auto k : work) {
                calculationCheckpoint();
                if (domains[k].size() > 1) {
                    ++cost.movable;
                    cost.candidates += domains[k].size();
                }
                for (const auto &c : domains[k])
                    cost.spans += c.spans.size();
                edges.insert(incident[group[k]].begin(), incident[group[k]].end());
            }
            for (const auto e : edges) {
                const auto &r = rules[e];
                cost.edges += position[r.from] == n ? 1 : domains[position[r.from]].size();
                cost.edges += position[r.to] == n ? 1 : domains[position[r.to]].size();
            }
            return cost;
        };
        if (scalable) {
            // Every rule is a binary inequality L(from) <= R(to). Resource-free candidates
            // disable that resource rule. Extremal supports remain exact even when candidate
            // demand is non-monotone; no resource-feasibility binary search is used.
            std::deque<std::size_t> pending;
            std::vector<bool> enqueued(rules.size(), false);
            auto enqueue = [&](std::size_t i) {
                if (!enqueued[i]) {
                    enqueued[i] = true;
                    pending.push_back(i);
                }
            };
            for (const auto i : group)
                for (const auto e : incident[i])
                    enqueue(e);
            for (;;) {
                while (!pending.empty()) {
                    budget.check();
                    const auto e = pending.front();
                    pending.pop_front();
                    enqueued[e] = false;
                    const auto &r = rules[e];
                    std::optional<std::string> minLeft, maxRight, maxLeft, minRight;
                    bool anyLeft = false, anyRight = false;
                    auto bounds = [&](std::size_t i, bool from) {
                        auto visit = [&](const Candidate &c) {
                            budget.check();
                            const auto value = keyFor(r, from, c);
                            auto &missing = from ? anyLeft : anyRight;
                            auto &bound = from ? minLeft : maxRight;
                            if (!value)
                                missing = true;
                            else if (!bound || (from ? *value < *bound : *value > *bound))
                                bound = value;
                            if (optimized && value) {
                                auto &other = from ? maxLeft : minRight;
                                if (!other || (from ? *value > *other : *value < *other))
                                    other = value;
                            }
                        };
                        if (position[i] == n)
                            visit(early[i]);
                        else
                            for (const auto &c : domains[position[i]])
                                visit(c);
                    };
                    bounds(r.from, true);
                    bounds(r.to, false);
                    auto prune = [&](std::size_t i, bool from) {
                        if (position[i] == n || (from ? anyRight : anyLeft))
                            return;
                        if (optimized && (from ? (!maxLeft || (maxRight && *maxLeft <= *maxRight))
                                               : (!minRight || (minLeft && *minRight >= *minLeft)))) {
                            ++pruneSkipped;
                            return;
                        }
                        auto &domain = domains[position[i]];
                        const auto count = domain.size();
                        std::erase_if(domain, [&](const Candidate &c) {
                            budget.check();
                            const auto value = keyFor(r, from, c);
                            return value && (from ? maxRight && *value > *maxRight : minLeft && *value < *minLeft);
                        });
                        if (domain.empty() || resume(domain.front().row) != resume(early[i].row))
                            unavailable("제약 후보 제거 후 원본 배치 재현 실패");
                        if (domain.size() != count)
                            for (const auto edge : incident[i])
                                enqueue(edge);
                    };
                    prune(r.from, true);
                    prune(r.to, false);
                }
                if (!propagateDemand || !propagationAvailable)
                    break;
                if (costAware) {
                    // M(C-M) < 4C <= 4W when M <= 4, so a detailed
                    // profile/edge estimate cannot enable an initial sweep.
                    if (std::count_if(domains.begin(), domains.end(), [](const auto &d) { return d.size() > 1; }) <=
                        4) {
                        ++globalSkipped;
                        break;
                    }
                    std::vector<std::size_t> work(group.size());
                    std::iota(work.begin(), work.end(), 0);
                    const auto cost = estimate(work);
                    estimatedChecks += cost.sweep();
                    if (!cost.worthwhile()) {
                        ++globalSkipped;
                        break;
                    }
                }
                try {
                    ++propagationRounds;
                    std::vector<Candidate> mandatory;
                    Timeline load(p, clock);
                    for (std::size_t k = 0; k < group.size(); ++k) {
                        mandatory.push_back(minimumDemand(k, nullptr));
                        load.add(mandatory.back(), 1);
                    }
                    if (std::all_of(mandatory.begin(), mandatory.end(), [](const auto &c) { return c.spans.empty(); }))
                        break;
                    bool changed = false;
                    for (std::size_t k = 0; k < group.size(); ++k) {
                        auto &domain = domains[k];
                        if (domain.size() < 2)
                            continue;
                        load.add(mandatory[k], -1);
                        const auto count = domain.size();
                        // Collect before erasing: a soft limit must never leave a
                        // half-compacted candidate array behind.
                        std::vector<bool> remove(count);
                        for (std::size_t c = 0; c < count; ++c) {
                            propagationCheck();
                            remove[c] = !load.fits(domain[c]);
                        }
                        std::size_t cursor = 0;
                        std::erase_if(domain, [&](const Candidate &) { return remove[cursor++]; });
                        load.add(mandatory[k], 1);
                        mandatoryRemoved += count - domain.size();
                        if (domain.empty() || resume(domain.front().row) != resume(early[group[k]].row))
                            unavailable("필수 수요 전파 후 원본 배치 재현 실패");
                        if (domain.size() != count) {
                            changed = true;
                            for (const auto edge : incident[group[k]])
                                enqueue(edge);
                        }
                    }
                    if (!changed)
                        break;
                } catch (const PropagationLimit &) {
                    propagationAvailable = false;
                    ++propagationFallbacks;
                    break;
                }
            }
            const auto movable =
                std::count_if(domains.begin(), domains.end(), [](const auto &d) { return d.size() > 1; });
            if (!expanded && movable > 128)
                unavailable("이동 가능 작업 한도 128개 초과");
            proofs.push_back(
                {"LEVELING_REPARTITION_SEARCH_POLICY",
                 {std::to_string(group.size()), std::to_string(group.size() - movable), std::to_string(movable)},
                 {},
                 "LEVELING_REPARTITION_SEARCH_POLICY"});
            if (expanded) {
                std::vector<std::size_t> parts(group.size());
                std::iota(parts.begin(), parts.end(), 0);
                auto findPart = [&](std::size_t i) {
                    auto top = i;
                    while (parts[top] != top)
                        top = parts[top];
                    while (parts[i] != i) {
                        const auto next = parts[i];
                        parts[i] = top;
                        i = next;
                    }
                    return top;
                };
                auto join = [&](std::size_t a, std::size_t b) { parts[findPart(a)] = findPart(b); };
                // Drop only inequalities satisfied by EVERY remaining candidate pair.
                // Missing resource demand disables that resource inequality, as in logicalFits.
                std::set<std::size_t> relevant;
                for (const auto i : group)
                    relevant.insert(incident[i].begin(), incident[i].end());
                for (const auto index : relevant) {
                    budget.check();
                    const auto &rule = rules[index];
                    const auto a = position[rule.from], b = position[rule.to];
                    if (a == n || b == n || domains[a].size() < 2 || domains[b].size() < 2)
                        continue;
                    std::optional<std::string> maxLeft, minRight;
                    for (const auto &c : domains[a]) {
                        budget.check();
                        const auto key = keyFor(rule, true, c);
                        if (key && (!maxLeft || *key > *maxLeft))
                            maxLeft = key;
                    }
                    for (const auto &c : domains[b]) {
                        budget.check();
                        const auto key = keyFor(rule, false, c);
                        if (key && (!minRight || *key < *minRight))
                            minRight = key;
                    }
                    if (maxLeft && minRight && *maxLeft > *minRight)
                        join(a, b);
                }
                struct Envelope {
                    Minute start, finish;
                    std::size_t task;
                };
                std::vector<std::vector<Envelope>> envelopes(p.resources.size());
                for (std::size_t k = 0; k < group.size(); ++k) {
                    if (domains[k].size() < 2)
                        continue;
                    std::map<std::size_t, std::pair<Minute, Minute>> ranges;
                    for (const auto &c : domains[k])
                        for (const auto &span : c.spans) {
                            calculationCheckpoint();
                            const auto [it, inserted] = ranges.try_emplace(span.resource, span.start, span.finish);
                            if (!inserted) {
                                it->second.first = std::min(it->second.first, span.start);
                                it->second.second = std::max(it->second.second, span.finish);
                            }
                        }
                    for (const auto &[resource, times] : ranges)
                        envelopes[resource].push_back({times.first, times.second, k});
                }
                // Overlap of conservative demand envelopes couples tasks regardless of units.
                // In particular, three 50% tasks cannot be separated by a pairwise capacity test.
                for (auto &list : envelopes) {
                    std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
                        return std::tie(a.start, a.finish, a.task) < std::tie(b.start, b.finish, b.task);
                    });
                    Minute right = 0;
                    std::size_t representative = group.size();
                    for (const auto &e : list) {
                        budget.check();
                        if (representative != group.size() && e.start < right) {
                            join(representative, e.task);
                            right = std::max(right, e.finish);
                        } else {
                            representative = e.task;
                            right = e.finish;
                        }
                    }
                }
                if (inventory) {
                    std::map<std::string, std::size_t> owner;
                    for (const auto &u : inventory->uses()) {
                        const auto k = position[ids.at(u.taskId)];
                        if (k == n || domains[k].size() < 2)
                            continue;
                        const auto [it, inserted] = owner.emplace(u.materialId, k);
                        if (!inserted)
                            join(it->second, k);
                    }
                }
                std::map<std::size_t, std::size_t> unitIds;
                std::size_t largest = 0;
                for (std::size_t k = 0; k < group.size(); ++k)
                    if (domains[k].size() > 1) {
                        const auto [it, inserted] = unitIds.emplace(findPart(k), units.size());
                        if (inserted)
                            units.emplace_back();
                        unitOf[k] = it->second;
                        units[it->second].push_back(k);
                        largest = std::max(largest, units[it->second].size());
                    }
                proofs.push_back({"LEVELING_REPARTITION_COMPONENT_POLICY",
                                  {std::to_string(units.size()), std::to_string(largest)},
                                  {},
                                  "LEVELING_REPARTITION_COMPONENT_POLICY"});
            }
        }
        struct EdgeKeys {
            std::vector<std::optional<std::string>> left, right;
            std::optional<std::string> maxLeft, minRight;
        };
        std::map<std::size_t, EdgeKeys> keyCache;
        std::size_t cachedEntries = 0;
        auto cachedKeys = [&](std::size_t index, bool extra) -> const EdgeKeys * {
            if (const auto it = keyCache.find(index); it != keyCache.end())
                return &it->second;
            const auto &r = rules[index];
            const auto a = position[r.from], b = position[r.to];
            if (a == n || b == n || cachedEntries + domains[a].size() + domains[b].size() > 250000)
                return nullptr;
            EdgeKeys keys;
            for (const auto &c : domains[a]) {
                if (extra && propagateDemand && propagationAvailable)
                    propagationCheck();
                else
                    budget.check();
                auto value = keyFor(r, true, c);
                if (value && (!keys.maxLeft || *value > *keys.maxLeft))
                    keys.maxLeft = value;
                keys.left.push_back(std::move(value));
            }
            for (const auto &c : domains[b]) {
                if (extra && propagateDemand && propagationAvailable)
                    propagationCheck();
                else
                    budget.check();
                auto value = keyFor(r, false, c);
                if (value && (!keys.minRight || *value < *keys.minRight))
                    keys.minRight = value;
                keys.right.push_back(std::move(value));
            }
            cachedEntries += keys.left.size() + keys.right.size();
            return &keyCache.emplace(index, std::move(keys)).first->second;
        };
        std::vector<std::vector<std::size_t>> certificatePlans(units.size());
        std::vector<std::vector<LevelingWitnessDates>> lastPlans(units.size());
        std::size_t plannedCertificates = 0;
        std::vector<PropagationDecision> decisions;
        if (costAware && propagateDemand)
            for (const auto &unit : units) {
                PropagationCost cost;
                const bool exact = unit.size() > 4;
                if (exact)
                    cost = estimate(unit);
                else {
                    cost.movable = unit.size();
                    for (const auto k : unit)
                        cost.candidates += domains[k].size();
                }
                estimatedChecks += cost.sweep();
                decisions.emplace_back(cost, exact);
                if (cost.worthwhile())
                    ++predictedEnabled;
                else
                    ++unitsSkipped;
            }
        Timeline background(p, clock);
        if (expanded)
            for (const auto i : group) {
                budget.check();
                background.add(early[i], 1);
            }
        auto solve = [&](std::size_t goal, bool fixedOthers) {
            Timeline ownTimeline(p, clock);
            auto &timeline = expanded ? background : ownTimeline;
            std::vector<const Candidate *> chosen(group.size(), nullptr);
            std::vector<std::size_t> work;
            if (expanded) {
                work = fixedOthers ? std::vector<std::size_t>{goal} : units.at(unitOf.at(goal));
                for (std::size_t k = 0; k < group.size(); ++k)
                    chosen[k] = &domains[k].front();
                for (const auto k : work) {
                    chosen[k] = nullptr;
                    timeline.add(early[group[k]], -1);
                }
            }
            std::optional<CandidateDomains> live;
            auto *decision = costAware && propagateDemand && !fixedOthers ? &decisions.at(unitOf.at(goal)) : nullptr;
            auto ensureLive = [&] {
                if (live)
                    return;
                std::vector<std::size_t> sizes(group.size());
                for (const auto k : work)
                    sizes[k] = domains[k].size();
                live.emplace(sizes);
            };
            const auto candidateVolume = std::accumulate(work.begin(), work.end(), std::size_t(0),
                                                         [&](auto sum, auto k) { return sum + domains[k].size(); });
            if (optimized && !fixedOthers &&
                ((propagateDemand && work.size() > 1 &&
                  (costAware ? decision && decision->active() : candidateVolume > 32)) ||
                 std::any_of(work.begin(), work.end(), [&](auto k) { return domains[k].size() > 32; }))) {
                ensureLive();
            }
            auto propagateProfiles = [&](std::size_t selected) {
                std::deque<std::size_t> pending;
                std::set<std::size_t> queued;
                auto enqueue = [&](std::size_t k) {
                    for (const auto e : incident[group[k]])
                        if (queued.insert(e).second)
                            pending.push_back(e);
                };
                enqueue(selected);
                for (;;) {
                    ++propagationRounds;
                    while (!pending.empty()) {
                        propagationCheck();
                        const auto e = pending.front();
                        pending.pop_front();
                        queued.erase(e);
                        const auto &r = rules[e];
                        const auto *keys = cachedKeys(e, true);
                        std::optional<std::string> minLeft, maxRight;
                        bool anyLeft = false, anyRight = false;
                        auto visit = [&](bool from, auto action) {
                            const auto i = from ? r.from : r.to;
                            const auto k = position[i];
                            if (k == n || chosen[k]) {
                                action(std::size_t(0), keyFor(r, from, k == n ? early[i] : *chosen[k]));
                                return;
                            }
                            for (std::size_t c = 0; c < domains[k].size(); ++c) {
                                if (!live->live(k, c))
                                    continue;
                                propagationCheck();
                                action(c,
                                       keys ? (from ? keys->left[c] : keys->right[c]) : keyFor(r, from, domains[k][c]));
                            }
                        };
                        for (const bool from : {true, false})
                            visit(from, [&](auto, const auto &value) {
                                auto &missing = from ? anyLeft : anyRight;
                                auto &bound = from ? minLeft : maxRight;
                                if (!value)
                                    missing = true;
                                else if (!bound || (from ? *value < *bound : *value > *bound))
                                    bound = value;
                            });
                        for (const bool from : {true, false}) {
                            const auto k = position[from ? r.from : r.to];
                            if (k == n || chosen[k] || (from ? anyRight : anyLeft))
                                continue;
                            bool changed = false;
                            visit(from, [&](auto c, const auto &value) {
                                if (value && (from ? maxRight && *value > *maxRight : minLeft && *value < *minLeft)) {
                                    live->exclude(k, c);
                                    ++supportExcluded;
                                    changed = true;
                                }
                            });
                            if (!live->count(k))
                                return false;
                            if (changed)
                                enqueue(k);
                        }
                    }
                    // Selected dates and fixed peers already occupy timeline. Add
                    // only the pointwise lower demand of each unselected task.
                    Timeline load = timeline;
                    std::vector<std::pair<std::size_t, Candidate>> mandatory;
                    for (const auto k : work)
                        if (!chosen[k]) {
                            mandatory.emplace_back(k, minimumDemand(k, &*live));
                            load.add(mandatory.back().second, 1);
                        }
                    bool changed = false;
                    for (const auto &[k, lower] : mandatory) {
                        load.add(lower, -1);
                        for (std::size_t c = 0; c < domains[k].size(); ++c) {
                            if (!live->live(k, c))
                                continue;
                            propagationCheck();
                            if (!load.fits(domains[k][c])) {
                                live->exclude(k, c);
                                ++loadExcluded;
                                changed = true;
                                enqueue(k);
                            }
                        }
                        load.add(lower, 1);
                        if (!live->count(k))
                            return false;
                    }
                    if (!changed)
                        return true;
                }
            };
            auto propagate = [&](std::size_t selected) {
                if (decision && propagationAvailable) {
                    if (decision->needsEstimate()) {
                        const auto cost = estimate(work);
                        estimatedChecks += cost.sweep();
                        decision->refine(cost);
                        ++estimatesRefined;
                    }
                    if (decision->observe())
                        ++observedEnabled;
                    if (decision->active())
                        ensureLive();
                }
                if (!live)
                    return true;
                if (propagateDemand && propagationAvailable && (!decision || decision->active())) {
                    try {
                        const auto beforeChecks = propagationChecks;
                        const auto beforeExcluded = supportExcluded + loadExcluded;
                        const auto fits = propagateProfiles(selected);
                        if (decision && decision->feedback(propagationChecks - beforeChecks,
                                                           supportExcluded + loadExcluded - beforeExcluded))
                            ++backoffs;
                        return fits;
                    } catch (const PropagationLimit &) {
                        // Every completed exclusion is still a necessary condition.
                        // Keep it in the reversible trail, then use the original
                        // exact search/selected-neighbor path for the rest.
                        propagationAvailable = false;
                        ++propagationFallbacks;
                        return true;
                    }
                }
                const auto i = group[selected];
                for (const auto e : incident[i]) {
                    budget.check();
                    const auto &r = rules[e];
                    const bool from = r.from == i;
                    const auto j = position[from ? r.to : r.from];
                    if (j == n || chosen[j])
                        continue;
                    const auto pin = keyFor(r, from, *chosen[selected]);
                    if (!pin)
                        continue;
                    const auto *keys = cachedKeys(e, false);
                    if (keys) {
                        const auto &bound = from ? keys->minRight : keys->maxLeft;
                        if (!bound || (from ? *pin <= *bound : *bound <= *pin))
                            continue;
                    }
                    for (std::size_t c = 0; c < domains[j].size(); ++c) {
                        if (!live->live(j, c))
                            continue;
                        budget.check();
                        const auto key =
                            keys ? (from ? keys->right[c] : keys->left[c]) : keyFor(r, !from, domains[j][c]);
                        if (key && (from ? *pin > *key : *key > *pin)) {
                            live->exclude(j, c);
                            ++branchExcluded;
                        }
                    }
                    if (!live->count(j))
                        return false;
                }
                return true;
            };
            auto allowed = [&](std::size_t k, const Candidate &c) {
                budget.check();
                const auto i = group[k];
                for (const auto index : incident[i]) {
                    const auto &r = rules[index];
                    const auto j = r.from == i ? r.to : r.from;
                    const Candidate *other = position[j] == n ? &early[j] : chosen[position[j]];
                    if (other && !(r.from == i ? logicalFits(r, c, *other) : logicalFits(r, *other, c))) {
                        if (decision)
                            decision->failedProbe();
                        return false;
                    }
                }
                bool fits = timeline.fits(c);
                if (fits && inventory) {
                    std::vector<TaskSchedule> rows;
                    rows.reserve(n);
                    for (std::size_t j = 0; j < n; ++j) {
                        const auto slot = position[j];
                        if (slot == n)
                            rows.push_back(early[j].row);
                        else if (slot == k)
                            rows.push_back(c.row);
                        else if (chosen[slot])
                            rows.push_back(chosen[slot]->row);
                    }
                    fits = inventory->fits(rows);
                }
                if (!fits && decision)
                    decision->failedProbe();
                return fits;
            };
            std::function<bool()> search = [&]() {
                std::size_t next = group.size();
                for (std::size_t k = 0; k < group.size(); ++k)
                    if (!chosen[k] && (next == group.size() || domains[k].size() < domains[next].size()))
                        next = k;
                if (next == group.size())
                    return true;
                for (auto it = domains[next].rbegin(); it != domains[next].rend(); ++it)
                    if (allowed(next, *it)) {
                        chosen[next] = &*it;
                        timeline.add(*it, 1);
                        if (search())
                            return true;
                        timeline.add(*it, -1);
                        chosen[next] = nullptr;
                    }
                return false;
            };
            auto iterativeSearch = [&]() {
                struct Frame {
                    std::size_t task, remaining;
                    bool installed = false;
                    std::size_t mark = 0;
                };
                std::vector<std::size_t> order;
                for (std::size_t k = 0; k < group.size(); ++k)
                    if (!chosen[k])
                        order.push_back(k);
                std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
                    if (expanded && incident[group[a]].size() != incident[group[b]].size())
                        return incident[group[a]].size() > incident[group[b]].size();
                    return domains[a].size() < domains[b].size();
                });
                std::vector<Frame> stack;
                for (const auto k : order)
                    stack.push_back({k, domains[k].size()});
                std::size_t depth = 0;
                while (depth < stack.size()) {
                    if (expanded)
                        calculationCheckpoint();
                    else
                        budget.check();
                    auto &frame = stack[depth];
                    if (frame.installed) {
                        if (live)
                            live->restore(frame.mark);
                        timeline.add(*chosen[frame.task], -1);
                        chosen[frame.task] = nullptr;
                        frame.installed = false;
                    }
                    while (frame.remaining) {
                        const auto candidate = --frame.remaining;
                        if (live && !live->live(frame.task, candidate))
                            continue;
                        const auto &c = domains[frame.task][candidate];
                        if (allowed(frame.task, c)) {
                            if (live)
                                frame.mark = live->mark();
                            timeline.add(c, 1);
                            chosen[frame.task] = &c;
                            if (!propagate(frame.task)) {
                                if (live)
                                    live->restore(frame.mark);
                                timeline.add(c, -1);
                                chosen[frame.task] = nullptr;
                                continue;
                            }
                            frame.installed = true;
                            break;
                        }
                    }
                    if (frame.installed) {
                        ++depth;
                        if (depth < stack.size())
                            stack[depth].remaining = domains[stack[depth].task].size();
                    } else if (!depth)
                        return false;
                    else
                        --depth;
                }
                return true;
            };
            if (!expanded && (fixedOthers || scalable))
                for (std::size_t k = 0; k < group.size(); ++k)
                    if (k != goal && (fixedOthers || domains[k].size() == 1)) {
                        if (scalable && !allowed(k, domains[k].front()))
                            unavailable("고정 작업 배치 재현 실패");
                        chosen[k] = &domains[k].front();
                        timeline.add(*chosen[k], 1);
                    }
            for (auto it = domains[goal].rbegin(); it != domains[goal].rend(); ++it)
                if (allowed(goal, *it)) {
                    const auto mark = live ? live->mark() : 0;
                    chosen[goal] = &*it;
                    timeline.add(*it, 1);
                    if (propagate(goal) && (fixedOthers || (scalable ? iterativeSearch() : search()))) {
                        if (expanded)
                            for (const auto k : work) {
                                timeline.add(*chosen[k], -1);
                                timeline.add(early[group[k]], 1);
                            }
                        return chosen;
                    }
                    if (live)
                        live->restore(mark);
                    timeline.add(*it, -1);
                    chosen[goal] = nullptr;
                }
            unavailable("원본 배치 재현 실패");
        };
        for (std::size_t goal = 0; goal < group.size(); ++goal) {
            if (scalable && domains[goal].size() == 1) {
                const auto i = group[goal];
                auto &row = result[i];
                row.totalFloatMinutes = row.freeFloatMinutes = 0;
                row.lateStartMinutes = nativeClock.offset(resume(early[i].row));
                row.lateFinishMinutes = nativeClock.offset(early[i].row.finish);
                row.critical = true;
                proofs.push_back({"LEVELING_REPARTITION_WITNESS",
                                  {p.tasks[i].id, "0",
                                   p.tasks[i].id + "=" + resume(early[i].row) + "~" + early[i].row.finish + "; "},
                                  {p.tasks[i].id},
                                  "LEVELING_REPARTITION_WITNESS"});
                if (++budget.witnessEntries > 2000000)
                    unavailable("증인 날짜 한도 2,000,000개 초과");
                witnesses->tasks.push_back(
                    {p.tasks[i].id,
                     {{p.tasks[i].id, early[i].row.start, early[i].row.finish, early[i].row.remainingStart}}});
                continue;
            }
            const auto total = solve(goal, false), free = solve(goal, true);
            if (inventory) {
                auto rows = result;
                for (std::size_t k = 0; k < group.size(); ++k)
                    rows[group[k]] = free[k]->row;
                if (!inventory->fits(rows))
                    unavailable("자유여유 증인의 자재 재고 검증 실패");
            }
            if (costAware && propagateDemand)
                decisions.at(unitOf.at(goal)).completedGoal();
            const auto i = group[goal];
            if (!optimized) {
                auto witness = out;
                witness.tasks = result;
                for (std::size_t k = 0; k < group.size(); ++k)
                    witness.tasks[group[k]] = total[k]->row;
                if (!logicalOnly && resourceLoad(p, witness).status != CalculationStatus::Valid)
                    unavailable("재배치 증인의 전체 자원 검증 실패");
                if (inventory && !inventory->fits(witness.tasks))
                    unavailable("재배치 증인의 자재 재고 검증 실패");
                ++certificates;
            }
            auto &row = result[i];
            row.totalFloatMinutes =
                calendar(i).offset(resume(total[goal]->row)) - calendar(i).offset(resume(early[i].row));
            row.freeFloatMinutes =
                calendar(i).offset(resume(free[goal]->row)) - calendar(i).offset(resume(early[i].row));
            if (row.totalFloatMinutes < row.freeFloatMinutes)
                unavailable("총/자유여유 증인 불일치");
            row.lateStartMinutes = nativeClock.offset(resume(total[goal]->row));
            row.lateFinishMinutes = nativeClock.offset(total[goal]->row.finish);
            row.critical = row.totalFloatMinutes == 0;
            std::string moves;
            std::size_t previewCount = 0, omitted = 0;
            LevelingFloatWitness evidence{p.tasks[i].id, {}};
            for (std::size_t k = 0; k < group.size(); ++k)
                if (!scalable || k == goal || resume(total[k]->row) != resume(early[group[k]].row) ||
                    total[k]->row.finish != early[group[k]].row.finish) {
                    if (scalable && ++budget.witnessEntries > 2000000)
                        unavailable("증인 날짜 한도 2,000,000개 초과");
                    const auto entry =
                        p.tasks[group[k]].id + "=" + resume(total[k]->row) + "~" + total[k]->row.finish + "; ";
                    if (!expanded || (previewCount < 20 && moves.size() + entry.size() <= 16000)) {
                        moves += entry;
                        ++previewCount;
                    } else
                        ++omitted;
                    if (scalable)
                        evidence.dates.push_back({p.tasks[group[k]].id, total[k]->row.start, total[k]->row.finish,
                                                  total[k]->row.remainingStart});
                }
            if (optimized) {
                ++plannedCertificates;
                const auto unit = unitOf[goal];
                std::vector<LevelingWitnessDates> signature;
                for (const auto k : units[unit])
                    signature.push_back({p.tasks[group[k]].id, total[k]->row.start, total[k]->row.finish,
                                         total[k]->row.remainingStart});
                if (signature != lastPlans[unit]) {
                    certificatePlans[unit].push_back(witnesses->tasks.size());
                    lastPlans[unit] = std::move(signature);
                }
            }
            if (scalable)
                witnesses->tasks.push_back(std::move(evidence));
            if (omitted)
                proofs.push_back(
                    {"LEVELING_REPARTITION_WITNESS_PREVIEW",
                     {p.tasks[i].id, std::to_string(row.totalFloatMinutes), moves, std::to_string(omitted)},
                     {p.tasks[i].id},
                     "LEVELING_REPARTITION_WITNESS_PREVIEW"});
            else
                proofs.push_back({"LEVELING_REPARTITION_WITNESS",
                                  {p.tasks[i].id, std::to_string(row.totalFloatMinutes), moves},
                                  {p.tasks[i].id},
                                  "LEVELING_REPARTITION_WITNESS"});
        }
        if (optimized) {
            std::size_t rounds = 0;
            for (const auto &plans : certificatePlans)
                rounds = std::max(rounds, plans.size());
            for (std::size_t round = 0; round < rounds; ++round) {
                budget.check();
                auto witness = out;
                witness.tasks = result;
                // Units have universally satisfied cross-unit relations and disjoint
                // potential resource envelopes. A combined native certificate therefore
                // certifies each unit plan with the same fixed/current background.
                for (const auto &plans : certificatePlans)
                    if (round < plans.size())
                        for (const auto &d : witnesses->tasks[plans[round]].dates) {
                            auto &row = witness.tasks[ids.at(d.taskId)];
                            row.start = d.start;
                            row.finish = d.finish;
                            row.remainingStart = d.remainingStart;
                            row.earlyStartMinutes = nativeClock.offset(d.remainingStart.value_or(d.start));
                            row.earlyFinishMinutes = nativeClock.offset(d.finish);
                        }
                if (!logicalOnly && resourceLoad(p, witness).status != CalculationStatus::Valid)
                    unavailable("결합 재배치 증인의 전체 자원 검증 실패");
                if (inventory && !inventory->fits(witness.tasks))
                    unavailable("재배치 증인의 자재 재고 검증 실패");
                ++certificates;
            }
            reused += plannedCertificates - rounds;
        }
    }
    // Current handovers are explanatory references. Repartition search may retire them.
    std::vector<std::vector<Span>> blocked(n);
    for (const auto &[key, group] : groups) {
        (void)key;
        if (!affected[group.front()])
            continue;
        Timeline current(p, clock);
        if (scalable)
            for (const auto i : group) {
                budget.check();
                current.add(early[i], 1);
            }
        for (const auto a : group) {
            budget.check();
            if (scalable)
                current.add(early[a], -1);
            try {
                auto trial = profile(a, dates(a, first[a] + 1));
                if (scalable)
                    current.fits(trial, &blocked[a]);
                else {
                    Timeline timeline(p, clock);
                    for (const auto i : group)
                        if (i != a)
                            timeline.add(early[i], 1);
                    timeline.fits(trial, &blocked[a]);
                }
            } catch (const CalculationCancelled &) {
                throw;
            } catch (const std::exception &) {
            }
            if (scalable)
                current.add(early[a], 1);
        }
    }
    for (auto &link : handovers) {
        const auto a = ids.at(link.predecessorId), b = ids.at(link.taskId), r = resources.at(link.resourceId);
        for (const auto &s : blocked[a])
            if (s.resource == r)
                for (const auto &t : early[b].spans)
                    if (t.resource == r && t.start < s.finish && t.finish > s.start)
                        link.driving = true;
    }
    for (std::size_t i = 0; i < n; ++i)
        if (affected[i] || !previous)
            result[i].drivingPredecessors.clear();
    for (const auto &r : rules) {
        if (!r.logical || (!affected[r.to] && previous) || early[r.from].row.completedActual ||
            early[r.to].row.completedActual)
            continue;
        const auto &d = *r.logical;
        const auto bound = calendar(r.to).shift(
            dependencyFromFinish(d.type) ? early[r.from].row.finish : early[r.from].row.start, d.lagMinutes);
        const auto required = execution[r.to]   ? execution[r.to]->required(bound, dependencyToFinish(d.type))
                              : !duration[r.to] ? (dependencyToFinish(d.type) ? calendar(r.to).pointEndAtMost(bound)
                                                                              : calendar(r.to).pointStartAtLeast(bound))
                              : dependencyToFinish(d.type) ? calendar(r.to).finishAtLeast(bound) - duration[r.to]
                                                           : calendar(r.to).startAtLeast(bound);
        const auto *fact = out.isForecast ? progressAsOf(p, p.tasks[r.from].id) : nullptr;
        if (required == first[r.to] && !(fact && fact->actualStart && !dependencyFromFinish(d.type)))
            result[r.to].drivingPredecessors.push_back(p.tasks[r.from].id);
    }
    std::vector<ResourceOrderLink> links;
    for (const auto &link : out.resourceOrderLinks)
        if (!affected[ids.at(link.taskId)])
            links.push_back(link);
    for (const auto &link : handovers)
        if (affected[ids.at(link.taskId)])
            links.push_back(link);
    out.tasks = std::move(result);
    if (scalable)
        out.levelingWitnesses = std::move(witnesses);
    out.resourceOrderLinks = std::move(links);
    out.levelingFloatCalculated = !logicalOnly;
    if (logicalOnly) {
        out.logicalFloatCalculated = true;
        if (out.isForecast)
            out.forecastFloatCalculated = true;
    }
    out.levelingFloatReason.clear();
    for (const auto suffix : {"/resource-order-float-v1", "/resource-segment-float-v2"})
        if (out.calculationPolicy.ends_with(suffix))
            out.calculationPolicy.resize(out.calculationPolicy.size() - std::string(suffix).size());
    out.calculationPolicy += logicalOnly ? "/work-contour-logical-float-v1"
                             : material  ? "/resource-material-float-v1"
                             : dated     ? "/resource-dated-float-v1"
                                         : "/resource-repartition-float-v3";
    if (material)
        out.diagnostics.push_back(
            {"MATERIAL_FLOAT_POLICY", {std::to_string(inventory->calls())}, {}, "MATERIAL_FLOAT_POLICY"});
    if (dated && !curvedWork && !material) {
        out.engineVersion = "ppm-dated-remaining-1.1";
        out.diagnostics.push_back({"LEVELING_DATED_FLOAT_POLICY", {}, {}, "LEVELING_DATED_FLOAT_POLICY"});
    }
    std::erase_if(out.diagnostics, [](const auto &d) {
        return d.code == "LEVELING_FLOAT_POLICY" || d.code == "LEVELING_SEGMENT_FLOAT_POLICY";
    });
    out.diagnostics.push_back({"LEVELING_REPARTITION_FLOAT_POLICY", {}, {}, "LEVELING_REPARTITION_FLOAT_POLICY"});
    out.diagnostics.insert(out.diagnostics.end(), proofs.begin(), proofs.end());
    if (optimized)
        out.diagnostics.push_back(
            {"LEVELING_REPARTITION_PROPAGATION_POLICY",
             {std::to_string(savedBounds), std::to_string(budget.candidates), std::to_string(branchExcluded),
              std::to_string(certificates), std::to_string(reused), std::to_string(pruneSkipped)},
             {},
             "LEVELING_REPARTITION_PROPAGATION_POLICY"});
    if (nonmonotone)
        out.diagnostics.push_back({"LEVELING_NONMONOTONE_PROPAGATION_POLICY",
                                   {std::to_string(mandatoryRemoved), std::to_string(supportExcluded),
                                    std::to_string(loadExcluded), std::to_string(propagationRounds),
                                    std::to_string(propagationFallbacks), std::to_string(propagationChecks)},
                                   {},
                                   "LEVELING_NONMONOTONE_PROPAGATION_POLICY"});
    if (costAware)
        out.diagnostics.push_back(
            {"LEVELING_PROPAGATION_COST_POLICY",
             {std::to_string(globalSkipped), std::to_string(unitsSkipped), std::to_string(predictedEnabled),
              std::to_string(observedEnabled), std::to_string(backoffs), std::to_string(estimatedChecks),
              std::to_string(estimatesRefined)},
             {},
             "LEVELING_PROPAGATION_COST_POLICY"});
}
} // namespace ppm
