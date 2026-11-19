#pragma once
#include "calculation/Scheduler.h"
#include "calculation/WorkCalendar.h"
#include <functional>

namespace ppm {
// Adapters reuse the forward engine's pending execution models and calendars.
struct RemainingExecution {
    const WorkCalendar *calendar;
    std::function<TaskSchedule(std::int64_t)> dates;
    std::function<std::int64_t(const std::string &)> offset;
    std::function<std::int64_t(const std::string &, bool)> required;
};
void calculateRemainingFloat(const Project &, ScheduleResult &, const std::vector<std::size_t> &order,
                             const std::vector<RemainingExecution> &);
} // namespace ppm
