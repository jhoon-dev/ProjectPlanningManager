#include "domain/Activities.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <map>
#include <set>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &reason) {
    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {reason});
}
bool label(const std::string &s) {
    return !s.empty() && s.size() <= 1024 && std::any_of(s.begin(), s.end(), [](unsigned char c) { return c > 32; }) &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
void validateSteps(const std::vector<ActivityStep> &steps) {
    if (steps.empty() || steps.size() > 200)
        invalid("Steps는 작업당 1~200개입니다.");
    std::set<std::string> ids;
    for (const auto &step : steps)
        if (!label(step.id) || !label(step.name) || !ids.insert(step.id).second || step.weight < 1 ||
            step.weight > 1000000)
            invalid("Step ID·이름·가중치(1~1,000,000)를 확인하세요.");
}
} // namespace
bool hasActivityExtensions(const PlanInput &p) {
    return !p.activities.empty();
}
void validateActivities(const PlanInput &p) {
    if (p.activities.size() > 10000)
        invalid("지원 활동은 최대10,000개입니다.");
    std::set<std::string> tasks, ids, stepTasks, calendars;
    std::map<std::string, std::string> wbs;
    for (const auto &t : p.tasks)
        tasks.insert(t.id);
    for (const auto &c : p.calendars)
        calendars.insert(c.id);
    for (const auto &w : p.wbsNodes)
        wbs.emplace(w.id, w.parentId);
    std::size_t count = 0;
    for (const auto &a : p.activities) {
        calculationCheckpoint();
        if (!label(a.id) || !label(a.name) || tasks.contains(a.id) || !ids.insert(a.id).second ||
            (!a.calendarId.empty() && !calendars.contains(a.calendarId)))
            invalid("지원 활동 ID·이름·달력을 확인하세요.");
        if (a.kind == "steps") {
            if (!tasks.contains(a.taskId) || !stepTasks.insert(a.taskId).second || !a.startTaskId.empty() ||
                !a.finishTaskId.empty() || !a.wbsId.empty() || !a.calendarId.empty())
                invalid("Steps는 실제 작업 한 개에 연결하세요.");
            validateSteps(a.steps);
            count += a.steps.size();
        } else {
            if (!a.taskId.empty() || !a.steps.empty())
                invalid("지원 구간에는 Steps나 직접 실적·자원·원가를 배정하지 않습니다.");
            if (a.kind == "loe") {
                if (!tasks.contains(a.startTaskId) || !tasks.contains(a.finishTaskId) || !a.wbsId.empty())
                    invalid("LOE의 시작·완료 기준 작업을 선택하세요.");
            } else if (a.kind == "wbs-summary") {
                if ((!a.wbsId.empty() && !wbs.contains(a.wbsId)) || !a.startTaskId.empty() || !a.finishTaskId.empty())
                    invalid("WBS Summary의 WBS 범위를 확인하세요.");
            } else
                invalid("지원하지 않는 활동 종류입니다.");
        }
    }
    if (count > 10000)
        invalid("Steps 정의 합계는 최대10,000개입니다.");
}
int weightedStepProgress(const std::vector<StepObservation> &observations) {
    std::vector<ActivityStep> definitions;
    for (const auto &o : observations)
        definitions.push_back(o.step);
    validateSteps(definitions);
    std::int64_t numerator = 0, denominator = 0;
    for (const auto &o : observations) {
        if (o.basisPoints < 0 || o.basisPoints > 10000)
            invalid("Step 진척은 0~100%입니다.");
        denominator += o.step.weight;
        numerator += std::int64_t(o.step.weight) * o.basisPoints;
    }
    return int((numerator + denominator / 2) / denominator);
}
void validateStepReports(const Project &p) {
    if (p.stepReports.size() > 10000)
        invalid("Steps 보고는 최대10,000개입니다.");
    std::set<std::string> ids, tasks;
    for (const auto &t : p.tasks)
        tasks.insert(t.id);
    std::size_t observations = 0;
    for (const auto &r : p.stepReports) {
        calculationCheckpoint();
        if (!label(r.id) || !ids.insert(r.id).second || !tasks.contains(r.taskId) || !validStamp(r.statusAt) ||
            !validUtcStamp(r.recordedAt) || !label(r.reporter) || !label(r.reason) || !label(r.sourceDocumentId) ||
            !r.recordedRevision || r.recordedRevision > p.revision)
            invalid("Steps 보고 출처·상태일·작업 참조를 확인하세요.");
        (void)weightedStepProgress(r.steps);
        observations += r.steps.size();
        if (observations > 100000)
            invalid("Steps 보고 관측값 합계는 최대100,000개입니다.");
    }
}
const StepReport *stepsAsOf(const Project &p, const std::string &taskId, const std::string &at) {
    const StepReport *found = nullptr;
    for (const auto &r : p.stepReports)
        if (r.taskId == taskId && r.statusAt <= at && (!found || r.statusAt >= found->statusAt))
            found = &r;
    return found;
}
std::optional<int> stepsPhysicalAsOf(const Project &p, const std::string &taskId, const std::string &at) {
    const auto definition = std::find_if(p.activities.begin(), p.activities.end(), [&](const auto &a) {
        return a.kind == "steps" && a.taskId == taskId && a.physicalFromSteps;
    });
    const auto *r = definition == p.activities.end() ? nullptr : stepsAsOf(p, taskId, at);
    return r ? std::optional<int>{weightedStepProgress(r->steps)} : std::nullopt;
}
} // namespace jobs
