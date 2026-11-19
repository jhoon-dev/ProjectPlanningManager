#include "application/Document.h"
#include "domain/LedgerIdentity.h"
#include "domain/ProjectCurrency.h"
namespace ppm {
void Document::migrateLedgerIds(LedgerIdSubmission s) {
    requireEditable();
    auto before = *project_;
    clearTransitionHistories(before);
    auto after = migrateLedgerIdentities(before, s.links);
    for (const auto &b : after.baselines)
        verifyBaseline(b);
    LedgerIdMigration e;
    static_cast<LedgerIdSubmission &>(e) = std::move(s);
    e.id = store_.newId();
    e.sourceDocumentId = before.documentId;
    e.recordedRevision = after.revision;
    e.before = std::make_shared<const Project>(std::move(before));
    e.after = std::make_shared<const Project>(after);
    after.ledgerIdMigrations = project_->ledgerIdMigrations;
    after.currencyConversions = project_->currencyConversions;
    after.ledgerIdMigrations.push_back(std::move(e));
    --after.revision;
    edit(std::move(after), "관측 원장 ID 이관");
}
} // namespace ppm
