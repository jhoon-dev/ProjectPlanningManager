#pragma once
#include "calculation/Scheduler.h"
namespace ppm {
struct IgnoredProgressRelation {
    std::string predecessorId, taskId;
    DependencyType type = DependencyType::FS;
    std::int64_t lagMinutes = 0;
};
struct ProgressPolicyTask {
    std::string id, name;
    std::optional<std::string> actualStart, actualFinish;
    std::optional<TaskSchedule> retained, overridden, actualDates;
    std::optional<std::int64_t> finishDeltaMinutes, resumeDeltaMinutes;
    bool comparable = false, changed = false;
};
struct ProgressPolicyComparison {
    std::string documentId, name, statusAt, blockingReason;
    std::uint64_t revision = 0;
    bool computed = false;
    ScheduleResult originalPlan, retained, overridden, actualDates;
    std::vector<ProgressPolicyTask> tasks;
    std::vector<IgnoredProgressRelation> ignored, actualDatesIgnored;
    std::size_t changedTasks = 0, uncomparedTasks = 0;
};
ScheduleResult selectedProgressSchedule(const Project &);
// Private execution graph; source relationships and policy metadata remain in the caller.
Project progressPolicyGraph(const Project &);
// A read-only what-if. Neither output is adopted into the source Document.
ProgressPolicyComparison compareProgressPolicies(const Project &, const std::shared_ptr<CalculationControl> & = {});
} // namespace ppm
