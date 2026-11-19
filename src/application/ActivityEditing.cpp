#include "application/Document.h"
#include "domain/Activities.h"
#include <algorithm>
#include <map>
namespace ppm {
std::string Document::putActivity(ActivityDefinition activity) {
    requireEditable();
    auto p = *project_;
    if (activity.id.empty())
        activity.id = store_.newId();
    const auto id = activity.id;
    auto found = std::find_if(p.activities.begin(), p.activities.end(), [&](const auto &a) { return a.id == id; });
    if (found == p.activities.end())
        p.activities.push_back(std::move(activity));
    else
        *found = std::move(activity);
    edit(std::move(p), "Steps·지원 활동 정의");
    return id;
}
void Document::removeActivity(const std::string &id) {
    requireEditable();
    auto p = *project_;
    if (!std::erase_if(p.activities, [&](const auto &a) { return a.id == id; }))
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"지원 활동이 없습니다."});
    edit(std::move(p), "지원 활동 정의 삭제");
}
void Document::recordSteps(StepReport report) {
    requireEditable();
    if (!project_->statusAt)
        throw AppError(ErrorCode::STATUS_DATE_REQUIRED);
    auto p = *project_;
    const auto found = std::find_if(p.activities.begin(), p.activities.end(),
                                    [&](const auto &a) { return a.kind == "steps" && a.taskId == report.taskId; });
    if (found == p.activities.end())
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"Steps 정의를 먼저 저장하세요."});
    std::map<std::string, int> observations;
    for (const auto &s : report.steps)
        if (!observations.emplace(s.step.id, s.basisPoints).second)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 Steps 관측값입니다."});
    if (observations.size() != found->steps.size())
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"모든 Steps 값을 보고하세요."});
    report.steps.clear();
    for (const auto &s : found->steps) {
        if (!observations.contains(s.id))
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"Steps 참조가 변경되었습니다."});
        report.steps.push_back({s, observations.at(s.id)});
    }
    report.id = store_.newId();
    report.statusAt = *p.statusAt;
    report.sourceDocumentId = p.documentId;
    report.recordedRevision = p.revision + 1;
    p.stepReports.push_back(std::move(report));
    edit(std::move(p), "Steps 물리 진척 보고·정정");
}
} // namespace ppm
