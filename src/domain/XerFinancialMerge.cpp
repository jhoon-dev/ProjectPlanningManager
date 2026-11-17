#include "domain/XerFinancialMerge.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <set>
namespace jobs {
namespace {
using Pair = std::pair<std::string, std::string>;
bool reported(const PeriodActualEntry &e) {
    return e.workUnitMinutes || e.actualMinor;
}
bool external(const XerFinancialPeriod &p) {
    return p.laborWork || p.equipmentWork || p.laborMinor || p.equipmentMinor || p.materialMinor || p.bcwp || p.bcws;
}
} // namespace
bool XerFinancialMergeResult::ready() const {
    return std::none_of(issues.begin(), issues.end(), [](const auto &i) { return !i.resolved; });
}
XerFinancialMergeResult mergeXerFinancials(const Project &p, const XerFinancialMergeOptions &options) {
    XerFinancialMergeResult result;
    auto issue = [&](std::string kind, std::string task, std::string resource, std::string message) {
        result.issues.push_back(
            {std::move(kind), std::move(task), std::move(resource), {}, {}, std::move(message), {}, false});
    };
    const auto *base = activeXerFinancials(p);
    if (!base) {
        issue("source", {}, {}, "보존된 XER 집계가 없습니다.");
        return result;
    }
    validateXerFinancials(*base);
    result.financials = *base;
    if (!p.statusAt || *p.statusAt < base->statusAt) {
        issue("status", {}, {}, "상태일은 마지막 집계의 상태일 이상이어야 합니다.");
        return result;
    }
    if (p.currency != base->currency) {
        issue("currency", {}, {}, "통화가 달라 병합할 수 없습니다.");
        return result;
    }
    result.financials.statusAt = *p.statusAt;
    std::map<std::string, const Task *> tasks;
    std::map<std::string, const Resource *> resources;
    std::map<Pair, const Assignment *> assignments;
    for (const auto &t : p.tasks)
        tasks[t.id] = &t;
    for (const auto &r : p.resources)
        resources[r.id] = &r;
    for (const auto &a : p.assignments)
        assignments[{a.taskId, a.resourceId}] = &a;
    for (const auto &t : base->tasks)
        if (!tasks.contains(t.id))
            issue("missing-task", t.id, {}, "원본 작업이 삭제되었습니다. 작업 연결을 복원한 뒤 병합하세요.");
    for (const auto &r : base->resources) {
        if (!resources.contains(r.id))
            issue("missing-resource", {}, r.id, "원본 자원이 삭제되었습니다. 자원 연결을 복원하세요.");
        else if (resources.at(r.id)->type != r.type)
            issue("resource-type", {}, r.id, "인력/장비 종류가 바뀌었습니다. 원래 단위를 복원하세요.");
    }
    for (const auto &a : base->assignments)
        if (!assignments.contains({a.taskId, a.resourceId}))
            issue("missing-assignment", a.taskId, a.resourceId,
                  "원본 배정이 해제되었습니다. 같은 작업/자원 배정을 복원하세요.");
    if (!result.ready())
        return result;
    std::map<std::string, const CostEntry *> costs;
    for (const auto &c : p.costEntries)
        if (c.statusAt <= *p.statusAt) {
            auto &old = costs[c.taskId];
            if (!old || old->statusAt <= c.statusAt)
                old = &c;
        }
    result.financials.tasks.clear();
    result.financials.resources.clear();
    result.financials.assignments.clear();
    result.financials.periods.clear();
    for (const auto &t : p.tasks) {
        const auto *c = costs[t.id];
        result.financials.tasks.push_back({t.id, t.name, c ? c->actualMinor : std::nullopt});
    }
    for (const auto &r : p.resources)
        result.financials.resources.push_back({r.id, r.name, r.type});
    std::map<Pair, XerFinancialAssignment> totals;
    for (const auto &a : base->assignments)
        totals[{a.taskId, a.resourceId}] = a;
    for (const auto &a : p.assignments)
        if (!totals.contains({a.taskId, a.resourceId}))
            totals[{a.taskId, a.resourceId}] = {a.taskId, a.resourceId, {}, {}, {}, {}};
    std::set<Pair> overrides;
    for (const auto &a : options.assignmentOverrides) {
        const Pair key{a.taskId, a.resourceId};
        if (!totals.contains(key) || !overrides.insert(key).second) {
            issue("override", a.taskId, a.resourceId, "누계 대상이 중복되거나 존재하지 않습니다.");
            continue;
        }
        totals[key] = a;
    }
    std::set<std::size_t> replaced;
    for (auto i : options.replacePeriods)
        if (i >= base->periods.size() || !replaced.insert(i).second)
            issue("replacement", {}, {}, "기간 교체 대상이 중복되거나 존재하지 않습니다.");
    if (!result.ready())
        return result;
    const auto native = periodActualsAsOf(p, *p.statusAt);
    std::map<Pair, std::vector<const PeriodActualEntry *>> byOwner;
    std::size_t nativeCount = 0;
    for (const auto &[key, e] : native) {
        (void)key;
        if (!reported(*e))
            continue;
        if (++nativeCount > 1000) {
            issue("limit", {}, {}, "병합은 알려진 날짜 원장 1,000행까지 지원합니다.");
            return result;
        }
        if (!tasks.contains(e->taskId) ||
            (!e->resourceId.empty() && !assignments.contains({e->taskId, e->resourceId}))) {
            issue("ledger-owner", e->taskId, e->resourceId, "현재 날짜 실적의 배정/작업 연결을 복원하세요.");
            continue;
        }
        if (!e->resourceId.empty() && resources.at(e->resourceId)->type != e->resourceType)
            issue("ledger-unit", e->taskId, e->resourceId, "현재 자원 종류와 보고 시점 단위가 다릅니다.");
        byOwner[{e->taskId, e->resourceId}].push_back(e);
    }
    if (!result.ready())
        return result;
    const auto baseAllocations = p.xerFinancialRevisions.empty() ? initialXerFinancialAllocations(*base)
                                                                 : p.xerFinancialRevisions.back().allocations;
    std::set<std::size_t> allocated;
    for (const auto &a : baseAllocations)
        allocated.insert(a.periodIndex);
    const auto expected = allocateXerFinancials(*base, baseAllocations);
    for (const auto &[key, e] : expected) {
        (void)e;
        if (!native.contains(key))
            issue("missing-ledger", std::get<0>(key), std::get<1>(key),
                  "기존 배분 기록이 사라졌습니다. 원장을 복원하세요.");
    }
    std::set<PeriodActualKey> consumed;
    auto appendDaily = [&](const PeriodActualEntry &e) {
        if (!reported(e) || !consumed.insert(periodActualKey(e)).second)
            return;
        XerFinancialPeriod period;
        period.taskId = e.taskId;
        period.resourceId = e.resourceId;
        period.start = e.date + " 00:00";
        period.finish = e.date + " 23:59";
        if (e.resourceId.empty())
            period.expenseMinor = e.actualMinor;
        else {
            period.work = e.workUnitMinutes;
            period.actualMinor = e.actualMinor;
        }
        const auto index = result.financials.periods.size();
        result.financials.periods.push_back(std::move(period));
        result.allocations.push_back({index, {{e.date, 1}}});
        result.entryIds.push_back(e.id);
    };
    std::set<std::size_t> eligible;
    for (std::size_t i = 0; i < base->periods.size(); ++i) {
        calculationCheckpoint();
        const auto &period = base->periods[i];
        const Pair pair{period.taskId, period.resourceId};
        std::vector<const PeriodActualEntry *> inside;
        for (const auto *e : byOwner[pair])
            if (e->date >= period.start.substr(0, 10) && e->date <= period.finish.substr(0, 10))
                inside.push_back(e);
        const bool detail = allocated.contains(i);
        const bool amount =
            period.resourceId.empty() ? bool(period.expenseMinor) : bool(period.work || period.actualMinor);
        const bool ambiguous = !detail && amount && !inside.empty();
        if (!detail && amount) {
            eligible.insert(i);
            result.issues.push_back(
                {"unallocated-period", period.taskId, period.resourceId, period.start, period.finish,
                 ambiguous ? "날짜 없는 집계와 현재 실적이 겹칩니다. 현재 날짜 원장으로 기간을 교체할지 선택하세요."
                           : "날짜 없는 원본 집계를 유지합니다. 선택하면 현재 날짜 원장으로 교체하며, 명세가 없으면 "
                             "기간 금액/공수를 비웁니다.",
                 i, !ambiguous || replaced.contains(i)});
        }
        const bool useNative = detail || replaced.contains(i) || (!amount && !inside.empty());
        if (period.resourceId.empty()) {
            auto reference = period;
            if (useNative)
                reference.expenseMinor.reset();
            if (external(reference) || reference.expenseMinor)
                result.financials.periods.push_back(std::move(reference));
        } else if (!useNative)
            result.financials.periods.push_back(period);
        if (useNative)
            for (const auto *e : inside)
                appendDaily(*e);
        else
            for (const auto *e : inside)
                consumed.insert(periodActualKey(*e)); // Unresolved overlap must not also become an independent period.
    }
    for (auto i : replaced)
        if (!eligible.contains(i))
            issue("replacement", {}, {}, "현재 충돌로 표시된 집계 기간만 교체할 수 있습니다.");
    for (const auto &[key, e] : native)
        if (reported(*e) && !consumed.contains(key))
            appendDaily(*e);
    std::map<Pair, std::pair<std::int64_t, std::int64_t>> sums;
    std::set<Pair> knownWork, knownCost;
    std::map<std::string, std::int64_t> taskResource, expenses;
    for (const auto &e : result.financials.periods) {
        if (e.resourceId.empty())
            expenses[e.taskId] += e.expenseMinor.value_or(0);
        else {
            auto &sum = sums[{e.taskId, e.resourceId}];
            if (e.work)
                knownWork.insert({e.taskId, e.resourceId});
            if (e.actualMinor)
                knownCost.insert({e.taskId, e.resourceId});
            sum.first += e.work.value_or(0);
            sum.second += e.actualMinor.value_or(0);
        }
    }
    for (const auto &a : p.assignments) {
        auto total = totals.at({a.taskId, a.resourceId});
        const auto used = sums[{a.taskId, a.resourceId}];
        const auto work = total.regularWork.value_or(0) + total.overtimeWork.value_or(0),
                   cost = total.regularMinor.value_or(0) + total.overtimeMinor.value_or(0);
        if ((knownWork.contains({a.taskId, a.resourceId}) &&
             ((!total.regularWork && !total.overtimeWork) || used.first > work)) ||
            (knownCost.contains({a.taskId, a.resourceId}) &&
             ((!total.regularMinor && !total.overtimeMinor) || used.second > cost)))
            issue("cumulative-split", a.taskId, a.resourceId,
                  "날짜 명세가 누계를 초과합니다. 정규/초과근무 누계를 명시 입력하세요.");
        taskResource[a.taskId] += cost;
        result.financials.assignments.push_back(std::move(total));
    }
    for (const auto &t : result.financials.tasks) {
        const auto known = taskResource[t.id] + expenses[t.id];
        if (known > 0 && (!t.actualMinor || known > *t.actualMinor))
            issue("cumulative-ac", t.id, {},
                  "병합 집계가 현재 누계 AC를 초과합니다. 누계 구분 또는 현재 AC를 정정하세요.");
    }
    if (result.ready())
        try {
            validateXerFinancials(result.financials);
            const auto values = allocateXerFinancials(result.financials, result.allocations);
            if (values.size() != result.entryIds.size())
                issue("allocation", {}, {}, "날짜 명세를 모두 연결할 수 없습니다.");
        } catch (const AppError &) {
            issue("balance", {}, {}, "집계 한도·누계·기간 중복·미보고 값의 대사를 확인하세요.");
        }
    return result;
}
} // namespace jobs
