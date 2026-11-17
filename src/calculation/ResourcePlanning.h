#pragma once
#include "calculation/PlannedWork.h"
#include "calculation/Effort.h"
#include "domain/Productivity.h"
namespace jobs {
NativeCurve mspWorkContour(int code);
Project materializeResourceWork(const Project &, const std::string &taskId, const std::string &resourceId,
                                const std::vector<PlannedWorkAmount> &);
Project applyNativeCurve(const Project &, const std::string &, const std::string &, NativeCurve,
                         std::optional<std::int64_t> totalWork = {});
bool nativeCurveCurrent(const Assignment &, std::int64_t duration, std::int64_t work);
struct CurveExport {
    NativeCurve definition;
    std::vector<WorkSpan> spans;
    TaskSchedule dates;
    bool exact = false;
};
CurveExport exportResourceCurve(const Project &, const Assignment &);
struct CurveTimephase {
    std::string start, finish;
    std::int64_t work = 0;
    int units = 0;
};
std::vector<CurveTimephase> curveTimephases(const Project &, const Assignment &, const CurveExport &);
} // namespace jobs
