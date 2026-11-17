#include "calculation/BaselineCostFormulaValues.h"
#include "calculation/Costs.h"
#include "domain/Classification.h"
namespace jobs {
void addBaselineCostFormulaValues(const Project &p, const Baseline &b, XmlScheduleValues &out) {
    try {
        validatePlan(b.input);
        if (b.input.currency != p.currency)
            throw AppError(ErrorCode::FORMULA_INVALID,
                           {"동결 기준선과 현재 문서의 통화가 다릅니다. 자동 환산하지 않습니다."});
        Project frozen;
        static_cast<PlanInput &>(frozen) = b.input;
        frozen.documentId = b.sourceDocumentId;
        frozen.revision = b.inputRevision;
        ScheduleResult result;
        result.documentId = frozen.documentId;
        result.inputRevision = frozen.revision;
        result.engineVersion = b.engineVersion;
        result.profile = frozen.calculationProfile;
        result.status = CalculationStatus::Valid;
        result.tasks = b.schedule;
        result.projectFinish = b.projectFinish;
        for (const auto &t : frozen.tasks)
            out.baselineTaskCost.emplace(t.id, 0);
        for (const auto &r : frozen.resources)
            out.baselineResourceCost.emplace(r.id, 0);
        for (const auto &a : frozen.assignments)
            out.baselineAssignmentCost.emplace(std::pair{a.taskId, a.resourceId}, 0);
        const auto add = [](Money &sum, Money amount) {
            if (!sum || !amount) {
                sum.reset();
                return;
            }
            if (*amount < 0 || *amount > maxMoney || *sum > maxMoney - *amount)
                throw AppError(ErrorCode::FORMULA_INVALID, {"기준선 원가 합계 범위를 초과했습니다."});
            *sum += *amount;
        };
        for (const auto &day :
             plannedCostDays(frozen, result, out.baselineStart->substr(0, 10), out.baselineFinish->substr(0, 10))) {
            calculationCheckpoint();
            if (!out.baselineTaskCost.contains(day.taskId))
                throw AppError(ErrorCode::FORMULA_INVALID, {"동결 원가의 작업 연결을 확인하세요."});
            add(out.baselineTaskCost.at(day.taskId), day.amount);
            if (!day.resourceId.empty()) {
                const auto key = std::pair{day.taskId, day.resourceId};
                if (!out.baselineResourceCost.contains(day.resourceId) || !out.baselineAssignmentCost.contains(key))
                    throw AppError(ErrorCode::FORMULA_INVALID, {"동결 원가의 자원/배정 연결을 확인하세요."});
                add(out.baselineResourceCost.at(day.resourceId), day.amount);
                add(out.baselineAssignmentCost.at(key), day.amount);
            }
        }
        out.baselineCost = 0;
        for (const auto &[id, value] : out.baselineTaskCost) {
            (void)id;
            add(out.baselineCost, value);
        }
    } catch (const CalculationCancelled &) {
        throw;
    } catch (const std::exception &e) {
        out.baselineTaskCost.clear();
        out.baselineResourceCost.clear();
        out.baselineAssignmentCost.clear();
        out.baselineCost.reset();
        out.baselineCostIssue = e.what();
        if (const auto *error = dynamic_cast<const AppError *>(&e); error && !error->diagnostic().arguments.empty())
            out.baselineCostIssue = error->diagnostic().arguments.front();
    }
}
} // namespace jobs
