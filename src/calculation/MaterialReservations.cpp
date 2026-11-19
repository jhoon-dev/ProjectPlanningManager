#include "calculation/MaterialReservations.h"
#include "domain/ResourcePlan.h"
#include "domain/MaterialActuals.h"
#include <algorithm>
namespace ppm {
namespace {
Diagnostic stock(const std::string &message) {
    return {"MATERIAL_STOCK", {message}, {}, "MATERIAL_STOCK"};
}
} // namespace
std::vector<Diagnostic> materialAutoReadiness(const Project &p) {
    if (!(p.statusAt ? p.resourcePlan.autoDelayRemainingForReceipts || p.resourcePlan.autoDelayForReceipts
                     : p.resourcePlan.autoDelayForReceipts))
        return {};
    if (p.statusAt && !p.resourcePlan.autoDelayRemainingForReceipts)
        return {stock("잔여 자재 자동 대기 옵션과 상태일 실제 재고·잔여 소모 확인이 필요합니다.")};
    validate(p);
    std::optional<Project> projected;
    if (p.statusAt) {
        try {
            projected = remainingMaterialInventoryInput(p);
        } catch (const AppError &e) {
            return {e.diagnostic()};
        }
    }
    const auto &input = projected ? *projected : p;
    std::map<std::string, std::int64_t> demand, incoming;
    for (const auto &u : input.resourcePlan.uses) {
        calculationCheckpoint();
        demand[u.materialId] += u.quantityMilli;
    }
    for (const auto &r : input.resourcePlan.receipts) {
        calculationCheckpoint();
        incoming[r.materialId] += r.quantityMilli;
    }
    std::vector<Diagnostic> out;
    for (const auto &m : input.resourcePlan.materials)
        if (demand.contains(m.id)) {
            if (!m.stockMilli)
                out.push_back(stock(m.name + " · 개시 재고 미정"));
            else if (*m.stockMilli + incoming[m.id] < demand[m.id])
                out.push_back(stock(m.name + " · 개시 재고와 전체 입고를 합해도 계획 소모가 부족합니다."));
        }
    return out;
}
MaterialReservations::MaterialReservations(const Project &p, bool enabled, bool forecast)
    : source_(p), forecast_(forecast), active_(enabled && (forecast ? p.resourcePlan.autoDelayRemainingForReceipts
                                                                    : p.resourcePlan.autoDelayForReceipts)) {
    if (!active_)
        return;

    const auto issues = materialAutoReadiness(p);
    if (!issues.empty())
        throw AppError(issues.front());
    if (forecast)
        inventory_ = remainingMaterialInventoryInput(p);
    const auto &input = project();
    accepted_.isForecast = forecast;
    accepted_.statusAt = forecast ? p.statusAt : std::nullopt;
    accepted_.documentId = p.documentId;
    accepted_.inputRevision = p.revision;
    accepted_.profile = p.calculationProfile;
    accepted_.status = CalculationStatus::Valid;
    for (const auto &u : input.resourcePlan.uses)
        uses_[u.taskId].push_back(&u);
    for (const auto &m : input.resourcePlan.materials)
        supply_[m.id].push_back(m.stockAt.value_or(p.startDate + " 09:00"));
    for (const auto &r : input.resourcePlan.receipts)
        supply_[r.materialId].push_back(r.at);
    for (auto &[id, dates] : supply_) {
        (void)id;
        std::sort(dates.begin(), dates.end());
        dates.erase(std::unique(dates.begin(), dates.end()), dates.end());
    }
}
bool MaterialReservations::fits(const TaskSchedule &row) {
    if (!active_ || !uses_.contains(row.taskId))
        return true;
    calculationCheckpoint();
    if (++trials_ > 20000)
        throw AppError(ErrorCode::WORKSPACE_LIMIT);
    auto trial = accepted_;
    trial.tasks.push_back(row);
    std::set<std::string> scheduled;
    for (const auto &r : trial.tasks)
        if (!scheduled.insert(r.taskId).second)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    const auto &project_ = project();
    MaterialInventoryQuery query(project_, trial, {}, &scheduled, true, forecast_);
    work_ += std::max<std::uint64_t>(1, query.shortageWork_) + project_.tasks.size() + project_.resources.size() +
             project_.assignments.size() + project_.resourcePlan.materials.size() +
             project_.resourcePlan.receipts.size() + project_.resourcePlan.uses.size();
    if (work_ > 100000000)
        throw AppError(ErrorCode::WORKSPACE_LIMIT);
    bool fits = true;
    for (const auto &[id, e] : query.entries_) {
        calculationCheckpoint();
        for (const auto &d : e.demands)
            if (d.openingConsumed)
                fits = false;
        if (!e.demands.empty() && (!e.material.stockMilli || query.firstShortage(id)))
            fits = false;
    }
    if (!fits)
        waited_.insert(row.taskId);
    return fits;
}
std::optional<std::string> MaterialReservations::nextStart(const TaskSchedule &row) const {
    if (!active_ || !uses_.contains(row.taskId))
        return {};
    // Only start-instant demand can safely skip to the next supply event. Finish/uniform
    // demand and non-monotone work contours must be tried at each legal working minute.
    if (row.remainingStart.value_or(row.start) != row.finish &&
        std::any_of(uses_.at(row.taskId).begin(), uses_.at(row.taskId).end(),
                    [](const auto *u) { return u->accrual != "start"; }))
        return {};
    std::optional<std::string> next;
    for (const auto *u : uses_.at(row.taskId)) {
        calculationCheckpoint();
        const auto &events = supply_.at(u->materialId);
        const auto found = std::upper_bound(events.begin(), events.end(), row.remainingStart.value_or(row.start));
        if (found != events.end() && (!next || *found < *next))
            next = *found;
    }
    return next;
}
void MaterialReservations::commit(const TaskSchedule &row) {
    if (active_ && uses_.contains(row.taskId))
        accepted_.tasks.push_back(row);
}
std::optional<Diagnostic> MaterialReservations::delay(const TaskSchedule &row) const {
    if (!waited_.contains(row.taskId))
        return {};
    return Diagnostic{"MATERIAL_DELAY", {row.remainingStart.value_or(row.start)}, {row.taskId}, "MATERIAL_DELAY"};
}
} // namespace ppm
