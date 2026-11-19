#include "calculation/ForecastFloat.h"
#include "calculation/Constraints.h"
#include <algorithm>
#include <unordered_map>

namespace ppm {
void calculateRemainingFloat(const Project &p, ScheduleResult &out, const std::vector<std::size_t> &order,
                             const std::vector<RemainingExecution> &models) {
    if (out.status != CalculationStatus::Valid || !out.projectFinish)
        return;
    const auto n = p.tasks.size();
    std::unordered_map<std::string, std::size_t> ids;
    std::vector<bool> started(n);
    std::vector<std::int64_t> early(n);
    std::vector<std::vector<std::pair<std::size_t, Dependency>>> successors(n);
    for (std::size_t i = 0; i < n; ++i) {
        calculationCheckpoint();
        ids.emplace(p.tasks[i].id, i);
        const auto *actual = progressAsOf(p, p.tasks[i].id);
        started[i] = actual && actual->actualStart.has_value();
        auto &row = out.tasks[i];
        // No future float applies to observed completed work.
        row.lateStartMinutes = row.lateFinishMinutes = row.totalFloatMinutes = row.freeFloatMinutes = 0;
        row.critical = false;
        row.drivingPredecessors.clear();
        if (!row.completedActual)
            early[i] = models[i].offset(row.remainingStart.value_or(row.start));
    }
    for (std::size_t i = 0; i < n; ++i)
        if (!out.tasks[i].completedActual)
            for (const auto &d : dependencies(p.tasks[i]))
                successors[ids.at(d.predecessorId)].push_back({i, d});
    auto late = out.tasks;
    auto trialAt = [&](std::size_t i, std::int64_t local) {
        // Preserve the exact forward boundary, including non-working milestones.
        if (local == early[i])
            return out.tasks[i];
        auto trial = models[i].dates(local);
        trial.remainingStart = trial.start;
        if (started[i])
            trial.start = out.tasks[i].start;
        return trial;
    };
    auto fits = [&](std::size_t i, const TaskSchedule &trial, const std::vector<TaskSchedule> &targets) {
        if (trial.finish > *out.projectFinish || (hasConstraints(p) && timingViolation(p.tasks[i], trial)))
            return false;
        for (const auto &[j, d] : successors[i]) {
            const auto &target = targets[j];
            const auto bound =
                models[j].calendar->shift(dependencyFromFinish(d.type) ? trial.finish : trial.start, d.lagMinutes);
            if (bound > (dependencyToFinish(d.type) ? target.finish : target.remainingStart.value_or(target.start)))
                return false;
        }
        return true;
    };
    auto latest = [&](std::size_t i, const std::vector<TaskSchedule> &targets) {
        auto lo = early[i], hi = std::max(lo, models[i].offset(*out.projectFinish));
        while (lo < hi) {
            calculationCheckpoint();
            const auto mid = lo + (hi - lo + 1) / 2;
            bool feasible = false;
            try {
                feasible = fits(i, trialAt(i, mid), targets);
            } catch (const std::exception &) {
                // A trial beyond the execution horizon is not a failed forecast.
            }
            if (feasible)
                lo = mid;
            else
                hi = mid - 1;
        }
        return trialAt(i, lo);
    };
    WorkCalendar clock(p);
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        calculationProgress(CalculationStage::Backward, std::uint64_t(it - order.rbegin()), n);
        const auto i = *it;
        auto &row = out.tasks[i];
        if (row.completedActual)
            continue;
        late[i] = latest(i, late);
        const auto free = latest(i, out.tasks);
        const auto &cal = *models[i].calendar;
        const auto &resume = row.remainingStart.value_or(row.start);
        row.lateStartMinutes = clock.offset(late[i].remainingStart.value_or(late[i].start));
        row.lateFinishMinutes = clock.offset(late[i].finish);
        row.totalFloatMinutes = cal.offset(late[i].remainingStart.value_or(late[i].start)) - cal.offset(resume);
        row.freeFloatMinutes = cal.offset(free.remainingStart.value_or(free.start)) - cal.offset(resume);
        row.critical = row.totalFloatMinutes == 0;
    }
    for (std::size_t i = 0; i < n; ++i) {
        calculationCheckpoint();
        auto &row = out.tasks[i];
        if (row.completedActual)
            continue;
        for (const auto &d : dependencies(p.tasks[i])) {
            const auto j = ids.at(d.predecessorId);
            const auto &prev = out.tasks[j];
            if (prev.completedActual || (!dependencyFromFinish(d.type) && started[j]))
                continue;
            const auto bound =
                models[i].calendar->shift(dependencyFromFinish(d.type) ? prev.finish : prev.start, d.lagMinutes);
            if (models[i].required(bound, dependencyToFinish(d.type)) == early[i])
                row.drivingPredecessors.push_back(d.predecessorId);
        }
    }
    out.forecastFloatCalculated = true;
    out.calculationPolicy += "/remaining-float-v1";
    out.diagnostics.push_back({"FORECAST_FLOAT_POLICY", {}, {}, "FORECAST_FLOAT_POLICY"});
}
} // namespace ppm
