#include "domain/RemainingWork.h"
#include "domain/ResourcePlan.h"
#include "domain/MaterialActuals.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <set>
#include <unordered_set>
namespace ppm {
bool hasDatedEffort(const Project &p) {
    std::unordered_set<std::string> ids;
    for (const auto &t : p.tasks)
        if (t.taskType != TaskType::FixedDuration)
            ids.insert(t.id);
    return std::any_of(p.remainingWork.begin(), p.remainingWork.end(),
                       [&](const auto &v) { return ids.contains(v.taskId); });
}
std::string datedRemainingEngine(const Project &p) {
    return hasDatedEffort(p)             ? "ppm-dated-effort-1.0"
           : hasWorkAssignmentPeriods(p) ? "ppm-dated-work-periods-1.0"
                                         : "ppm-dated-remaining-1.0";
}
void validateRemainingWork(const Project &p) {
    if (p.remainingWork.size() > 10000)
        throw AppError(ErrorCode::REMAINING_WORK_INVALID, {"배정 한도 10,000개"}, {}, "DATED_REMAINING_INVALID");
    std::set<std::pair<std::string, std::string>> keys;
    std::size_t total = 0;
    for (const auto &v : p.remainingWork) {
        calculationCheckpoint();
        auto t = std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &x) { return x.id == v.taskId; });
        if (!isEffort(p) || t == p.tasks.end() ||
            (usesWork(*t) ? t->workUnitMinutes.value_or(0) <= 0 : t->durationMinutes.value_or(0) <= 0) ||
            !validStamp(v.statusAt) || v.days.empty() || v.days.size() > 1024 ||
            !keys.emplace(v.taskId, v.resourceId).second ||
            std::none_of(p.assignments.begin(), p.assignments.end(),
                         [&](const auto &a) { return a.taskId == v.taskId && a.resourceId == v.resourceId; }) ||
            std::any_of(p.activities.begin(), p.activities.end(),
                        [&](const auto &a) { return a.taskId == v.taskId && a.kind != "steps"; }))
            throw AppError(ErrorCode::REMAINING_WORK_INVALID,
                           {"양수 기간/공수 작업과 현재 배정·상태일·중복·행 한도를 확인하세요."}, {},
                           "DATED_REMAINING_INVALID");
        std::string previous;
        std::int64_t amount = 0;
        for (const auto &day : v.days) {
            if (!validDate(day.date) || day.date <= previous || day.date < v.statusAt.substr(0, 10) ||
                day.workUnitMinutes < 0 || day.workUnitMinutes > maxWork - amount)
                throw AppError(ErrorCode::REMAINING_WORK_INVALID,
                               {"날짜는 상태일 이후 정렬·비중복, 공수는 0 이상 허용 범위여야 합니다."}, {},
                               "DATED_REMAINING_INVALID");
            previous = day.date;
            amount += day.workUnitMinutes;
        }
        total += v.days.size();
        if (total > 100000)
            throw AppError(ErrorCode::REMAINING_WORK_INVALID, {"전체 날짜 행 한도 100,000개"}, {},
                           "DATED_REMAINING_INVALID");
    }
}
std::string levelingPolicyFor(const Project &p) {
    if (p.statusAt && p.resourcePlan.autoDelayRemainingForReceipts)
        return remainingMaterialPolicy;
    if (p.resourcePlan.autoDelayForReceipts)
        return materialArrivalPolicy;
    return p.statusAt && !p.remainingWork.empty() ? (hasDatedEffort(p) ? datedEffortPolicy : datedRemainingPolicy)
                                                  : levelingPolicyFor(static_cast<const PlanInput &>(p));
}
bool supportsLevelingPolicy(const Project &p, const std::string &policy) {
    if (p.statusAt && p.resourcePlan.autoDelayRemainingForReceipts)
        return policy == remainingMaterialPolicy;
    if (policy == remainingMaterialPolicy)
        return false;
    if (p.resourcePlan.autoDelayForReceipts)
        return !p.statusAt && policy == materialArrivalPolicy;
    if (p.statusAt && !p.remainingWork.empty())
        return policy == (hasDatedEffort(p) ? datedEffortPolicy : datedRemainingPolicy);
    return supportsLevelingPolicy(static_cast<const PlanInput &>(p), policy);
}
} // namespace ppm
