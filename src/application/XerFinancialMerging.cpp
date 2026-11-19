#include "application/Document.h"
namespace ppm {
void Document::recordXerFinancialMerge(XerFinancialMergeSubmission submission, std::string (*hash)(const Project &)) {
    requireEditable();
    if (!hash || !activeXerFinancials(*project_))
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    auto result = mergeXerFinancials(*project_, submission.options);
    if (!result.ready())
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_080, {"merge/unresolved conflicts"});
    auto p = *project_;
    XerFinancialRevision revision;
    revision.id = store_.newId();
    revision.reporter = std::move(submission.reporter);
    revision.reason = std::move(submission.reason);
    revision.recordedAt = std::move(submission.recordedAt);
    revision.sourceDocumentId = p.documentId;
    revision.recordedRevision = p.revision + 1;
    revision.merge = XerFinancialMergeAudit{activeXerFinancials(p)->projectionSha256, submission.options.replacePeriods,
                                            submission.options.assignmentOverrides};
    revision.financials = std::move(result.financials);
    revision.allocations = std::move(result.allocations);
    revision.ownedEntryIds = std::move(result.entryIds);
    // Only the financial archive changes. Existing fact IDs and cumulative reports remain intact.
    revision.financials.projectionSha256 = hash(p);
    p.xerFinancialRevisions.push_back(std::move(revision));
    edit(std::move(p), "XER 원본·현재 원장 자동 병합");
}
} // namespace ppm
