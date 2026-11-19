#include "application/Document.h"
namespace ppm {
void Document::adoptProgressPolicy(ProgressPolicySelection selection) {
    requireEditable();
    if (selection.sourceDocumentId != project_->documentId || selection.sourceRevision != project_->revision)
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    validateProgressPolicySelection(selection);
    if (!project_->statusAt)
        throw AppError(ErrorCode::PROGRESS_POLICY_INVALID);
    auto p = *project_;
    p.planLeveling.reset();
    p.remainingLeveling.reset();
    p.progressPolicy = std::move(selection);
    auto snapshot = p;
    snapshot.levelingDecisions.clear();
    ++snapshot.revision;
    auto computed = calculate(snapshot);
    if (computed.status != CalculationStatus::Valid)
        throw AppError(Diagnostic{
            "PROGRESS_POLICY_INVALID", {}, {}, "PROGRESS_POLICY_INVALID", {}, {}, {}, {}, {}, computed.diagnostics});
    calculationCheckpoint();
    edit(std::move(p), "진척 정책 채택");
    applyCalculation(std::move(computed));
}
void Document::clearProgressPolicy() {
    requireEditable();
    if (!project_->progressPolicy)
        return;
    auto p = *project_;
    p.planLeveling.reset();
    p.remainingLeveling.reset();
    p.progressPolicy.reset();
    auto snapshot = p;
    snapshot.levelingDecisions.clear();
    ++snapshot.revision;
    auto computed = calculate(snapshot);
    calculationCheckpoint();
    edit(std::move(p), "진척 정책 채택 해제");
    applyCalculation(std::move(computed));
}
} // namespace ppm
