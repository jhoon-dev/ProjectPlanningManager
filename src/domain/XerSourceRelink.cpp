#include "domain/XerSourceRelink.h"
#include "domain/CalculationControl.h"
#include "domain/ProjectCurrency.h"
#include <algorithm>
#include <set>
namespace jobs {
namespace {
using Pair = std::pair<std::string, std::string>;
[[noreturn]] void bad(const char *s) {
    static const std::map<std::string, std::string> messages{
        {"relink/confirm source project", "선택한 프로젝트와 연결표를 후속 원본으로 확인하세요."},
        {"relink/incomplete identity map", "새 원본과 이전 집계의 모든 작업·자원에 연결 또는 제외를 명시하세요."},
        {"relink/duplicate source ID", "연결표의 원본 ID가 중복되었습니다."},
        {"relink/missing or duplicate target ID", "현재 연결 대상이 없거나 같은 종류의 대상에 중복 연결했습니다."},
        {"relink/cannot retire an observed identity",
         "실적·원가 관측값(명시 0 포함)이 있는 항목은 제외할 수 없습니다. 현재 대상에 연결하세요."},
        {"relink/incompatible resource type",
         "자원 종류가 다릅니다. 해당 원본의 종류 전환을 명시하거나 같은 종류의 자원을 선택하세요."},
        {"relink/currency or status date",
         "통화 전환 결과가 현재 통화와 같아야 하며 현재 상태일이 이전 집계와 새 원본의 상태일 이상이어야 합니다."},
        {"relink/restore mapped assignment", "관측값이 있는 작업·자원 조합의 배정을 현재 문서에서 복원하세요."},
        {"relink/replay mismatch", "저장된 원본·연결표와 병합 결과가 일치하지 않습니다."}};
    const auto it = messages.find(s);
    throw AppError(ErrorCode::FILE_PRIMAVERAXER_080, {it == messages.end() ? std::string(s) : it->second});
}
bool observed(const XerFinancialAssignment &a) {
    return a.regularWork || a.overtimeWork || a.regularMinor || a.overtimeMinor;
}
bool observed(const XerFinancialPeriod &e) {
    return e.work || e.actualMinor || e.laborWork || e.equipmentWork || e.laborMinor || e.equipmentMinor ||
           e.materialMinor || e.expenseMinor || e.bcwp || e.bcws;
}
std::map<Pair, std::string> links(const XerFinancialSnapshot &s, const XerFinancialSnapshot &target,
                                  const std::vector<XerIdLink> &input) {
    if (input.size() != s.tasks.size() + s.resources.size())
        bad("relink/incomplete identity map");
    std::map<Pair, std::string> out;
    std::set<Pair> used;
    std::map<Pair, std::string> kinds;
    for (const auto &t : target.tasks)
        kinds[{"task", t.id}] = "task";
    for (const auto &r : target.resources)
        kinds[{"resource", r.id}] = r.type;
    for (const auto &l : input) {
        calculationCheckpoint();
        const Pair key{l.kind, l.sourceId};
        if (!out.emplace(key, l.targetId).second)
            bad("relink/duplicate source ID");
        if (!l.targetId.empty() && (!kinds.contains({l.kind, l.targetId}) || !used.emplace(l.kind, l.targetId).second))
            bad("relink/missing or duplicate target ID");
    }
    auto check = [&](const std::string &kind, const std::string &id, const std::string &type, bool data) {
        if (!out.contains({kind, id}))
            bad("relink/missing source ID");
        const auto &to = out.at({kind, id});
        if (to.empty() && data)
            bad("relink/cannot retire an observed identity");
        if (!to.empty() && kinds.at({kind, to}) != type)
            bad("relink/incompatible resource type");
    };
    std::set<std::string> taskData, resourceData;
    for (const auto &a : s.assignments)
        if (observed(a)) {
            taskData.insert(a.taskId);
            resourceData.insert(a.resourceId);
        }
    for (const auto &e : s.periods)
        if (observed(e)) {
            taskData.insert(e.taskId);
            resourceData.insert(e.resourceId);
        }
    for (const auto &t : s.tasks)
        check("task", t.id, "task", t.actualMinor.has_value() || taskData.contains(t.id));
    for (const auto &r : s.resources)
        check("resource", r.id, r.type, resourceData.contains(r.id));
    return out;
}
XerFinancialSnapshot remap(XerFinancialSnapshot s, const std::map<Pair, std::string> &map) {
    std::erase_if(s.tasks, [&](auto &t) {
        const auto to = map.at({"task", t.id});
        t.id = to;
        return to.empty();
    });
    std::erase_if(s.resources, [&](auto &r) {
        const auto to = map.at({"resource", r.id});
        r.id = to;
        return to.empty();
    });
    std::erase_if(s.assignments, [&](auto &a) {
        a.taskId = map.at({"task", a.taskId});
        a.resourceId = map.at({"resource", a.resourceId});
        return a.taskId.empty() || a.resourceId.empty();
    });
    std::erase_if(s.periods, [&](auto &e) {
        const bool resource = !e.resourceId.empty();
        e.taskId = map.at({"task", e.taskId});
        if (resource)
            e.resourceId = map.at({"resource", e.resourceId});
        return e.taskId.empty() || (resource && e.resourceId.empty());
    });
    return s;
}
} // namespace
std::vector<XerIdLink> suggestXerLinks(const XerFinancialSnapshot &s, const Project &p) {
    std::vector<XerIdLink> out;
    std::set<std::string> ids;
    std::map<std::string, std::vector<std::string>> names;
    for (const auto &t : p.tasks) {
        ids.insert(t.id);
        names[t.name].push_back(t.id);
    }
    for (const auto &t : s.tasks) {
        const auto it = names.find(t.name);
        out.push_back({"task", t.id,
                       ids.contains(t.id)
                           ? t.id
                           : (it != names.end() && it->second.size() == 1 ? it->second[0] : std::string{})});
    }
    std::map<std::string, std::string> types;
    std::map<std::pair<std::string, std::string>, std::vector<std::string>> resources;
    for (const auto &r : p.resources) {
        types[r.id] = r.type;
        resources[{r.name, r.type}].push_back(r.id);
    }
    for (const auto &r : s.resources) {
        const auto it = resources.find({r.name, r.type});
        out.push_back({"resource", r.id,
                       types.contains(r.id) && types.at(r.id) == r.type
                           ? r.id
                           : (it != resources.end() && it->second.size() == 1 ? it->second[0] : std::string{})});
    }
    return out;
}
XerRelinkCandidate buildXerRelinkCandidate(const XerFinancialSnapshot &previous,
                                           const std::vector<XerFinancialAllocation> &oldAlloc,
                                           const XerRelinkAudit &audit, const XerFinancialSnapshot &target,
                                           const std::map<PeriodActualKey, const PeriodActualEntry *> &native) {
    if (!audit.confirmed || audit.projectName.empty() || audit.projectName.size() > 4096 ||
        std::any_of(audit.projectName.begin(), audit.projectName.end(),
                    [](unsigned char c) { return c < 32 || c == 127; }))
        bad("relink/confirm source project");
    validateXerFinancials(audit.incoming);
    validateXerFinancials(previous);
    const auto convertedPrevious =
        audit.previousConversion ? convertXerSource(previous, *audit.previousConversion) : previous;
    auto convertedIncoming =
        audit.incomingConversion ? convertXerSource(audit.incoming, *audit.incomingConversion) : audit.incoming;
    if (audit.incomingCurrencySteps.size() > 16)
        bad("relink/currency or status date");
    for (const auto &rate : audit.incomingCurrencySteps)
        convertedIncoming = convertCurrencyFinancials(convertedIncoming, rate);
    if (convertedIncoming.currency != target.currency || convertedPrevious.currency != target.currency ||
        audit.incoming.statusAt > target.statusAt || target.statusAt < previous.statusAt)
        bad("relink/currency or status date");
    if (audit.sourceStatusAt) {
        if (*audit.sourceStatusAt != audit.incoming.statusAt)
            bad("relink/source status");
    } else if (audit.incoming.statusAt != target.statusAt || !audit.incoming.periods.empty() ||
               std::any_of(audit.incoming.assignments.begin(), audit.incoming.assignments.end(),
                           [](const auto &a) { return observed(a); }) ||
               std::any_of(audit.incoming.tasks.begin(), audit.incoming.tasks.end(),
                           [](const auto &t) { return t.actualMinor.has_value(); }))
        bad("relink/undated source has observations");
    const auto oldLinks = links(convertedPrevious, target, audit.previousLinks),
               newLinks = links(convertedIncoming, target, audit.incomingLinks);
    const auto old = remap(convertedPrevious, oldLinks), incoming = remap(convertedIncoming, newLinks);
    XerRelinkCandidate result;
    auto &s = result.financials;
    s = convertedIncoming;
    s.tasks = target.tasks;
    s.resources = target.resources;
    s.assignments.clear();
    s.periods.clear();
    s.statusAt = target.statusAt;
    std::map<Pair, XerFinancialAssignment> totals;
    std::set<std::string> incomingTasks;
    std::set<Pair> incomingOwners;
    std::map<std::string, std::optional<std::int64_t>> sourceAC;
    for (const auto &t : old.tasks)
        sourceAC[t.id] = t.actualMinor;
    for (const auto &t : incoming.tasks) {
        incomingTasks.insert(t.id);
        if (t.actualMinor || audit.replaceMissingData)
            sourceAC[t.id] = t.actualMinor;
    }
    for (auto &t : s.tasks)
        if (sourceAC.contains(t.id))
            t.actualMinor = sourceAC[t.id];
    for (const auto &a : old.assignments)
        totals[{a.taskId, a.resourceId}] = a;
    for (const auto &a : incoming.assignments) {
        auto &v = totals[{a.taskId, a.resourceId}];
        v.taskId = a.taskId;
        v.resourceId = a.resourceId;
        for (auto m : {&XerFinancialAssignment::regularWork, &XerFinancialAssignment::overtimeWork,
                       &XerFinancialAssignment::regularMinor, &XerFinancialAssignment::overtimeMinor})
            if (a.*m || audit.replaceMissingData)
                v.*m = a.*m;
    }
    std::set<Pair> validAssignments;
    for (const auto &a : target.assignments) {
        const Pair key{a.taskId, a.resourceId};
        validAssignments.insert(key);
        s.assignments.push_back(totals.contains(key) ? totals.at(key)
                                                     : XerFinancialAssignment{a.taskId, a.resourceId, {}, {}, {}, {}});
    }
    for (const auto &[key, a] : totals)
        if (!validAssignments.contains(key) && observed(a))
            bad("relink/restore mapped assignment");
    for (const auto &e : incoming.periods)
        incomingOwners.emplace(e.taskId, e.resourceId);
    for (const auto &e : old.periods)
        if (!incomingOwners.contains({e.taskId, e.resourceId}) &&
            !(audit.replaceMissingData && incomingTasks.contains(e.taskId)))
            s.periods.push_back(e);
    s.periods.insert(s.periods.end(), incoming.periods.begin(), incoming.periods.end());
    validateXerFinancials(s);
    // Only previously allocated, unchanged amounts or identical native single days become bound automatically.
    auto key = [](const XerFinancialPeriod &e) {
        return std::tuple{e.taskId, e.resourceId, e.start, e.finish, e.work, e.actualMinor, e.expenseMinor};
    };
    std::map<decltype(key(XerFinancialPeriod{})), std::vector<XerFinancialDay>> bindings;
    for (const auto &a : oldAlloc) {
        if (a.periodIndex >= previous.periods.size())
            bad("relink/base allocation");
        auto e = convertedPrevious.periods[a.periodIndex];
        e.taskId = oldLinks.at({"task", e.taskId});
        if (!e.resourceId.empty())
            e.resourceId = oldLinks.at({"resource", e.resourceId});
        bindings[key(e)] = a.days;
    }
    for (std::size_t i = 0; i < s.periods.size(); ++i) {
        const auto &e = s.periods[i];
        bool bound = false;
        if (auto it = bindings.find(key(e)); it != bindings.end()) {
            result.allocations.push_back({i, it->second});
            bound = true;
        }
        if (!bound && e.start.substr(0, 10) == e.finish.substr(0, 10)) {
            const auto it = native.find({e.taskId, e.resourceId, e.start.substr(0, 10)});
            if (it != native.end() && it->second->workUnitMinutes == (e.resourceId.empty() ? std::nullopt : e.work) &&
                it->second->actualMinor == (e.resourceId.empty() ? e.expenseMinor : e.actualMinor) &&
                (it->second->workUnitMinutes || it->second->actualMinor))
                result.allocations.push_back({i, {{e.start.substr(0, 10), 1}}});
        }
    }
    return result;
}
Project projectForXerRelink(const Project &p, const XerRelinkAudit &audit) {
    if (!p.statusAt || !activeXerFinancials(p))
        bad("relink/source or native status missing");
    auto target = *activeXerFinancials(p);
    target.tasks.clear();
    target.resources.clear();
    target.assignments.clear();
    target.statusAt = *p.statusAt;
    target.currency = p.currency;
    std::map<std::string, const CostEntry *> costs;
    for (const auto &c : p.costEntries)
        if (c.statusAt <= *p.statusAt) {
            auto &old = costs[c.taskId];
            if (!old || old->statusAt <= c.statusAt)
                old = &c;
        }
    for (const auto &t : p.tasks) {
        const auto *c = costs[t.id];
        target.tasks.push_back({t.id, t.name, c ? c->actualMinor : std::nullopt});
    }
    for (const auto &r : p.resources)
        target.resources.push_back({r.id, r.name, r.type});
    for (const auto &a : p.assignments)
        target.assignments.push_back({a.taskId, a.resourceId, {}, {}, {}, {}});
    auto candidate =
        buildXerRelinkCandidate(*activeXerFinancials(p),
                                p.xerFinancialRevisions.empty() ? initialXerFinancialAllocations(*p.xerFinancials)
                                                                : p.xerFinancialRevisions.back().allocations,
                                audit, target, periodActualsAsOf(p, *p.statusAt));
    Project proposal = p;
    XerFinancialRevision temporary;
    temporary.financials = std::move(candidate.financials);
    temporary.allocations = std::move(candidate.allocations);
    proposal.xerFinancialRevisions.push_back(std::move(temporary));
    return proposal;
}
XerRelinkCandidate validateXerRelinkRevision(const Project &p, const XerFinancialSnapshot &previous,
                                             const std::vector<XerFinancialAllocation> &oldAlloc,
                                             const XerFinancialRevision &r) {
    if (!r.relink || !r.merge)
        bad("relink/missing audit");
    Project historical = p;
    historical.tasks.clear();
    historical.resources.clear();
    historical.assignments.clear();
    historical.statusAt = r.financials.statusAt;
    historical.currency = r.financials.currency;
    for (const auto &t : r.financials.tasks) {
        Task task;
        task.id = t.id;
        task.name = t.name;
        historical.tasks.push_back(task);
    }
    for (const auto &s : r.financials.resources) {
        Resource resource;
        resource.id = s.id;
        resource.name = s.name;
        resource.type = s.type;
        historical.resources.push_back(resource);
    }
    for (const auto &a : r.financials.assignments)
        historical.assignments.push_back({a.taskId, a.resourceId, 100});
    std::erase_if(historical.periodActuals, [&](const auto &e) { return e.recordedRevision >= r.recordedRevision; });
    std::erase_if(historical.costEntries, [&](const auto &e) { return e.recordedRevision >= r.recordedRevision; });
    auto candidate = buildXerRelinkCandidate(previous, oldAlloc, *r.relink, r.financials,
                                             periodActualsAsOf(historical, r.financials.statusAt));
    historical.xerFinancialRevisions.clear();
    XerFinancialRevision temp;
    temp.financials = candidate.financials;
    temp.allocations = candidate.allocations;
    historical.xerFinancialRevisions.push_back(std::move(temp));
    auto expected = mergeXerFinancials(historical, {r.merge->replacedPeriods, r.merge->assignmentOverrides});
    expected.financials.projectionSha256 = r.financials.projectionSha256;
    if (!expected.ready() || expected.financials != r.financials || expected.allocations != r.allocations ||
        expected.entryIds != r.ownedEntryIds)
        bad("relink/replay mismatch");
    return candidate;
}
} // namespace jobs
