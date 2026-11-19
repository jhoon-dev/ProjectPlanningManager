#pragma once
#include "calculation/Scheduler.h"
namespace ppm {
struct ActivityInterval {
    std::string id, name, kind, reason;
    std::optional<std::string> start, finish;
    std::optional<std::int64_t> durationMinutes;
    std::size_t sourceTasks = 0;
};
struct ActivityProgress {
    // Work uses explicitly reported period work and remaining work; unreported work is not inferred.
    std::optional<int> physical, duration, work;
    std::string physicalSource;
};
std::vector<ActivityInterval> activityIntervals(const Project &, const ScheduleResult &);
ActivityProgress activityProgress(const Project &, const std::string &taskId);
} // namespace ppm
