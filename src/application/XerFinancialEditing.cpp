#include "application/Document.h"
#include "application/PeriodActualEditing.h"
#include "domain/XerFinancialAllocation.h"
#include <map>
#include <set>
namespace jobs {
void Document::recordXerFinancials(XerFinancialSubmission s, std::string (*hash)(const Project &)) {
    requireEditable();
    if (!hash || !xerFinancialsMatch(*project_, hash(*project_)))
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    auto p = *project_;
    const auto &current = *activeXerFinancials(p);
    const auto desired = allocateXerFinancials(s.financials, s.allocations);
    const auto old = periodActualsAsOf(p, *p.statusAt);
    std::map<PeriodActualKey, const PeriodActualEntry *> owned;
    if (p.xerFinancialRevisions.empty()) {
        const auto imported = allocateXerFinancials(current, initialXerFinancialAllocations(current));
        for (const auto &[key, value] : imported) {
            const auto it = old.find(key);
            if (it == old.end() || it->second->workUnitMinutes != value.workUnitMinutes ||
                it->second->actualMinor != value.actualMinor)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            owned.emplace(key, it->second);
        }
    } else {
        std::map<std::string, const PeriodActualEntry *> byId;
        for (const auto &[key, entry] : old) {
            (void)key;
            byId.emplace(entry->id, entry);
        }
        for (const auto &id : p.xerFinancialRevisions.back().ownedEntryIds) {
            if (!byId.contains(id))
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
            owned.emplace(periodActualKey(*byId.at(id)), byId.at(id));
        }
    }
    PeriodActualSubmission native;
    native.reporter = s.reporter;
    native.reason = s.reason;
    native.recordedAt = s.recordedAt;
    for (const auto &[key, e] : desired) {
        if (old.contains(key) && !owned.contains(key))
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        native.entries.push_back(e);
    }
    // Withdraw former dates by appending an unreported replacement, never erasing facts.
    for (const auto &[key, e] : owned)
        if (!desired.contains(key)) {
            auto withdrawal = *e;
            withdrawal.workUnitMinutes.reset();
            withdrawal.actualMinor.reset();
            native.entries.push_back(std::move(withdrawal));
        }
    std::set<std::string> touched;
    for (const auto &e : native.entries)
        touched.insert(e.taskId);
    // Include AC-only edits, and always link affected dated rows to explicit cumulative totals.
    for (std::size_t i = 0; i < s.financials.tasks.size(); ++i) {
        const auto &t = s.financials.tasks[i];
        if (i >= current.tasks.size() || t.actualMinor != current.tasks[i].actualMinor)
            touched.insert(t.id);
    }
    for (const auto &t : s.financials.tasks)
        if (touched.contains(t.id))
            native.totals.push_back({t.id, t.actualMinor});
    const auto begin = p.periodActuals.size();
    if (!native.entries.empty() || !native.totals.empty())
        appendPeriodActuals(p, std::move(native), [this] { return store_.newId(); }, true);
    XerFinancialRevision revision;
    revision.id = store_.newId();
    revision.reporter = s.reporter;
    revision.reason = s.reason;
    revision.recordedAt = s.recordedAt;
    revision.sourceDocumentId = p.documentId;
    revision.recordedRevision = p.revision + 1;
    revision.financials = std::move(s.financials);
    revision.allocations = std::move(s.allocations);
    for (std::size_t i = begin; i < p.periodActuals.size(); ++i)
        if (desired.contains(periodActualKey(p.periodActuals[i])))
            revision.ownedEntryIds.push_back(p.periodActuals[i].id);
    // edit() invalidates derived selections before hashing/validation too. Mirror that invalidation here.
    invalidateLevelingSelection(p, *project_);
    revision.financials.projectionSha256 = hash(p);
    p.xerFinancialRevisions.push_back(std::move(revision));
    edit(std::move(p), "XER 집계 정정·날짜별 배분 제출");
}
} // namespace jobs
