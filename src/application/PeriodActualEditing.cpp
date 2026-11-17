#include "application/Document.h"
#include "application/PeriodActualEditing.h"
#include "domain/PeriodActuals.h"
#include <algorithm>
#include <set>
namespace jobs {
void Document::recordPeriodActuals(PeriodActualSubmission submission) {
    requireEditable();
    auto p = *project_;
    appendPeriodActuals(p, std::move(submission), [this] { return store_.newId(); });
    edit(std::move(p), "기간별 실제공수·원가 제출");
}
void Document::recordPeriodActualBatch(std::vector<PeriodActualSubmission> submissions, const std::string &statusAt) {
    requireEditable();
    if (!project_->statusAt || *project_->statusAt != statusAt)
        throw AppError(ErrorCode::STATUS_DATE_REQUIRED);
    if (submissions.empty())
        return;
    std::size_t count = 0;
    std::set<PeriodActualKey> keys;
    for (const auto &s : submissions)
        for (const auto &e : s.entries) {
            if (++count > 1000 || !keys.insert(periodActualKey(e)).second)
                throw AppError(ErrorCode::PERIOD_ACTUAL_LIMIT);
        }
    if (!count || submissions.size() > 1000)
        throw AppError(ErrorCode::PERIOD_ACTUAL_LIMIT);
    auto p = *project_;
    for (auto &s : submissions) {
        calculationCheckpoint();
        appendPeriodActuals(p, std::move(s), [this] { return store_.newId(); });
    }
    edit(std::move(p), "외부 기간 실적 일괄 채택");
}
void appendPeriodActuals(Project &p, PeriodActualSubmission submission, const std::function<std::string()> &newId,
                         bool allowACOnly) {
    if (!p.statusAt)
        throw AppError(ErrorCode::STATUS_DATE_REQUIRED);
    if ((!allowACOnly && submission.entries.empty()) || submission.entries.size() > 1000 ||
        submission.totals.size() > 10000)
        throw AppError(ErrorCode::PERIOD_ACTUAL_LIMIT);
    std::map<PeriodActualKey, const PeriodActualEntry *> previous;
    std::set<std::pair<std::string, std::string>> historical;
    for (const auto &e : p.periodActuals) {
        previous[periodActualKey(e)] = &e;
        historical.emplace(e.taskId, e.resourceId);
    }
    std::set<PeriodActualKey> keys;
    std::set<std::string> tasks, requiresCost;
    for (auto &e : submission.entries) {
        calculationCheckpoint();
        if (!keys.insert(periodActualKey(e)).second)
            throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
        tasks.insert(e.taskId);
        const auto old = previous.contains(periodActualKey(e)) ? previous.at(periodActualKey(e)) : nullptr;
        if (e.actualMinor || (old && old->actualMinor))
            requiresCost.insert(e.taskId);
        if (!e.resourceId.empty()) {
            const auto r = std::find_if(p.resources.begin(), p.resources.end(),
                                        [&](const auto &r) { return r.id == e.resourceId; });
            if (r == p.resources.end() || (!historical.contains({e.taskId, e.resourceId}) &&
                                           std::none_of(p.assignments.begin(), p.assignments.end(), [&](const auto &a) {
                                               return a.taskId == e.taskId && a.resourceId == e.resourceId;
                                           })))
                throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
            e.resourceType = old ? old->resourceType : r->type;
        } else
            e.resourceType.clear();
        e.id = newId();
        e.statusAt = *p.statusAt;
        e.recordedAt = submission.recordedAt;
        e.reporter = submission.reporter;
        e.reason = submission.reason;
        e.previousEntryId = old ? old->id : "";
        e.sourceDocumentId = p.documentId;
        e.recordedRevision = p.revision + 1;
        e.currency = p.currency;
        e.costEntryId.clear();
    }
    std::map<std::string, std::string> costIds;
    for (const auto &total : submission.totals) {
        if ((!tasks.contains(total.taskId) &&
             (!allowACOnly ||
              std::none_of(p.tasks.begin(), p.tasks.end(), [&](const auto &t) { return t.id == total.taskId; }))) ||
            costIds.contains(total.taskId))
            throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
        CostEntry c;
        c.id = newId();
        c.taskId = total.taskId;
        c.statusAt = *p.statusAt;
        c.recordedAt = submission.recordedAt;
        c.reporter = submission.reporter;
        c.reason = submission.reason;
        c.sourceDocumentId = p.documentId;
        c.recordedRevision = p.revision + 1;
        c.currency = p.currency;
        c.actualMinor = total.actualMinor;
        if (const auto old = costAsOf(p, c.taskId, *p.statusAt))
            c.physicalBasisPoints = old->physicalBasisPoints;
        for (auto it = p.costEntries.rbegin(); it != p.costEntries.rend(); ++it)
            if (it->taskId == c.taskId) {
                c.previousEntryId = it->id;
                break;
            }
        costIds.emplace(c.taskId, c.id);
        p.costEntries.push_back(std::move(c));
    }
    for (const auto &task : requiresCost)
        if (!costIds.contains(task))
            throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
    for (auto &e : submission.entries) {
        if (costIds.contains(e.taskId))
            e.costEntryId = costIds.at(e.taskId);
        p.periodActuals.push_back(std::move(e));
    }
}
} // namespace jobs
