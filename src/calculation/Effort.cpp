#include "domain/Diagnostics.h"
#include "calculation/Effort.h"
#include "calculation/DatedRemainingWork.h"
#include "calculation/Constraints.h"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <numeric>
#include <tuple>
namespace jobs {
using Minute = std::int64_t;
ExecutionInputs::ExecutionInputs(const PlanInput &p) {
    for (const auto &a : p.assignments) {
        calculationCheckpoint();
        assignments[a.taskId].push_back(a);
    }
    for (const auto &r : p.resources)
        resources[r.id] = &r;
    for (const auto &t : p.tasks)
        durations[t.id] = t.durationMinutes.value_or(0);
}
TaskExecution::TaskExecution(const PlanInput &p, const Task &t, const ExecutionInputs *inputs)
    : task_(t), clock_(p), calendar_(p, t) {
    EvaluationScope evaluation;
    calculationCheckpoint();
    std::vector<Assignment> assigned;
    if (inputs) {
        auto it = inputs->assignments.find(t.id);
        if (it != inputs->assignments.end())
            assigned = it->second;
    } else
        for (const auto &a : p.assignments)
            if (a.taskId == t.id)
                assigned.push_back(a);
    profiles_ = assigned;
    if (usesWork(t) &&
        std::any_of(assigned.begin(), assigned.end(), [](const auto &a) { return !a.periods.empty(); })) {
        bool same = true;
        for (const auto &a : assigned) {
            const auto r = std::find_if(p.resources.begin(), p.resources.end(),
                                        [&](const auto &v) { return v.id == a.resourceId; });
            same = same && r != p.resources.end() && r->calendarId == t.calendarId;
        }
        if (same) {
            std::map<Minute, Minute> events;
            for (const auto &a : assigned) {
                events[0] += a.units;
                for (const auto &v : a.periods) {
                    events[v.fromMinute] += v.units - a.units;
                    events[v.toMinute] += a.units - v.units;
                }
            }
            Minute rate = 0, work = 0, last = 0;
            const auto target = t.workUnitMinutes.value_or(0);
            if (!target)
                contourDuration_ = 0;
            for (const auto &[at, delta] : events) {
                if (contourDuration_)
                    break;
                if (rate && work + (at - last) * rate >= target)
                    contourDuration_ = last + (target - work + rate - 1) / rate;
                work += (at - last) * rate;
                rate += delta;
                last = at;
            }
            if (!contourDuration_ && rate)
                contourDuration_ = last + (target - work + rate - 1) / rate;
        }
    }
    if (inputs)
        planDuration_ = inputs->durations.at(t.id);
    else
        for (const auto &original : p.tasks)
            if (original.id == t.id)
                planDuration_ = original.durationMinutes.value_or(0);
    std::string key = "execution-grid/";
    auto field = [&](const std::string &value) { key += std::to_string(value.size()) + ":" + value; };
    field(calendar_.cacheKey());
    std::vector<WorkCalendar> resourceCalendars;
    for (const auto &a : assigned) {
        const Resource *resource = nullptr;
        if (inputs)
            resource = inputs->resources.at(a.resourceId);
        else
            for (const auto &r : p.resources)
                if (r.id == a.resourceId) {
                    resource = &r;
                    break;
                }
        if (!resource)
            throw AppError(ErrorCode::WORK_RESOURCE_REQUIRED, {});
        Task resourceTask;
        resourceTask.calendarId = resource->calendarId;
        resourceCalendars.emplace_back(p, resourceTask);
        field(a.resourceId);
        field(std::to_string(a.units));
        field(resourceCalendars.back().cacheKey());
    }
    if (auto cached = evaluationCached(key)) {
        grid_ = std::static_pointer_cast<const Grid>(cached);
        return;
    }
    auto grid = std::make_shared<Grid>();
    const auto taskTimes = calendar_.civilIntervals();
    std::map<Minute, Minute> changes;
    for (std::size_t assignment = 0; assignment < assigned.size(); ++assignment) {
        const auto &a = assigned[assignment];
        const auto resourceTimes = resourceCalendars[assignment].civilIntervals();
        std::size_t i = 0, j = 0;
        while (i < taskTimes.size() && j < resourceTimes.size()) {
            calculationCheckpoint();
            const auto s = std::max(taskTimes[i].first, resourceTimes[j].first),
                       e = std::min(taskTimes[i].second, resourceTimes[j].second);
            if (s < e) {
                grid->availability.push_back({a.resourceId, s, e, a.units});
                changes[s] += a.units;
                changes[e] -= a.units;
            }
            if (taskTimes[i].second < resourceTimes[j].second)
                ++i;
            else
                ++j;
        }
    }
    Minute units = 0, prefix = 0, last = 0;
    for (const auto &[at, delta] : changes) {
        calculationCheckpoint();
        if (units && at > last) {
            grid->weighted.push_back({last, at, units, prefix});
            prefix += (at - last) * units;
        }
        units += delta;
        last = at;
    }
    // Each resource's calendar intersection is non-overlapping and ordered.
    // Keep that order searchable so a short task does not scan the twenty-year horizon.
    std::sort(grid->availability.begin(), grid->availability.end(), [](const auto &a, const auto &b) {
        return std::tie(a.resourceId, a.start) < std::tie(b.resourceId, b.start);
    });
    grid_ = std::move(grid);
    evaluationRemember(std::move(key), grid_);
}
Minute TaskExecution::integrated(Minute civil) const {
    const auto &weighted = grid_->weighted;
    auto it =
        std::upper_bound(weighted.begin(), weighted.end(), civil, [](Minute n, const auto &w) { return n < w.start; });
    if (it == weighted.begin())
        return 0;
    --it;
    return it->prefix + std::clamp(civil - it->start, Minute{0}, it->finish - it->start) * it->units;
}
TaskSchedule TaskExecution::dates(Minute local) const {
    calculationCheckpoint();
    if (datedInverse_)
        return datedExecution(local).first;
    const auto &weighted = grid_->weighted;
    TaskSchedule row;
    row.taskId = task_.id;
    const auto amount = usesWork(task_) ? task_.workUnitMinutes.value() : task_.durationMinutes.value();
    row.start = amount == 0 ? calendar_.pointBoundary(local) : calendar_.boundary(local, false);
    row.finish = row.start;
    if (amount > 0 && usesWork(task_) && grid_->availability.empty())
        throw AppError(ErrorCode::WORK_CALENDAR_NO_INTERSECTION);
    if (amount > 0 && contourDuration_) {
        if (*contourDuration_ > calendar_.maxOffset() - local)
            throw AppError(ErrorCode::WORK_HORIZON_EXHAUSTED);
        row.finish = calendar_.boundary(local + *contourDuration_, true);
    } else if (amount > 0 && usesWork(task_) &&
               std::any_of(profiles_.begin(), profiles_.end(), [](const auto &a) { return !a.periods.empty(); })) {
        const auto limit = calendar_.maxOffset() - local;
        Minute base = 0;
        for (const auto &a : profiles_)
            base += a.units;
        auto width = std::min(limit, std::max(Minute{1}, amount / std::max(Minute{1}, base) + 1));
        for (;;) {
            calculationCheckpoint();
            if (width <= 0)
                throw AppError(ErrorCode::WORK_HORIZON_EXHAUSTED);
            row.finish = calendar_.boundary(local + width, true);
            const auto spans = profileSpans(row);
            std::map<Minute, Minute> events;
            Minute sum = 0;
            for (const auto &s : spans) {
                sum += (s.finish - s.start) * s.units;
                events[s.start] += s.units;
                events[s.finish] -= s.units;
            }
            if (sum >= amount) {
                Minute used = 0, work = 0, last = clock_.offset(row.start);
                for (const auto &[at, delta] : events) {
                    calculationCheckpoint();
                    if (used && work + (at - last) * used >= amount) {
                        row.finish = clock_.boundary(last + (amount - work + used - 1) / used, true);
                        break;
                    }
                    work += (at - last) * used;
                    used += delta;
                    last = at;
                }
                break;
            }
            if (width == limit)
                throw AppError(ErrorCode::WORK_HORIZON_EXHAUSTED);
            width = std::min(limit, width * 2);
        }
    } else if (amount > 0 && usesWork(task_)) {
        const auto first = clock_.offset(row.start);
        auto it = std::lower_bound(weighted.begin(), weighted.end(), first,
                                   [](const auto &w, Minute n) { return w.finish <= n; });
        if (it == weighted.end())
            throw AppError(ErrorCode::WORK_CALENDAR_NO_INTERSECTION, {});
        const auto begin = std::max(first, it->start), target = integrated(begin) + amount;
        row.start = clock_.boundary(begin, false);
        auto end = std::lower_bound(weighted.begin(), weighted.end(), target, [](const auto &w, Minute n) {
            return w.prefix + (w.finish - w.start) * w.units < n;
        });
        if (end == weighted.end())
            throw AppError(ErrorCode::WORK_HORIZON_EXHAUSTED, {});
        const auto finish = end->start + (target - end->prefix + end->units - 1) / end->units;
        row.finish = clock_.boundary(finish, true);
    } else if (amount > 0)
        row.finish = calendar_.boundary(local + amount, true);
    row.earlyStartMinutes = clock_.offset(row.start);
    row.earlyFinishMinutes = clock_.offset(row.finish);
    return row;
}
Minute TaskExecution::required(const std::string &bound, bool finish) const {
    if (usesWork(task_) ? task_.workUnitMinutes.value() == 0 : task_.durationMinutes.value() == 0)
        return calendar_.pointStartAtLeast(bound);
    if (!finish)
        return calendar_.startAtLeast(bound);
    if (usesWork(task_) && grid_->availability.empty())
        throw AppError(ErrorCode::WORK_CALENDAR_NO_INTERSECTION);
    if (datedInverse_ ||
        (usesWork(task_) && !contourDuration_ &&
         std::any_of(profiles_.begin(), profiles_.end(), [](const auto &a) { return !a.periods.empty(); }))) {
        // Relative contours on different calendars need not have monotone finishes.
        Minute first = 0, limit = calendar_.maxOffset();
        if (datedInverse_) {
            if (statusAt_)
                first = calendar_.startAtLeast(*statusAt_);
            // A fixed target date cannot survive a candidate starting after that day.
            for (const auto &p : remainingWork_)
                limit = std::min(limit, calendar_.offset(p.days.front().date + " 23:59"));
        }
        for (Minute at = first; at <= limit; ++at) {
            calculationCheckpoint();
            if (at - first > 2000000)
                throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID, {"곡선 완료 역산 한도 2,000,000회 초과"});
            try {
                if (dates(at).finish >= bound)
                    return at;
            } catch (const AppError &e) {
                if (e.code() != "WORK_HORIZON_EXHAUSTED" && e.code() != "WORK_CALENDAR_NO_INTERSECTION" &&
                    !(datedInverse_ && e.code() == "REMAINING_WORK_INVALID"))
                    throw;
            }
        }
        throw AppError(ErrorCode::WORK_HORIZON_EXHAUSTED);
    }
    // Finish-driven edges need an inverse of the work integral, not F - nominal D.
    Minute lo = 0, hi = calendar_.maxOffset();
    while (lo < hi) {
        auto mid = lo + (hi - lo) / 2;
        bool reached = true;
        try {
            reached = dates(mid).finish >= bound;
        } catch (const std::exception &) {
        }
        if (reached)
            hi = mid;
        else
            lo = mid + 1;
    }
    return lo;
}
Minute TaskExecution::offset(const std::string &stamp) const {
    const bool zero = usesWork(task_) ? task_.workUnitMinutes.value() == 0 : task_.durationMinutes.value() == 0;
    return zero ? calendar_.pointOffset(stamp) : calendar_.offset(stamp);
}
TaskExecution::TaskExecution(const Project &p, const Task &t, const ExecutionInputs *inputs, bool forecast)
    : TaskExecution(static_cast<const PlanInput &>(p), t, inputs) {
    statusAt_ = p.statusAt;
    for (const auto &v : p.remainingWork)
        if (v.taskId == t.id)
            remainingWork_.push_back(v);
    datedInverse_ = forecast && usesWork(t) && !remainingWork_.empty();
}
std::vector<WorkSpan> TaskExecution::profileSpans(const TaskSchedule &row) const {
    if (row.completedActual && !remainingWork_.empty())
        throw AppError(ErrorCode::REMAINING_WORK_INVALID, {"완료 작업의 날짜별 잔여 공수를 해제하세요: " + row.taskId},
                       {}, "DATED_REMAINING_INVALID");
    if (row.completedActual)
        return {};
    const auto begin = clock_.offset(row.remainingStart.value_or(row.start)), end = clock_.offset(row.finish);
    std::vector<WorkSpan> out;
    for (const auto &profile : profiles_) {
        calculationCheckpoint();
        const auto first = std::lower_bound(grid_->availability.begin(), grid_->availability.end(), profile.resourceId,
                                            [](const auto &a, const auto &id) { return a.resourceId < id; });
        const auto last = std::upper_bound(first, grid_->availability.end(), profile.resourceId,
                                           [](const auto &id, const auto &a) { return id < a.resourceId; });
        auto at = std::lower_bound(first, last, begin, [](const auto &a, Minute x) { return a.finish <= x; });
        for (; at != last && at->start < end; ++at)
            out.push_back({at->resourceId, std::max(begin, at->start), std::min(end, at->finish), at->units});
    }
    if (end > begin &&
        std::any_of(profiles_.begin(), profiles_.end(), [](const auto &a) { return !a.periods.empty(); })) {
        const auto origin = calendar_.offset(row.remainingStart.value_or(row.start));
        const auto remaining = calendar_.offset(row.finish) - origin;
        const auto shift = row.remainingStart && !usesWork(task_) ? std::max(Minute{0}, planDuration_ - remaining) : 0;
        std::vector<WorkSpan> shaped;
        for (const auto &span : out) {
            calculationCheckpoint();
            const auto &profile = *std::find_if(profiles_.begin(), profiles_.end(),
                                                [&](const auto &a) { return a.resourceId == span.resourceId; });
            const auto local = calendar_.offset(clock_.boundary(span.start, false)) - origin + shift;
            auto at = local;
            const auto finish = local + span.finish - span.start;
            auto period = std::lower_bound(profile.periods.begin(), profile.periods.end(), at,
                                           [](const auto &v, Minute x) { return v.toMinute <= x; });
            while (at < finish) {
                calculationCheckpoint();
                auto next = finish;
                int units = profile.units;
                if (period != profile.periods.end()) {
                    if (at < period->fromMinute)
                        next = std::min(next, period->fromMinute);
                    else {
                        next = std::min(next, period->toMinute);
                        units = period->units;
                    }
                }
                if (units)
                    shaped.push_back({span.resourceId, span.start + at - local, span.start + next - local, units});
                at = next;
                if (period != profile.periods.end() && at >= period->toMinute)
                    ++period;
            }
        }
        out = std::move(shaped);
    }
    return out;
}
std::vector<WorkSpan> TaskExecution::spans(const TaskSchedule &row) const {
    if (row.remainingStart && !row.completedActual && usesWork(task_) && !remainingWork_.empty()) {
        const auto result = datedExecution(calendar_.offset(*row.remainingStart));
        if (result.first.finish != row.finish)
            throw AppError(ErrorCode::WORK_RESULT_MISMATCH);
        return result.second;
    }
    auto out = profileSpans(row);
    if (row.completedActual)
        return out;
    const auto begin = clock_.offset(row.remainingStart.value_or(row.start)), end = clock_.offset(row.finish);
    if (usesWork(task_) && end > begin) {
        const auto target = *task_.workUnitMinutes;
        Minute before = 0, total = 0;
        for (const auto &s : out) {
            before += std::max(Minute{0}, std::min(s.finish, end - 1) - s.start) * s.units;
            total += std::max(Minute{0}, s.finish - std::max(s.start, end - 1)) * s.units;
        }
        const auto last = target - before;
        if (last < 0 || last > total)
            throw AppError(ErrorCode::WORK_RESULT_MISMATCH, {});
        if (last < total) {
            std::vector<WorkSpan> kept, tail;
            Minute allocated = 0;
            for (const auto &a : out) {
                if (a.finish < end) {
                    kept.push_back(a);
                    continue;
                }
                if (a.start < end - 1)
                    kept.push_back({a.resourceId, a.start, end - 1, a.units});
                const int units = int(Minute(a.units) * last / total);
                allocated += units;
                tail.push_back({a.resourceId, end - 1, end, units});
            }
            std::sort(tail.begin(), tail.end(),
                      [](const auto &a, const auto &b) { return a.resourceId < b.resourceId; });
            for (auto &a : tail)
                if (allocated < last) {
                    ++a.units;
                    ++allocated;
                }
            for (const auto &a : tail)
                if (a.units)
                    kept.push_back(a);
            out = std::move(kept);
        }
    }
    out = applyDatedRemainingWork(statusAt_, remainingWork_, row, clock_, grid_->availability, std::move(out));
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        return std::tie(a.resourceId, a.start) < std::tie(b.resourceId, b.start);
    });
    return out;
}
Minute TaskExecution::work(const TaskSchedule &row) const {
    Minute total = 0;
    for (const auto &a : spans(row))
        total += (a.finish - a.start) * a.units;
    return total;
}
Minute TaskExecution::duration(const TaskSchedule &row) const {
    return calendar_.offset(row.finish) - calendar_.offset(row.remainingStart.value_or(row.start));
}
Task pendingTask(const Project &p, const Task &t) {
    auto pending = t;
    if (!p.statusAt)
        return pending;
    const auto *e = progressAsOf(p, t.id);
    if (!e)
        return pending;
    if (usesWork(t)) {
        if (e->actualFinish)
            pending.workUnitMinutes = 0;
        else if (e->remainingWorkUnitMinutes)
            pending.workUnitMinutes = e->remainingWorkUnitMinutes;
        else
            throw AppError(ErrorCode::REMAINING_WORK_REQUIRED, {diagnosticArgument(t.name)});
    } else {
        if (!e->actualFinish && e->remainingMinutes == 0 && e->remainingWorkUnitMinutes)
            throw AppError(ErrorCode::REMAINING_DURATION_REQUIRED, {diagnosticArgument(t.name)});
        pending.durationMinutes = e->remainingMinutes;
    }
    return pending;
}
std::vector<WorkSpan> executionSpans(const PlanInput &p, const Task &t, const TaskSchedule &row) {
    return TaskExecution(p, t).spans(row);
}
std::vector<WorkSpan> executionSpans(const Project &p, const Task &t, const TaskSchedule &row) {
    return TaskExecution(p, t).spans(row);
}
} // namespace jobs
