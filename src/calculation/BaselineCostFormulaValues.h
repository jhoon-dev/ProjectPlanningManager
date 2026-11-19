#pragma once
#include "domain/XmlSchedule.h"
namespace ppm {
// Called only after the selected frozen schedule has been validated.
void addBaselineCostFormulaValues(const Project &, const Baseline &, XmlScheduleValues &);
} // namespace ppm
