#include "application/Document.h"
#include "domain/XerSourceRelink.h"
namespace ppm {
void Document::recordXerRelink(XerRelinkSubmission s, std::string (*hash)(const Project &)) {
    requireEditable();
    if (!hash)
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    auto candidate = projectForXerRelink(*project_, s.audit);
    auto result = mergeXerFinancials(candidate, s.merge.options);
    if (!result.ready())
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_080, {"relink/unresolved merge"});
    auto p = *project_;
    XerFinancialRevision r;
    r.id = store_.newId();
    r.reporter = std::move(s.merge.reporter);
    r.reason = std::move(s.merge.reason);
    r.recordedAt = std::move(s.merge.recordedAt);
    r.sourceDocumentId = p.documentId;
    r.recordedRevision = p.revision + 1;
    r.merge = XerFinancialMergeAudit{activeXerFinancials(p)->projectionSha256, s.merge.options.replacePeriods,
                                     s.merge.options.assignmentOverrides};
    r.relink = std::move(s.audit);
    r.financials = std::move(result.financials);
    r.allocations = std::move(result.allocations);
    r.ownedEntryIds = std::move(result.entryIds);
    r.financials.projectionSha256 = hash(p);
    p.xerFinancialRevisions.push_back(std::move(r));
    edit(std::move(p), "새 XER 버전·삭제 ID 재연결");
}
} // namespace ppm
