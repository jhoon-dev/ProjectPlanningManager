#include "application/Document.h"
#include "domain/ProjectCurrency.h"
namespace jobs {
void Document::convertCurrency(CurrencyConversionSubmission submission) {
    requireEditable();
    auto before = *project_;
    clearTransitionHistories(before);
    auto after = convertProjectCurrency(before, submission.rate);
    for (const auto &b : after.baselines)
        verifyBaseline(b);
    CurrencyConversion event;
    static_cast<CurrencyConversionSubmission &>(event) = std::move(submission);
    event.id = store_.newId();
    event.sourceDocumentId = before.documentId;
    event.recordedRevision = after.revision;
    event.before = std::make_shared<const Project>(std::move(before));
    event.after = std::make_shared<const Project>(after);
    after.ledgerIdMigrations = project_->ledgerIdMigrations;
    after.currencyConversions = project_->currencyConversions;
    after.currencyConversions.push_back(std::move(event));
    --after.revision;
    edit(std::move(after), "프로젝트 전체 통화 전환");
}
} // namespace jobs
