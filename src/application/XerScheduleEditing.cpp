#include "application/Document.h"
#include "domain/XerScheduleAdoption.h"
namespace jobs {
void Document::recordXerSchedule(XerScheduleSubmission submission) {
    requireEditable();
    auto p = *project_;
    XerScheduleAdoption event;
    static_cast<XerScheduleSubmission &>(event) = std::move(submission);
    event.id = store_.newId();
    event.sourceDocumentId = p.documentId;
    event.recordedRevision = p.revision + 1;
    event.before = static_cast<const PlanInput &>(p);
    event.after = adoptXerSchedule(event.before, event, event.id);
    static_cast<PlanInput &>(p) = event.after;
    if (event.schedule || event.calendars) {
        Project calculation;
        static_cast<PlanInput &>(calculation) = event.after;
        calculation.documentId = p.documentId;
        calculation.revision = p.revision + 1;
        const auto plan = calculatePlan(calculation);
        if (plan.status != CalculationStatus::Valid)
            throw AppError(ErrorCode::FILE_PRIMAVERAXER_080,
                           {"채택한 계획을 계산할 수 없습니다. 기간·선행 순환·제약·달력/배정 구간을 확인하세요."});
    }
    p.xerScheduleAdoptions.push_back(std::move(event));
    edit(std::move(p), "새 XER 일정·WBS·달력 채택");
}
} // namespace jobs
