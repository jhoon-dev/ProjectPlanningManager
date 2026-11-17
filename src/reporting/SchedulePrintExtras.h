#pragma once
#include "reporting/SchedulePrint.h"
namespace jobs {
void prepareScheduleExtras(SchedulePrintLayout &, const ScheduleResult &);
void paintScheduleRelations(QPainter &, const SchedulePrintLayout &, const SchedulePrintPage &);
} // namespace jobs
