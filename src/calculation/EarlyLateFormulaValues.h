#pragma once
#include "calculation/Scheduler.h"
#include "domain/XmlSchedule.h"
namespace jobs {
void addEarlyLateFormulaValues(const Project &, const ScheduleResult &, XmlScheduleValues &);
} // namespace jobs
