#pragma once
#include "domain/XmlSchedule.h"
namespace jobs {
// Called only after the selected frozen schedule has been validated.
void addBaselineCostFormulaValues(const Project &, const Baseline &, XmlScheduleValues &);
} // namespace jobs
