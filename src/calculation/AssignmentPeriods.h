#pragma once
#include "domain/Project.h"
namespace ppm {
enum class AssignmentCurve { Flat, Front, Back, Bell };
// Materialized integer rates preserve defaultUnits * duration exactly on the task calendar.
std::vector<AssignmentPeriod> assignmentCurve(std::int64_t duration, int defaultUnits, AssignmentCurve);
} // namespace ppm
