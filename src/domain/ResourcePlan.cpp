#include "domain/ResourcePlan.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <map>
#include <set>
#include <limits>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(Diagnostic{"RESOURCE_PLAN_INVALID", {s}, {}});
}
bool label(const std::string &s) {
    return !s.empty() && s.size() <= 1024 && std::any_of(s.begin(), s.end(), [](unsigned char c) { return c > 32; }) &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool accrual(const std::string &s) {
    return s == "start" || s == "finish" || s == "uniform";
}
} // namespace
bool advancedPriority(const std::string &s) {
    return s == "least-float-first" || s == "deadline-first";
}
std::string formatMaterialQuantity(std::int64_t milli) {
    if (milli < 0)
        invalid("음수 자재 수량");
    auto whole = std::to_string(milli / 1000);
    const auto remainder = milli % 1000;
    if (!remainder)
        return whole;
    auto fraction = std::to_string(1000 + remainder).substr(1);
    while (fraction.back() == '0')
        fraction.pop_back();
    return whole + "." + fraction;
}
bool hasResourcePlan(const PlanInput &p) {
    return p.resourcePlan != ResourcePlan{};
}
bool hasMaterialInventory(const PlanInput &p) {
    return p.resourcePlan.autoDelayRemainingForReceipts || p.resourcePlan.autoDelayForReceipts ||
           !p.resourcePlan.receipts.empty() ||
           std::any_of(p.resourcePlan.materials.begin(), p.resourcePlan.materials.end(),
                       [](const auto &m) { return m.stockAt.has_value(); });
}
void validateResourcePlan(const PlanInput &p) {
    const auto &r = p.resourcePlan;
    if (r.materials.size() > 10000 || r.uses.size() > 100000 || r.expenses.size() > 100000 || r.roles.size() > 10000 ||
        r.demands.size() > 10000 || r.receipts.size() > 100000)
        invalid("자원 계획 한도");
    std::set<std::string> ids, tasks, materials;
    std::map<std::string, const Material *> materialDefinitions;
    std::map<std::string, std::string> roles, resources;
    for (const auto &t : p.tasks)
        tasks.insert(t.id);
    for (const auto &v : p.resources)
        resources.emplace(v.id, v.type);
    auto identity = [&](const std::string &id) {
        if (!label(id) || !ids.insert(id).second)
            invalid("자원 계획 ID 중복/형식");
    };
    for (const auto &m : r.materials) {
        calculationCheckpoint();
        identity(m.id);
        materials.insert(m.id);
        materialDefinitions[m.id] = &m;
        if (!label(m.name) || !label(m.unit) ||
            (m.unitPriceMinor && (*m.unitPriceMinor < 0 || *m.unitPriceMinor > maxMoney)) ||
            (m.stockMilli && (*m.stockMilli < 0 || *m.stockMilli > 1000000000000LL)) ||
            (m.stockAt && !validStamp(*m.stockAt)))
            invalid("자재 단위·단가·재고");
    }
    for (const auto &u : r.uses) {
        calculationCheckpoint();
        identity(u.id);
        if (!tasks.contains(u.taskId) || !materials.contains(u.materialId) || u.quantityMilli <= 0 ||
            u.quantityMilli > 1000000000000LL || !accrual(u.accrual))
            invalid("자재 소모 참조·수량·발생 방식");
    }
    for (const auto &receipt : r.receipts) {
        calculationCheckpoint();
        identity(receipt.id);
        const auto m = materialDefinitions.find(receipt.materialId);
        if (m == materialDefinitions.end() || !validStamp(receipt.at) ||
            receipt.at < m->second->stockAt.value_or(p.startDate + " 09:00") || receipt.quantityMilli <= 0 ||
            receipt.quantityMilli > 1000000000000LL)
            invalid("입고 자재·시각·개시 재고 기준·수량");
    }
    for (const auto &e : r.expenses) {
        calculationCheckpoint();
        identity(e.id);
        if (!tasks.contains(e.taskId) || !label(e.name) || !label(e.account) ||
            (e.amountMinor && (*e.amountMinor < 0 || *e.amountMinor > maxMoney)) || !accrual(e.accrual))
            invalid("비용 항목·계정·금액·발생 방식");
    }
    for (const auto &v : r.roles) {
        identity(v.id);
        roles.emplace(v.id, v.type);
        if (!label(v.name) || (v.type != "labor" && v.type != "equipment"))
            invalid("역할 이름·종류");
    }
    std::set<std::pair<std::string, std::string>> pairs;
    for (const auto &d : r.demands) {
        calculationCheckpoint();
        identity(d.id);
        if (!tasks.contains(d.taskId) || !roles.contains(d.roleId) || d.units <= 0 || d.units > 100000)
            invalid("역할 수요 참조·배정률");
        if (!d.resourceId.empty()) {
            if (!resources.contains(d.resourceId) || resources.at(d.resourceId) != roles.at(d.roleId) ||
                !pairs.emplace(d.taskId, d.resourceId).second)
                invalid("역할의 구체 자원 연결");
            const auto a = std::find_if(p.assignments.begin(), p.assignments.end(), [&](const auto &a) {
                return a.taskId == d.taskId && a.resourceId == d.resourceId;
            });
            if (a == p.assignments.end() || a->units != d.units)
                invalid("역할 수요와 실제 배정 불일치");
        }
    }
}
std::optional<std::int64_t> materialCost(const Material &m, std::int64_t quantity) {
    if (!m.unitPriceMinor || quantity < 0 || quantity > 1000000000000LL)
        return {};
    const auto price = *m.unitPriceMinor, q = quantity / 1000, r = quantity % 1000;
    if (price < 0 || price > maxMoney || (price && q > maxMoney / price))
        return {};
    const auto whole = q * price, fraction = (r * price + 500) / 1000;
    if (whole > maxMoney - fraction)
        return {};
    return whole + fraction;
}
std::vector<PlannedCharge> plannedCharges(const PlanInput &p, const std::string &task) {
    std::vector<PlannedCharge> result;
    for (const auto &u : p.resourcePlan.uses)
        if (u.taskId == task) {
            const auto m = std::find_if(p.resourcePlan.materials.begin(), p.resourcePlan.materials.end(),
                                        [&](const auto &m) { return m.id == u.materialId; });
            result.push_back(
                {m == p.resourcePlan.materials.end() ? std::nullopt : materialCost(*m, u.quantityMilli), u.accrual});
        }
    for (const auto &e : p.resourcePlan.expenses)
        if (e.taskId == task)
            result.push_back({e.amountMinor, e.accrual});
    return result;
}
std::vector<Diagnostic> materialReadiness(const PlanInput &p) {
    std::map<std::string, std::int64_t> used;
    if (p.resourcePlan.uses.size() > 100000)
        invalid("자재 소모 한도");
    for (const auto &u : p.resourcePlan.uses) {
        calculationCheckpoint();
        if (u.quantityMilli <= 0 || u.quantityMilli > 1000000000000LL)
            invalid("자재 소모 수량");
        used[u.materialId] += u.quantityMilli;
    }
    std::vector<Diagnostic> result;
    // Dated inventory is checked against candidate schedule dates by MaterialInventoryQuery.
    if (hasMaterialInventory(p))
        return result;
    for (const auto &m : p.resourcePlan.materials)
        if (used.contains(m.id) && (!m.stockMilli || used.at(m.id) > *m.stockMilli))
            result.push_back(
                {"MATERIAL_STOCK",
                 {m.name + (m.stockMilli ? " · 소모 수량이 재고를 초과합니다. 지연으로 재고가 재생되지 않습니다."
                                         : " · 자재 재고 미정")},
                 {},
                 "MATERIAL_STOCK"});
    return result;
}
std::size_t resourcePlanBytes(const ResourcePlan &r) {
    std::size_t n = sizeof(r);
    for (const auto &m : r.materials)
        n += sizeof(m) + m.id.size() + m.name.size() + m.unit.size() + (m.stockAt ? m.stockAt->size() : 0);
    for (const auto &receipt : r.receipts)
        n += sizeof(receipt) + receipt.id.size() + receipt.materialId.size() + receipt.at.size();
    for (const auto &u : r.uses)
        n += sizeof(u) + u.id.size() + u.taskId.size() + u.materialId.size() + u.accrual.size();
    for (const auto &e : r.expenses)
        n += sizeof(e) + e.id.size() + e.taskId.size() + e.name.size() + e.account.size() + e.accrual.size();
    for (const auto &v : r.roles)
        n += sizeof(v) + v.id.size() + v.name.size() + v.type.size();
    for (const auto &d : r.demands)
        n += sizeof(d) + d.id.size() + d.taskId.size() + d.roleId.size() + d.resourceId.size();
    return n;
}
} // namespace jobs
