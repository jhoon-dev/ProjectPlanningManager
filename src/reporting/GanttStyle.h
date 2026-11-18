#pragma once
#include "domain/Project.h"
#include <QColor>
#include <QString>
namespace jobs {
enum class GanttProgress { Plan, NotStarted, InProgress, Completed, Unreported };
GanttProgress ganttProgress(bool forecast, bool completed, const ProgressEntry *,
                            const std::optional<std::string> &statusAt, const TaskSchedule *planned);
QColor ganttFill(GanttProgress);
QString ganttProgressText(GanttProgress);
QColor ganttCriticalColor(bool dark = false);
QColor ganttDelayColor();
std::optional<std::int64_t> ganttFinishVariance(const PlanInput &, const Task &, const std::string &baselineFinish,
                                                const std::string &finish);
} // namespace jobs
