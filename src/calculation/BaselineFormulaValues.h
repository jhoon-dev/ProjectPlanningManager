#pragma once
#include "domain/XmlSchedule.h"
namespace ppm {
void addBaselineFormulaValues(const Project &, XmlScheduleValues &);
std::shared_ptr<const XmlScheduleValues> baselineExchangeValues(const Project &, int slot);
} // namespace ppm
