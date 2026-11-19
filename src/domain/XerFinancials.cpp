#include "domain/XerFinancials.h"
#include "domain/Project.h"
#include "domain/CalculationControl.h"
#include "domain/XerFinancialAllocation.h"
#include "domain/XerSourceRelink.h"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
namespace ppm {
namespace {
[[noreturn]] void invalid(const char *where) {
    throw AppError(ErrorCode::FILE_PRIMAVERAXER_080, {where});
}
bool label(const std::string &s) {
    return !s.empty() && s.size() <= 4096 && s.find_first_not_of(' ') != std::string::npos &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool digest(const std::string &s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::optional<std::int64_t> sum(std::optional<std::int64_t> a, std::optional<std::int64_t> b, std::int64_t limit) {
    if (!a && !b)
        return {};
    if (a.value_or(0) > limit - b.value_or(0))
        invalid("financials/sum overflow");
    return a.value_or(0) + b.value_or(0);
}
void amount(std::optional<std::int64_t> n, std::int64_t limit) {
    if (n && (*n < 0 || *n > limit))
        invalid("financials/amount");
}
} // namespace
void validateXerFinancials(const XerFinancialSnapshot &s) {
    if (!label(s.sourceName) || !digest(s.sourceSha256) || !label(s.projectId) || !digest(s.projectionSha256) ||
        !validStamp(s.statusAt) || !validUtcStamp(s.importedAt) || s.tasks.size() > 10000 ||
        s.resources.size() > 10000 || s.assignments.size() > 100000 || s.periods.size() > 100000)
        invalid("financials/metadata or limits");
    currencyDecimals(s.currency);
    std::map<std::string, std::optional<std::int64_t>> tasks;
    std::set<std::string> resources;
    for (const auto &t : s.tasks) {
        calculationCheckpoint();
        amount(t.actualMinor, maxMoney);
        if (!label(t.id) || !label(t.name) || !tasks.emplace(t.id, t.actualMinor).second)
            invalid("financials/tasks");
    }
    for (const auto &r : s.resources)
        if (!label(r.id) || !label(r.name) || (r.type != "labor" && r.type != "equipment") ||
            !resources.insert(r.id).second)
            invalid("financials/resources");
    using Key = std::pair<std::string, std::string>;
    std::map<Key, std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>> totals;
    std::map<std::string, std::optional<std::int64_t>> costs;
    for (const auto &a : s.assignments) {
        calculationCheckpoint();
        for (auto n : {a.regularWork, a.overtimeWork})
            amount(n, maxWork);
        for (auto n : {a.regularMinor, a.overtimeMinor})
            amount(n, maxMoney);
        const auto work = sum(a.regularWork, a.overtimeWork, maxWork),
                   cost = sum(a.regularMinor, a.overtimeMinor, maxMoney);
        if (!tasks.contains(a.taskId) || !resources.contains(a.resourceId) ||
            !totals.emplace(Key{a.taskId, a.resourceId}, std::pair{work, cost}).second)
            invalid("financials/assignment reference");
        costs[a.taskId] = sum(costs[a.taskId], cost, maxMoney);
    }
    using Range = std::pair<std::string, std::string>;
    std::map<std::tuple<std::string, std::string, int>, std::vector<Range>> ranges;
    std::map<Key, std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>> allocated;
    std::map<std::string, std::optional<std::int64_t>> expenses;
    std::set<Range> periods;
    for (const auto &e : s.periods) {
        calculationCheckpoint();
        if (!tasks.contains(e.taskId) || !validStamp(e.start) || !validStamp(e.finish) || e.start > e.finish ||
            e.finish.substr(0, 10) > s.statusAt.substr(0, 10))
            invalid("financials/period dates or task");
        const Key key{e.taskId, e.resourceId};
        if (!e.resourceId.empty() && !totals.contains(key))
            invalid("financials/period assignment");
        for (auto n : {e.work, e.laborWork, e.equipmentWork})
            amount(n, maxWork);
        for (auto n : {e.actualMinor, e.laborMinor, e.equipmentMinor, e.materialMinor, e.expenseMinor, e.bcwp, e.bcws})
            amount(n, maxMoney);
        if (e.resourceId.empty()) {
            if (e.work || e.actualMinor)
                invalid("financials/task summary fields");
            expenses[e.taskId] = sum(expenses[e.taskId], e.expenseMinor, maxMoney);
        } else {
            if (e.laborWork || e.equipmentWork || e.laborMinor || e.equipmentMinor || e.materialMinor ||
                e.expenseMinor || e.bcwp || e.bcws)
                invalid("financials/resource detail fields");
            auto &a = allocated[key];
            a.first = sum(a.first, e.work, maxWork);
            a.second = sum(a.second, e.actualMinor, maxMoney);
        }
        const Range range{e.start.substr(0, 10), e.finish.substr(0, 10)};
        if (!e.resourceId.empty())
            ranges[{e.taskId, e.resourceId, 0}].push_back(range);
        else {
            // External summary and dated expenses are independent dimensions.
            if (e.laborWork || e.equipmentWork || e.laborMinor || e.equipmentMinor || e.materialMinor || e.bcwp ||
                e.bcws || !e.expenseMinor)
                ranges[{e.taskId, "", 1}].push_back(range);
            if (e.expenseMinor)
                ranges[{e.taskId, "", 2}].push_back(range);
        }
        periods.emplace(e.start, e.finish);
    }
    if (periods.size() > 10000)
        invalid("financials/period limit");
    for (auto &[key, list] : ranges) {
        (void)key;
        std::sort(list.begin(), list.end());
        for (std::size_t i = 1; i < list.size(); ++i)
            if (list[i].first <= list[i - 1].second)
                invalid("financials/overlapping periods");
    }
    for (const auto &[key, a] : allocated) {
        const auto &total = totals.at(key);
        if ((a.first && (!total.first || *a.first > *total.first)) ||
            (a.second && (!total.second || *a.second > *total.second)))
            invalid("financials/period exceeds assignment total");
    }
    for (const auto &[task, total] : tasks) {
        const auto reported = sum(costs[task], expenses[task], maxMoney);
        if (reported && (!total || *reported > *total))
            invalid("financials/AC reconciliation");
    }
}
const XerFinancialSnapshot *activeXerFinancials(const Project &p) {
    if (!p.xerFinancialRevisions.empty())
        return &p.xerFinancialRevisions.back().financials;
    return p.xerFinancials ? &*p.xerFinancials : nullptr;
}
bool xerFinancialsMatch(const Project &p, const std::string &hash) {
    const auto *snapshot = activeXerFinancials(p);
    if (!snapshot || snapshot->projectionSha256 != hash)
        return false;
    const auto &s = *snapshot;
    if (p.statusAt != s.statusAt || p.currency != s.currency || p.tasks.size() != s.tasks.size() ||
        p.resources.size() != s.resources.size() || p.assignments.size() != s.assignments.size())
        return false;
    std::map<std::string, const CostEntry *> costs;
    for (const auto &c : p.costEntries)
        if (c.statusAt <= s.statusAt) {
            auto &old = costs[c.taskId];
            if (!old || old->statusAt <= c.statusAt)
                old = &c;
        }
    for (std::size_t i = 0; i < p.tasks.size(); ++i) {
        const auto &t = p.tasks[i];
        const auto &f = s.tasks[i];
        const auto *c = costs[t.id];
        if (t.id != f.id || t.name != f.name || f.actualMinor != (c ? c->actualMinor : std::nullopt))
            return false;
    }
    for (std::size_t i = 0; i < p.resources.size(); ++i) {
        const auto &r = p.resources[i];
        const auto &f = s.resources[i];
        if (r.id != f.id || r.name != f.name || r.type != f.type)
            return false;
    }
    for (std::size_t i = 0; i < p.assignments.size(); ++i)
        if (p.assignments[i].taskId != s.assignments[i].taskId ||
            p.assignments[i].resourceId != s.assignments[i].resourceId)
            return false;
    return true;
}
bool xerFinancialsNeedSchema27(const Project &p) {
    auto separated = [](const XerFinancialSnapshot &s) {
        std::map<std::string, std::vector<std::pair<std::string, std::string>>> ranges;
        for (const auto &e : s.periods)
            if (e.resourceId.empty())
                ranges[e.taskId].push_back({e.start.substr(0, 10), e.finish.substr(0, 10)});
        for (auto &[id, list] : ranges) {
            (void)id;
            std::sort(list.begin(), list.end());
            for (std::size_t i = 1; i < list.size(); ++i)
                if (list[i].first <= list[i - 1].second)
                    return true;
        }
        return false;
    };
    if (p.xerFinancials && separated(*p.xerFinancials))
        return true;
    for (const auto &r : p.xerFinancialRevisions)
        if (r.merge || separated(r.financials))
            return true;
    return false;
}
void validateXerFinancialRevisions(const Project &p) {
    if (p.xerFinancialRevisions.empty())
        return;
    if (!p.xerFinancials || p.xerFinancialRevisions.size() > 128)
        invalid("financial revisions/source or limit");
    const auto &source = *p.xerFinancials;
    const XerFinancialSnapshot *previous = &source;
    auto previousAllocations = initialXerFinancialAllocations(source);
    std::map<std::string, const PeriodActualEntry *> ledger;
    for (const auto &e : p.periodActuals)
        ledger.emplace(e.id, &e);
    std::set<std::string> ids;
    std::uint64_t revision = 0;
    std::size_t rows = 0;
    for (const auto &r : p.xerFinancialRevisions) {
        calculationCheckpoint();
        const auto &s = r.financials;
        const auto &metadata = r.relink ? r.relink->incoming : *previous;
        std::optional<XerRelinkCandidate> relinkBasis;
        if (r.relink) {
            relinkBasis = validateXerRelinkRevision(p, *previous, previousAllocations, r);
            const auto &i = r.relink->incoming;
            rows += i.tasks.size() + i.resources.size() + i.assignments.size() + i.periods.size();
        }
        rows += s.tasks.size() + s.resources.size() + s.assignments.size() + s.periods.size();
        if (rows > 300000 || !label(r.id) || !ids.insert(r.id).second || !label(r.reporter) || !label(r.reason) ||
            !label(r.sourceDocumentId) || !validUtcStamp(r.recordedAt) || !r.recordedRevision ||
            r.recordedRevision <= revision || r.recordedRevision > p.revision || s.sourceName != metadata.sourceName ||
            s.sourceSha256 != metadata.sourceSha256 || s.projectId != metadata.projectId ||
            s.currency != source.currency || s.importedAt != metadata.importedAt)
            invalid("financial revision identity/limit");
        if (r.merge) {
            if (r.merge->baseProjectionSha256 != previous->projectionSha256 || s.statusAt < previous->statusAt ||
                r.merge->replacedPeriods.size() > 100000 || r.merge->assignmentOverrides.size() > 100000)
                invalid("financial merge basis");
            std::set<std::size_t> replaced;
            for (auto i : r.merge->replacedPeriods)
                if (i >= (relinkBasis ? relinkBasis->financials.periods.size() : previous->periods.size()) ||
                    !replaced.insert(i).second)
                    invalid("financial merge replacement");
            std::map<std::pair<std::string, std::string>, const XerFinancialAssignment *> assignments;
            for (const auto &a : s.assignments)
                assignments[{a.taskId, a.resourceId}] = &a;
            std::set<std::pair<std::string, std::string>> overridden;
            for (const auto &a : r.merge->assignmentOverrides) {
                const auto key = std::pair{a.taskId, a.resourceId};
                if (!overridden.insert(key).second || !assignments.contains(key) || *assignments.at(key) != a)
                    invalid("financial merge override");
            }
        } else {
            if (s.statusAt != previous->statusAt || s.tasks.size() != previous->tasks.size() ||
                s.resources != previous->resources || s.assignments.size() != previous->assignments.size() ||
                s.periods.size() < previous->periods.size())
                invalid("financial revision shape");
            for (std::size_t i = 0; i < s.tasks.size(); ++i)
                if (s.tasks[i].id != previous->tasks[i].id || s.tasks[i].name != previous->tasks[i].name)
                    invalid("financial revision task");
            for (std::size_t i = 0; i < s.assignments.size(); ++i)
                if (s.assignments[i].taskId != previous->assignments[i].taskId ||
                    s.assignments[i].resourceId != previous->assignments[i].resourceId)
                    invalid("financial revision assignment");
            for (std::size_t i = 0; i < previous->periods.size(); ++i)
                if (s.periods[i].taskId != previous->periods[i].taskId ||
                    s.periods[i].resourceId != previous->periods[i].resourceId)
                    invalid("financial revision period owner");
        }
        revision = r.recordedRevision;
        const auto desired = allocateXerFinancials(s, r.allocations);
        if (r.ownedEntryIds.size() != desired.size())
            invalid("financial revision ownership count");
        std::map<PeriodActualKey, const PeriodActualEntry *> asOf;
        if (r.merge) {
            for (const auto &e : p.periodActuals)
                if (e.recordedRevision < r.recordedRevision && e.statusAt <= s.statusAt) {
                    auto &old = asOf[periodActualKey(e)];
                    if (!old || old->statusAt <= e.statusAt)
                        old = &e;
                }
            std::size_t known = 0;
            for (const auto &[key, e] : asOf)
                if (e->workUnitMinutes || e->actualMinor) {
                    ++known;
                    if (!desired.contains(key))
                        invalid("financial merge incomplete ledger");
                }
            if (known != desired.size())
                invalid("financial merge ledger coverage");
            std::map<std::string, const CostEntry *> costsAtMerge;
            for (const auto &c : p.costEntries)
                if (c.recordedRevision < r.recordedRevision && c.statusAt <= s.statusAt) {
                    auto &old = costsAtMerge[c.taskId];
                    if (!old || old->statusAt <= c.statusAt)
                        old = &c;
                }
            for (const auto &t : s.tasks) {
                const auto *cost = costsAtMerge[t.id];
                if (t.actualMinor != (cost ? cost->actualMinor : std::nullopt))
                    invalid("financial merge native cumulative AC");
            }
        }
        std::set<PeriodActualKey> owned;
        for (const auto &id : r.ownedEntryIds) {
            if (!ledger.contains(id))
                invalid("financial revision missing ledger");
            const auto &e = *ledger.at(id);
            const auto key = periodActualKey(e);
            if (!owned.insert(key).second || !desired.contains(key) ||
                e.workUnitMinutes != desired.at(key).workUnitMinutes || e.actualMinor != desired.at(key).actualMinor)
                invalid("financial revision ledger mismatch");
            if (r.merge) {
                if (!asOf.contains(key) || asOf.at(key)->id != id)
                    invalid("financial merge latest ledger");
            } else if (e.recordedRevision != r.recordedRevision || e.sourceDocumentId != r.sourceDocumentId ||
                       e.recordedAt != r.recordedAt || e.reporter != r.reporter || e.reason != r.reason ||
                       e.statusAt != s.statusAt)
                invalid("financial revision ledger provenance");
        }
        previous = &s;
        previousAllocations = r.allocations;
    }
}
} // namespace ppm
