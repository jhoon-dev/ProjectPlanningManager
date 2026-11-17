#include "application/Document.h"
#include "calculation/DatedRemainingWork.h"
#include <algorithm>
namespace jobs {
void Document::setDatedRemainingWork(RemainingWorkPlan plan) {
    requireEditable();
    auto p = *project_;
    if (std::none_of(p.assignments.begin(), p.assignments.end(),
                     [&](const auto &a) { return a.taskId == plan.taskId && a.resourceId == plan.resourceId; }))
        throw AppError(ErrorCode::REMAINING_WORK_INVALID, {"현재 자원 배정을 확인하세요."}, {},
                       "DATED_REMAINING_INVALID");
    auto it = std::find_if(p.remainingWork.begin(), p.remainingWork.end(),
                           [&](const auto &v) { return v.taskId == plan.taskId && v.resourceId == plan.resourceId; });
    if (plan.days.empty()) {
        if (it == p.remainingWork.end())
            return;
        p.remainingWork.erase(it);
    } else {
        if (!p.statusAt || plan.statusAt != *p.statusAt)
            throw AppError(ErrorCode::REMAINING_WORK_INVALID, {"입력 기준 상태일이 변경되었습니다."}, {},
                           "DATED_REMAINING_INVALID");
        p.calculationProfile = effortProfile;
        if (it == p.remainingWork.end())
            p.remainingWork.push_back(plan);
        else
            *it = plan;
        validateRemainingWork(p);
        // Certify this assignment independently so stale inputs on another assignment can be repaired in sequence.
        auto probe = p;
        const auto task =
            std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &t) { return t.id == plan.taskId; });
        if (!usesWork(*task))
            probe.remainingWork = {plan};
        else
            // Work-driven assignments share one total. Include all current
            // targets, but retain stale entries only in the live proposal so
            // they can be repaired one at a time without silently adopting them.
            std::erase_if(probe.remainingWork,
                          [&](const auto &v) { return v.taskId != plan.taskId || v.statusAt != plan.statusAt; });
        probe.planLeveling.reset();
        probe.remainingLeveling.reset();
        probe.levelingDecisions.clear();
        const auto result = calculate(probe);
        if (result.status != CalculationStatus::Valid) {
            Diagnostic d{"REMAINING_WORK_INVALID",
                         {"현재 잔여 실행 구간에 입력을 적용할 수 없습니다."},
                         {plan.taskId},
                         "DATED_REMAINING_INVALID"};
            d.causes = result.diagnostics;
            throw AppError(std::move(d));
        }
    }
    calculationCheckpoint();
    edit(std::move(p), "날짜별 잔여 공수 변경");
}
void Document::clearDatedRemainingWork() {
    requireEditable();
    auto p = *project_;
    p.remainingWork.clear();
    edit(std::move(p), "날짜별 잔여 공수 전체 해제");
}
} // namespace jobs
