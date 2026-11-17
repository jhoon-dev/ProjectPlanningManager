#include "application/Document.h"
#include "domain/FinancialPeriods.h"
#include "calculation/FinancialPeriodReports.h"
namespace jobs {
std::string Document::recordFinancialPeriod(FinancialPeriodSubmission submission) {
    requireEditable();
    FinancialPeriodEvent e;
    static_cast<FinancialPeriodSubmission &>(e) = std::move(submission);
    e.id = store_.newId();
    if (e.period.id.empty() && e.action == "define")
        e.period.id = e.id;
    e.sourceDocumentId = project_->documentId;
    e.currency = project_->currency;
    e.statusAt = project_->statusAt;
    e.knowledgeRevision = project_->revision;
    e.recordedRevision = project_->revision + 1;
    auto p = *project_;
    if (e.action == "close") {
        const auto &previous = financialPeriod(p, e.period.id);
        if (previous.action == "close" || previous.period != e.period || !e.statusAt ||
            *e.statusAt < e.period.toDate + " 23:59")
            throw AppError(ErrorCode::FINANCIAL_PERIOD_INVALID, {"열린 기간 정의와 종료 이후 상태일을 확인하세요."});
        // Validate the submitted definition and metadata before starting calculation.
        auto check = p;
        auto draft = e;
        draft.action = "define";
        check.financialPeriodEvents.push_back(draft);
        ++check.revision;
        validate(check);
        e.tasks = financialPeriodSnapshot(p, e.period, e.formula);
    }
    p.financialPeriodEvents.push_back(e);
    edit(std::move(p), e.action == "close"    ? "사용자 기간 마감"
                       : e.action == "reopen" ? "사용자 기간 재개방"
                                              : "사용자 기간 정의");
    return e.period.id;
}
} // namespace jobs
