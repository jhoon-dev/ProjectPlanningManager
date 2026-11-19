#include "reporting/GanttStyle.h"
#include "calculation/WorkCalendar.h"
namespace ppm {
GanttProgress ganttProgress(bool forecast, bool completed, const ProgressEntry *fact,
                            const std::optional<std::string> &statusAt, const TaskSchedule *planned) {
    if (!forecast)
        return GanttProgress::Plan;
    if (completed)
        return GanttProgress::Completed;
    if (!fact)
        return statusAt && planned && planned->start > *statusAt ? GanttProgress::Plan : GanttProgress::Unreported;
    return fact->actualStart ? GanttProgress::InProgress : GanttProgress::NotStarted;
}
QColor ganttFill(GanttProgress progress) {
    switch (progress) {
    case GanttProgress::InProgress:
        return QColor("#36a4ad");
    case GanttProgress::Completed:
        return QColor("#51b384");
    case GanttProgress::Unreported:
        return QColor("#929ba6");
    default:
        return QColor("#4997dd");
    }
}
QString ganttProgressText(GanttProgress progress) {
    switch (progress) {
    case GanttProgress::NotStarted:
        return "미착수 보고";
    case GanttProgress::InProgress:
        return "진행 보고";
    case GanttProgress::Completed:
        return "실제 완료";
    case GanttProgress::Unreported:
        return "미보고 · 착수 여부 미확인";
    default:
        return "계획 · 실적 상태 미반영";
    }
}
QColor ganttCriticalColor(bool dark) {
    return QColor(dark ? "#f19c91" : "#9e3f39");
}
QColor ganttDelayColor() {
    return QColor("#d88416");
}
std::optional<std::int64_t> ganttFinishVariance(const PlanInput &input, const Task &task,
                                                const std::string &baselineFinish, const std::string &finish) {
    try {
        WorkCalendar calendar(input, task);
        return calendar.offset(finish) - calendar.offset(baselineFinish);
    } catch (const std::exception &) {
        return {};
    }
}
} // namespace ppm
