#include "calculation/LevelingFloat.h"
#include "calculation/ProgressPolicies.h"
#include "calculation/Effort.h"
#include "calculation/Constraints.h"
#include "calculation/Pdm.h"
#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace jobs {
namespace {
using Minute = std::int64_t;
const std::string &resume(const TaskSchedule &row) {
    return row.remainingStart ? *row.remainingStart : row.start;
}
[[noreturn]] void unavailable(const std::string &reason) {
    throw std::runtime_error("실행 구간 여유 계산: " + reason);
}
struct Part {
    std::size_t task;
    std::string resource;
    Minute begin, end;
    int units;
    bool openEnd;
    std::optional<std::string> fence;
};
struct Edge {
    std::size_t from, to;
    Dependency rule;
    std::optional<std::pair<std::size_t, std::size_t>> parts;
    int units = 0;
};
struct Event {
    std::string at;
    int kind; // release, capacity, acquire
    std::size_t part;
    int units;
};
struct Lot {
    int units;
    std::optional<std::size_t> owner;
};
} // namespace

void calculateSegmentLevelingFloat(const Project &source, ScheduleResult &out) {
    auto p = progressPolicyGraph(source);
    const auto n = p.tasks.size();
    if (n > 10000)
        unavailable("작업 한도 10,000개 초과");
    WorkCalendar clock(p);
    ExecutionInputs inputs(p);
    std::unordered_map<std::string, std::size_t> ids;
    for (std::size_t i = 0; i < n; ++i)
        ids.emplace(p.tasks[i].id, i);
    std::vector<TaskSchedule> early(n);
    for (const auto &row : out.tasks)
        early.at(ids.at(row.taskId)) = row;
    std::vector<std::unique_ptr<TaskExecution>> execution(n);
    std::vector<std::unique_ptr<WorkCalendar>> calendars(n);
    std::vector<Minute> first(n), last(n), durations(n);
    for (std::size_t i = 0; i < n; ++i) {
        calculationCheckpoint();
        if (isEffort(p)) {
            execution[i] = std::make_unique<TaskExecution>(p, out.isForecast ? pendingTask(p, p.tasks[i]) : p.tasks[i],
                                                           &inputs, out.isForecast);
            first[i] = execution[i]->offset(resume(early[i]));
        } else {
            calendars[i] = std::make_unique<WorkCalendar>(p, p.tasks[i]);
            const auto *fact = out.isForecast ? progressAsOf(p, p.tasks[i].id) : nullptr;
            durations[i] = fact ? fact->remainingMinutes : p.tasks[i].durationMinutes.value_or(0);
            first[i] =
                durations[i] ? calendars[i]->offset(resume(early[i])) : calendars[i]->pointOffset(resume(early[i]));
        }
    }
    const auto &calendar = [&](std::size_t i) -> const WorkCalendar & {
        return execution[i] ? execution[i]->calendar() : *calendars[i];
    };
    auto dates = [&](std::size_t i, Minute value) {
        if (value == first[i])
            return early[i];
        TaskSchedule row;
        if (execution[i])
            row = execution[i]->dates(value);
        else
            pdmDates(clock, calendar(i), row, value, durations[i]);
        row.taskId = p.tasks[i].id;
        if (out.isForecast) {
            row.remainingStart = row.start;
            if (const auto *fact = progressAsOf(p, row.taskId); fact && fact->actualStart)
                row.start = early[i].start;
        }
        return row;
    };
    std::vector<Part> parts;
    std::vector<std::vector<std::size_t>> taskParts(n);
    std::map<std::string, std::vector<Event>> events;
    for (const auto &span : resourceOccupancies(p, out, 100000)) {
        calculationCheckpoint();
        const auto i = ids.at(span.taskId), index = parts.size();
        parts.push_back({i,
                         span.resourceId,
                         clock.offset(span.start),
                         clock.offset(span.finish),
                         span.units,
                         span.finish == early[i].finish,
                         {}});
        if (parts.back().begin >= parts.back().end)
            unavailable("실행 구간의 날짜 좌표 불일치");
        taskParts[i].push_back(index);
        events[span.resourceId].push_back({span.start, 2, index, span.units});
        events[span.resourceId].push_back({span.finish, 0, index, span.units});
    }
    auto point = [&](std::size_t index, const TaskSchedule &row, bool finish) {
        const auto &part = parts[index];
        if (finish)
            return part.openEnd ? row.finish : std::min(row.finish, clock.boundary(part.end, true));
        return std::max(resume(row), clock.boundary(part.begin, false));
    };
    std::vector<Edge> edges;
    auto add = [&](Edge edge) {
        if (edges.size() >= 250000)
            unavailable("관계 한도 250,000개 초과");
        edges.push_back(std::move(edge));
    };
    for (std::size_t i = 0; i < n; ++i)
        if (!early[i].completedActual)
            for (const auto &d : dependencies(p.tasks[i]))
                add({ids.at(d.predecessorId), i, d, {}});
    // Fixed FIFO resource flow is between execution slices, never whole-task FS.
    for (const auto &resource : p.resources) {
        auto &list = events[resource.id];
        if (list.empty())
            continue;
        if (!resource.maxUnits)
            unavailable("자원 기본 가용량 미정: " + resource.id);
        for (const auto &exception : resource.exceptions) {
            if (!exception.units)
                unavailable("자원 가용량 미정: " + resource.id);
            list.push_back({exception.date + " 00:00", 1, 0, *exception.units});
            const auto day = std::chrono::sys_days{std::chrono::year{std::stoi(exception.date.substr(0, 4))} /
                                                   unsigned(std::stoi(exception.date.substr(5, 2))) /
                                                   unsigned(std::stoi(exception.date.substr(8, 2)))} +
                             std::chrono::days{1};
            const auto ymd = std::chrono::year_month_day{day};
            char tomorrow[32];
            std::snprintf(tomorrow, sizeof(tomorrow), "%04d-%02u-%02u 00:00", int(ymd.year()), unsigned(ymd.month()),
                          unsigned(ymd.day()));
            auto restored = resource.maxUnits;
            for (const auto &next : resource.exceptions)
                if (next.date == std::string(tomorrow).substr(0, 10))
                    restored = next.units;
            if (!restored)
                unavailable("자원 가용량 미정: " + resource.id);
            list.push_back({tomorrow, 1, 0, *restored});
        }
        std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
            return std::tie(a.at, a.kind, a.part) < std::tie(b.at, b.kind, b.part);
        });
        int capacity = *resource.maxUnits, busy = 0;
        std::deque<Lot> free{{capacity, {}}};
        for (const auto &event : list) {
            calculationCheckpoint();
            if (!event.kind) {
                busy -= event.units;
                free.push_back({event.units, event.part});
                continue;
            }
            if (event.kind == 1) {
                if (busy > event.units)
                    unavailable("현재 실행 구간의 자원 용량 초과");
                if (event.units > capacity)
                    free.push_back({event.units - capacity, {}});
                auto remove = std::max(0, capacity - event.units);
                while (remove && !free.empty()) {
                    auto &lot = free.front();
                    const auto count = std::min(remove, lot.units);
                    if (count && lot.owner) {
                        auto &fence = parts[*lot.owner].fence;
                        if (!fence || event.at < *fence)
                            fence = event.at;
                    }
                    lot.units -= count;
                    remove -= count;
                    if (!lot.units)
                        free.pop_front();
                }
                if (remove)
                    unavailable("자원 회수 흐름 불일치");
                capacity = event.units;
                continue;
            }
            if (busy + event.units > capacity)
                unavailable("현재 실행 구간의 자원 용량 초과");
            auto need = event.units;
            while (need && !free.empty()) {
                auto &lot = free.front();
                const auto count = std::min(need, lot.units);
                if (count && lot.owner && parts[*lot.owner].task != parts[event.part].task)
                    add({parts[*lot.owner].task,
                         parts[event.part].task,
                         {p.tasks[parts[*lot.owner].task].id, DependencyType::FS, 0},
                         std::pair{*lot.owner, event.part},
                         count});
                lot.units -= count;
                need -= count;
                if (!lot.units)
                    free.pop_front();
            }
            if (need)
                unavailable("자원 예약 흐름 불일치");
            busy += event.units;
        }
    }
    std::vector<std::vector<std::size_t>> successors(n), predecessors(n);
    for (std::size_t e = 0; e < edges.size(); ++e) {
        successors[edges[e].from].push_back(e);
        predecessors[edges[e].to].push_back(edges[e].from);
    }
    std::vector<std::map<std::string, int>> assigned(n);
    for (const auto &assignment : p.assignments)
        assigned[ids.at(assignment.taskId)][assignment.resourceId] = assignment.units;
    std::size_t checks = 0;
    auto checkpoint = [&] {
        calculationCheckpoint();
        if (++checks > 2000000)
            unavailable("후진 판정 한도 2,000,000회 초과");
    };
    // The reservation profile is a unary admissible-start set. It need not be
    // monotone when resource calendars differ: search its greatest member below
    // the monotone temporal bound, rather than binary-searching resource load.
    auto profileFits = [&](std::size_t i, const TaskSchedule &row) {
        checkpoint();
        std::map<std::string, std::map<Minute, std::pair<Minute, Minute>>> changes;
        for (const auto part : taskParts[i]) {
            const auto a = clock.offset(point(part, row, false)), b = clock.offset(point(part, row, true));
            if (a >= b)
                return false;
            changes[parts[part].resource][a].first += parts[part].units;
            changes[parts[part].resource][b].first -= parts[part].units;
        }
        if (execution[i]) {
            for (const auto &span : execution[i]->spans(row)) {
                changes[span.resourceId][span.start].second += span.units;
                changes[span.resourceId][span.finish].second -= span.units;
            }
        } else {
            for (const auto &[start, finish] : calendar(i).segments(resume(row), row.finish))
                for (const auto &[resource, units] : assigned[i]) {
                    changes[resource][clock.offset(start)].second += units;
                    changes[resource][clock.offset(finish)].second -= units;
                }
        }
        for (const auto &[resource, events] : changes) {
            Minute reserved = 0, actual = 0;
            for (const auto &[at, delta] : events) {
                calculationCheckpoint();
                reserved += delta.first;
                actual += delta.second;
                if (actual > reserved || reserved > assigned[i].at(resource))
                    return false;
            }
        }
        return true;
    };
    auto fits = [&](std::size_t i, const TaskSchedule &row, const std::vector<TaskSchedule> &targets, bool relations) {
        checkpoint();
        if (row.finish > *out.projectFinish || (hasConstraints(p) && timingViolation(p.tasks[i], row)))
            return false;
        for (const auto part : taskParts[i])
            if (point(part, row, false) >= point(part, row, true) ||
                (parts[part].fence && point(part, row, true) > *parts[part].fence))
                return false;
        if (relations)
            for (const auto e : successors[i]) {
                const auto &edge = edges[e];
                const auto &target = targets[edge.to];
                if (target.completedActual)
                    continue;
                if (edge.parts) {
                    if (point(edge.parts->first, row, true) > point(edge.parts->second, target, false))
                        return false;
                } else {
                    const auto bound = calendar(edge.to).shift(
                        dependencyFromFinish(edge.rule.type) ? row.finish : row.start, edge.rule.lagMinutes);
                    if (bound > (dependencyToFinish(edge.rule.type) ? target.finish : resume(target)))
                        return false;
                }
            }
        return true;
    };
    auto latest = [&](std::size_t i, Minute upper, const auto &targets, bool relations) {
        auto lo = first[i], hi = std::max(lo, upper);
        if (!fits(i, early[i], targets, relations))
            unavailable("현재 배치와 실행 구간 순서 불일치");
        while (lo < hi) {
            const auto mid = lo + (hi - lo + 1) / 2;
            bool feasible = false;
            try {
                feasible = fits(i, dates(i, mid), targets, relations);
            } catch (const CalculationCancelled &) {
                throw;
            } catch (const AppError &) {
                // Dates beyond the execution horizon are outside the search domain.
            }
            if (feasible)
                lo = mid;
            else
                hi = mid - 1;
        }
        for (;;) {
            const auto row = dates(i, lo);
            if (profileFits(i, row))
                return lo;
            if (lo == first[i])
                unavailable("현재 실행 구간의 예약/실제 수요 불일치");
            --lo;
        }
    };
    auto late = early;
    for (std::size_t i = 0; i < n; ++i)
        if (!early[i].completedActual) {
            const auto upper = execution[i]   ? execution[i]->offset(*out.projectFinish)
                               : durations[i] ? calendar(i).offset(*out.projectFinish)
                                              : calendar(i).pointOffset(*out.projectFinish);
            last[i] = latest(i, upper, late, false);
            late[i] = dates(i, last[i]);
        }
    std::deque<std::size_t> queue;
    std::vector<bool> queued(n, true);
    for (std::size_t i = n; i > 0; --i)
        queue.push_back(i - 1);
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
            for (const auto prev : predecessors[i])
                if (!queued[prev]) {
                    queue.push_back(prev);
                    queued[prev] = true;
                }
        }
    }
    auto lateCheck = out;
    lateCheck.tasks = late;
    for (const auto &resource : p.resources) {
        auto list = events[resource.id];
        if (list.empty())
            continue;
        for (auto &event : list)
            if (event.kind != 1)
                event.at = point(event.part, late[parts[event.part].task], event.kind == 0);
        std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
            return std::tie(a.at, a.kind, a.part) < std::tie(b.at, b.kind, b.part);
        });
        int capacity = *resource.maxUnits, busy = 0;
        for (const auto &event : list) {
            checkpoint();
            if (event.kind == 1)
                capacity = event.units;
            else
                busy += event.kind == 0 ? -event.units : event.units;
            if (busy < 0 || busy > capacity)
                unavailable("최후 실행 창 예약의 자원 가용량 검증 실패");
        }
    }
    if (resourceLoad(p, lateCheck).status != CalculationStatus::Valid)
        unavailable("최후 실행 배치의 자원 가용량 검증 실패");
    auto result = early;
    for (std::size_t i = 0; i < n; ++i)
        if (!early[i].completedActual) {
            auto &row = result[i];
            const auto free = dates(i, latest(i, last[i], early, true));
            row.totalFloatMinutes = calendar(i).offset(resume(late[i])) - calendar(i).offset(resume(early[i]));
            row.freeFloatMinutes = calendar(i).offset(resume(free)) - calendar(i).offset(resume(early[i]));
            row.lateStartMinutes = clock.offset(resume(late[i]));
            row.lateFinishMinutes = clock.offset(late[i].finish);
            row.critical = row.totalFloatMinutes == 0;
            row.drivingPredecessors.clear();
        }
    std::vector<ResourceOrderLink> links;
    for (const auto &edge : edges) {
        if (early[edge.from].completedActual || early[edge.to].completedActual)
            continue;
        if (edge.parts) {
            const auto [from, to] = *edge.parts;
            const auto acquire = point(to, early[edge.to], false);
            const bool driving = point(from, dates(edge.from, first[edge.from] + 1), true) > acquire;
            links.push_back({p.tasks[edge.from].id, p.tasks[edge.to].id, parts[from].resource, edge.units, driving,
                             point(from, early[edge.from], true), acquire});
        } else {
            const auto bound = calendar(edge.to).shift(dependencyFromFinish(edge.rule.type) ? early[edge.from].finish
                                                                                            : early[edge.from].start,
                                                       edge.rule.lagMinutes);
            const auto required =
                execution[edge.to] ? execution[edge.to]->required(bound, dependencyToFinish(edge.rule.type))
                : !durations[edge.to]
                    ? (dependencyToFinish(edge.rule.type) ? calendar(edge.to).pointEndAtMost(bound)
                                                          : calendar(edge.to).pointStartAtLeast(bound))
                    : (dependencyToFinish(edge.rule.type) ? calendar(edge.to).finishAtLeast(bound) - durations[edge.to]
                                                          : calendar(edge.to).startAtLeast(bound));
            if (required == first[edge.to] &&
                !(out.isForecast && !dependencyFromFinish(edge.rule.type) && progressAsOf(p, p.tasks[edge.from].id) &&
                  progressAsOf(p, p.tasks[edge.from].id)->actualStart))
                result[edge.to].drivingPredecessors.push_back(p.tasks[edge.from].id);
        }
    }
    out.tasks = std::move(result);
    out.resourceOrderLinks = std::move(links);
    out.levelingFloatCalculated = true;
    out.calculationPolicy += "/resource-segment-float-v2";
    out.diagnostics.push_back({"LEVELING_SEGMENT_FLOAT_POLICY", {}, {}, "LEVELING_SEGMENT_FLOAT_POLICY"});
}
} // namespace jobs
