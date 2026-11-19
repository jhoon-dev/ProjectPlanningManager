#pragma once
#include "reporting/SchedulePrint.h"
namespace ppm {
void prepareScheduleExtras(SchedulePrintLayout &, const ScheduleResult &);
void paintScheduleRelations(QPainter &, const SchedulePrintLayout &, const SchedulePrintPage &);
} // namespace ppm
