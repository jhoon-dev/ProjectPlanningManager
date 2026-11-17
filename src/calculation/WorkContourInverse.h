#pragma once
#include "calculation/Effort.h"
namespace jobs {
void scaleWorkContours(Project &, std::size_t, std::int64_t work, std::int64_t duration, const TaskSchedule &);
void changeAssignmentContour(Project &, const std::string &, const std::string &, std::vector<AssignmentPeriod>);
void certifyWorkContourDurations(const Project &, const std::vector<std::size_t> &);
} // namespace jobs
