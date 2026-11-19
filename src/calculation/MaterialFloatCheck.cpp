#include "calculation/MaterialFloatCheck.h"
#include "domain/MaterialActuals.h"
#include "domain/ResourcePlan.h"
#include <algorithm>
namespace ppm {
MaterialFloatCheck::MaterialFloatCheck(const Project &p, const ScheduleResult &r) : source_(p) {
    if (r.status != CalculationStatus::Valid || r.tasks.size() != p.tasks.size())
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    validate(p);
    if (r.isForecast)
        inventory_ = remainingMaterialInventoryInput(p);
    header_.documentId = r.documentId;
    header_.inputRevision = r.inputRevision;
    header_.profile = r.profile;
    header_.status = CalculationStatus::Valid;
    header_.isForecast = r.isForecast;
    header_.statusAt = r.statusAt;
}
bool MaterialFloatCheck::fits(const std::vector<TaskSchedule> &rows) {
    calculationCheckpoint();
    const auto &p = input();
    const auto base = p.tasks.size() + p.resources.size() + p.assignments.size() + p.resourcePlan.materials.size() +
                      p.resourcePlan.uses.size() + p.resourcePlan.receipts.size() + rows.size();
    if (++calls_ > 100000 || work_ + base > 100000000)
        throw AppError(ErrorCode::WORKSPACE_LIMIT);
    work_ += base;
    auto trial = header_;
    trial.tasks = rows;
    std::set<std::string> scheduled;
    for (const auto &row : rows)
        if (!scheduled.insert(row.taskId).second)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    MaterialInventoryQuery query(p, trial, {}, &scheduled, true, header_.isForecast);
    work_ += std::max<std::uint64_t>(1, query.shortageWork_);
    if (work_ > 100000000)
        throw AppError(ErrorCode::WORKSPACE_LIMIT);
    for (const auto &[id, e] : query.entries_) {
        calculationCheckpoint();
        if (e.demands.empty())
            continue;
        if (!e.material.stockMilli || query.firstShortage(id))
            return false;
        if (p.resourcePlan.autoDelayForReceipts || header_.isForecast)
            for (const auto &d : e.demands)
                if (d.openingConsumed)
                    return false;
    }
    return true;
}
} // namespace ppm
