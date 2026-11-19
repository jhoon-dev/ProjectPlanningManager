#pragma once
#include "domain/Project.h"
namespace ppm {
bool xmlCalendarFormula(const XmlFormula &);
bool hasXmlCalendarFunctions(const PlanInput &, bool activeOnly = false);
std::string xmlCalendarInputKey(const PlanInput &, const Task *, const Resource *, const Assignment *);
// Values are fixed-six minutes; dates are timezone-free ISO civil instants.
std::int64_t xmlCalendarDifference(const PlanInput &, FieldScope, const Task *, const Resource *,
                                   const std::string &calendar, const std::string &from, const std::string &to);
std::string xmlCalendarShift(const PlanInput &, FieldScope, const Task *, const Resource *, const std::string &calendar,
                             const std::string &from, std::int64_t minutes, bool elapsed);
} // namespace ppm
