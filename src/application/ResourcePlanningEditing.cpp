#include "application/Document.h"
#include "calculation/ResourcePlanning.h"
#include <algorithm>
namespace ppm {
void Document::resourcePlanning(ResourcePlanningSubmission s) {
    requireEditable();
    auto p = *project_;
    auto resource =
        std::find_if(p.resources.begin(), p.resources.end(), [&](const auto &r) { return r.id == s.resourceId; });
    if (resource == p.resources.end())
        throw AppError(ErrorCode::RESOURCE_NOT_FOUND);
    if (s.action == "rate") {
        if (s.rate)
            validateProductivityRate(*s.rate);
        resource->productivity = std::move(s.rate);
    } else {
        auto a = std::find_if(p.assignments.begin(), p.assignments.end(),
                              [&](const auto &v) { return v.taskId == s.taskId && v.resourceId == s.resourceId; });
        if (a == p.assignments.end())
            throw AppError(ErrorCode::ASSIGNMENT_INPUT_INVALID);
        if (!s.confirmed || s.reason.empty() || s.reporter.empty() || !validUtcStamp(s.recordedAt))
            throw AppError(Diagnostic{"RESOURCE_PLANNING_INVALID",
                                      {"근거·작성자·UTC와 명시 확인이 필요합니다."},
                                      {},
                                      "RESOURCE_PLANNING_INVALID"});
        if (s.action == "estimate") {
            if (!s.quantityMilli)
                a->productivityEstimate.reset();
            else {
                if (!resource->productivity)
                    throw AppError(Diagnostic{"RESOURCE_PLANNING_INVALID",
                                              {"자원 생산성을 먼저 정의하세요."},
                                              {},
                                              "RESOURCE_PLANNING_INVALID"});
                const auto rate = *resource->productivity;
                const auto work = productivityWork(*s.quantityMilli, rate);
                auto basis = p;
                basis.planLeveling.reset();
                basis.remainingLeveling.reset();
                basis.levelingDecisions.clear();
                const auto plan = calculatePlan(basis);
                if (plan.status != CalculationStatus::Valid)
                    throw AppError(ErrorCode::INVERSE_PLAN_REQUIRED);
                const auto t =
                    std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &v) { return v.id == s.taskId; });
                const auto row = std::find_if(plan.tasks.begin(), plan.tasks.end(),
                                              [&](const auto &v) { return v.taskId == s.taskId; });
                const bool durationBasis = t->taskType == TaskType::FixedDuration || s.keepDuration;
                if (durationBasis) {
                    const auto duration = TaskExecution(basis, *t).duration(*row);
                    p = materializeResourceWork(p, s.taskId, s.resourceId, {{0, duration, work}});
                } else {
                    if (std::count_if(p.assignments.begin(), p.assignments.end(),
                                      [&](const auto &v) { return v.taskId == s.taskId; }) != 1)
                        throw AppError(Diagnostic{"RESOURCE_PLANNING_INVALID",
                                                  {"다중 자원 작업은 현재 기간 유지로 자원별 공수를 명시 배분하세요."},
                                                  {},
                                                  "RESOURCE_PLANNING_INVALID"});
                    auto &target = p.tasks[std::size_t(t - p.tasks.begin())];
                    target.workDriven = true;
                    target.workUnitMinutes = work;
                    p.calculationProfile = effortProfile;
                    auto probe = p;
                    probe.planLeveling.reset();
                    probe.remainingLeveling.reset();
                    probe.levelingDecisions.clear();
                    const auto check = calculatePlan(probe);
                    if (check.status != CalculationStatus::Valid)
                        throw AppError(ErrorCode::INVERSE_PLAN_REQUIRED);
                    const auto &r = *std::find_if(check.tasks.begin(), check.tasks.end(),
                                                  [&](const auto &v) { return v.taskId == s.taskId; });
                    if (TaskExecution(probe, target).work(r) != work)
                        throw AppError(ErrorCode::WORK_RESULT_MISMATCH);
                }
                auto selected = std::find_if(p.assignments.begin(), p.assignments.end(), [&](const auto &v) {
                    return v.taskId == s.taskId && v.resourceId == s.resourceId;
                });
                selected->productivityEstimate =
                    ProductivityEstimate{rate,
                                         *s.quantityMilli,
                                         work,
                                         s.reason,
                                         s.recordedAt,
                                         true,
                                         durationBasis ? "duration-rates-v1" : "work-duration-v1",
                                         s.reporter};
            }
        } else if (s.action == "curve") {
            if (!s.curve)
                a->nativeCurve.reset();
            else {
                s.curve->reference = s.reason;
                s.curve->reporter = s.reporter;
                s.curve->recordedAt = s.recordedAt;
                p = applyNativeCurve(p, s.taskId, s.resourceId, std::move(*s.curve));
            }
        } else
            throw AppError(ErrorCode::ASSIGNMENT_INPUT_INVALID);
    }
    edit(std::move(p), "자원 생산성·고유 곡선 변경");
}
} // namespace ppm
