#pragma once
#include "calculation/Effort.h"
#include "domain/RemainingWork.h"
namespace ppm {
struct RemainingWorkDay {
    std::string date;
    std::int64_t availableMinutes = 0, inheritedWork = 0;
    std::optional<std::int64_t> target;
    bool outside = false;
};
struct RemainingWorkDescription {
    TaskSchedule dates;
    std::vector<RemainingWorkDay> days;
    std::string storedStatusAt;
};
std::vector<WorkSpan> applyDatedRemainingWork(const std::optional<std::string> &statusAt,
                                              const std::vector<RemainingWorkPlan> &, const TaskSchedule &,
                                              const WorkCalendar &, const std::vector<WorkSpan> &availability,
                                              std::vector<WorkSpan> spans);
void validateRemainingWorkExecution(const Project &, const ScheduleResult &);
RemainingWorkDescription describeRemainingWork(const Project &, const std::string &taskId,
                                               const std::string &resourceId, std::shared_ptr<CalculationControl> = {});
} // namespace ppm
