#include "application/Document.h"
namespace jobs {
void Document::adoptLeveling(LevelingSelection selection) {
    requireEditable();
    if (selection.sourceDocumentId != project_->documentId || selection.sourceRevision != project_->revision)
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    validateLevelingSelection(*project_, selection);
    auto p = *project_;
    p.planLeveling.reset();
    p.remainingLeveling.reset();
    if (p.statusAt)
        p.remainingLeveling = std::move(selection);
    else
        p.planLeveling = std::move(selection);
    auto snapshot = p;
    snapshot.levelingDecisions.clear(); // The proposal's decision is appended only after successful calculation.
    ++snapshot.revision;
    auto computed = calculate(snapshot);
    if (computed.status != CalculationStatus::Valid || !computed.resourceLeveled)
        throw AppError(ErrorCode::LEVEL_OPTIONS_INVALID);
    calculationCheckpoint();
    edit(std::move(p), "평준화 결과 채택");
    applyCalculation(std::move(computed));
}
void Document::clearLeveling() {
    requireEditable();
    if (!project_->planLeveling && !project_->remainingLeveling)
        return;
    auto p = *project_;
    p.planLeveling.reset();
    p.remainingLeveling.reset();
    auto snapshot = p;
    snapshot.levelingDecisions.clear();
    ++snapshot.revision;
    auto computed = calculate(snapshot);
    calculationCheckpoint();
    edit(std::move(p), "평준화 채택 해제");
    applyCalculation(std::move(computed));
}
} // namespace jobs
