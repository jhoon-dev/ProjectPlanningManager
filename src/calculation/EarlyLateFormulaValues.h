#pragma once
#include "calculation/Scheduler.h"
#include "domain/XmlSchedule.h"
namespace ppm {
void addEarlyLateFormulaValues(const Project &, const ScheduleResult &, XmlScheduleValues &);
} // namespace ppm
