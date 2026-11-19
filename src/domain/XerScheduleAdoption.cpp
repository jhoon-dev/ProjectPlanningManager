#include "domain/XerScheduleAdoption.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
namespace ppm {
namespace {
[[noreturn]] void invalid(const char *s) {
    throw AppError(ErrorCode::FILE_PRIMAVERAXER_080, {s});
}
bool label(const std::string &s, std::size_t max = 1024) {
    return !s.empty() && s.size() <= max && std::any_of(s.begin(), s.end(), [](unsigned char c) { return c > 32; }) &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool digest(const std::string &s) {
    return s.size() == 64 &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
} // namespace
std::vector<XerScheduleLink> suggestXerScheduleLinks(const PlanInput &source, const PlanInput &target) {
    std::set<std::string> ids;
    std::map<std::string, std::vector<std::string>> names;
    for (const auto &t : target.tasks) {
        ids.insert(t.id);
        names[t.name].push_back(t.id);
    }
    std::vector<XerScheduleLink> out;
    for (const auto &t : source.tasks) {
        const auto it = names.find(t.name);
        out.push_back({t.id,
                       ids.contains(t.id)                            ? t.id
                       : it != names.end() && it->second.size() == 1 ? it->second[0]
                                                                     : "",
                       false});
    }
    return out;
}
PlanInput adoptXerSchedule(const PlanInput &before, const XerScheduleSubmission &s, const std::string &id) {
    calculationCheckpoint();
    validatePlan(before);
    validatePlan(s.source.input);
    if (!label(id) || !s.confirmed || (!s.schedule && !s.wbs && !s.calendars) || !label(s.reporter) ||
        !label(s.reason, 4096) || !validUtcStamp(s.recordedAt) || !label(s.source.sourceName, 4096) ||
        !digest(s.source.sourceSha256) || !label(s.source.projectId) || !validUtcStamp(s.source.importedAt) ||
        (s.source.statusAt && !validStamp(*s.source.statusAt)) || s.source.reportJson.size() > 1024 * 1024)
        invalid("채택할 원본·범위·확인·작성자·사유·출처를 확인하세요.");
    if (s.links.size() != s.source.input.tasks.size())
        invalid("새 원본의 모든 작업에 현재 대상·새 작업·제외 중 하나를 명시하세요.");
    auto result = before;
    std::set<std::string> occupied, currentTasks, used;
    for (const auto &t : before.tasks) {
        occupied.insert(t.id);
        currentTasks.insert(t.id);
    }
    for (const auto &r : before.resources)
        occupied.insert(r.id);
    for (const auto &w : before.wbsNodes)
        occupied.insert(w.id);
    for (const auto &c : before.calendars)
        occupied.insert(c.id);
    auto fresh = [&](const char *kind, const std::string &source) {
        auto name = "xsa-" + id + "-" + kind + "-" + source;
        if (!label(name) || !occupied.insert(name).second)
            invalid("새 작업·WBS·달력 ID가 기존 ID와 충돌합니다. 다시 미리보기를 실행하세요.");
        return name;
    };
    std::map<std::string, std::string> taskMap, wbsMap, calendarMap;
    std::set<std::string> created;
    for (const auto &link : s.links) {
        calculationCheckpoint();
        auto target = link.targetId;
        if (link.create) {
            if (!s.schedule || !target.empty())
                invalid("새 작업 추가에는 일정 채택과 빈 기존 대상이 필요합니다.");
            target = fresh("task", link.sourceId);
            created.insert(target);
        } else if (!target.empty() && !currentTasks.contains(target))
            invalid("연결할 현재 작업이 없습니다.");
        if (!taskMap.emplace(link.sourceId, target).second || (!target.empty() && !used.insert(target).second))
            invalid("원본 작업 또는 현재 연결 대상이 중복되었습니다.");
    }
    std::size_t selected = 0;
    for (const auto &t : s.source.input.tasks) {
        if (!taskMap.contains(t.id))
            invalid("연결표의 원본 작업 ID가 일치하지 않습니다.");
        selected += !taskMap.at(t.id).empty();
    }
    if (!selected)
        invalid("하나 이상의 작업을 연결하거나 새 작업으로 추가하세요.");
    if (s.wbs) {
        for (const auto &w : s.source.input.wbsNodes)
            wbsMap[w.id] = fresh("wbs", w.id);
        for (auto w : s.source.input.wbsNodes) {
            w.id = wbsMap.at(w.id);
            if (!w.parentId.empty())
                w.parentId = wbsMap.at(w.parentId);
            result.wbsNodes.push_back(std::move(w));
        }
    }
    if (s.calendars) {
        for (const auto &c : s.source.input.calendars)
            calendarMap[c.id] = fresh("calendar", c.id);
        for (auto c : s.source.input.calendars) {
            c.id = calendarMap.at(c.id);
            result.calendars.push_back(std::move(c));
        }
        result.calendar = s.source.input.calendar;
        result.holidays = s.source.input.holidays;
    }
    std::map<std::string, std::size_t> rows;
    for (std::size_t i = 0; i < result.tasks.size(); ++i)
        rows[result.tasks[i].id] = i;
    for (const auto &t : s.source.input.tasks) {
        calculationCheckpoint();
        const auto &target = taskMap.at(t.id);
        if (target.empty())
            continue;
        if (created.contains(target)) {
            Task task;
            task.id = target;
            task.name = t.name;
            rows[target] = result.tasks.size();
            result.tasks.push_back(std::move(task));
        }
        auto &task = result.tasks.at(rows.at(target));
        if (s.schedule) {
            task.name = t.name;
            task.durationMinutes = t.durationMinutes;
            task.predecessors.clear();
            task.dependencyRules.clear();
            for (const auto &pred : t.predecessors) {
                if (taskMap.at(pred).empty())
                    invalid("채택할 작업의 선행 작업이 제외되었습니다. 선행 작업도 연결하세요.");
                task.predecessors.push_back(taskMap.at(pred));
            }
            for (auto edge : t.dependencyRules) {
                edge.predecessorId = taskMap.at(edge.predecessorId);
                task.dependencyRules.push_back(std::move(edge));
            }
            task.constraintType = t.constraintType;
            task.constraintAt = t.constraintAt;
            task.deadlineAt = t.deadlineAt;
            task.taskType = t.taskType;
            task.workUnitMinutes = t.workUnitMinutes;
            task.workDriven = t.workDriven;
            task.breaks = t.breaks;
        }
        if (s.wbs)
            task.wbsId = t.wbsId.empty() ? "" : wbsMap.at(t.wbsId);
        if (s.calendars)
            task.calendarId = t.calendarId.empty() ? "" : calendarMap.at(t.calendarId);
    }
    if (s.schedule)
        result.startDate = s.source.input.startDate;
    if (s.schedule)
        for (const auto &a : result.assignments)
            if (used.contains(a.taskId)) {
                const auto &t = result.tasks.at(rows.at(a.taskId));
                if (t.taskType == TaskType::FixedDuration && t.durationMinutes &&
                    std::any_of(a.periods.begin(), a.periods.end(),
                                [&](const auto &period) { return period.toMinute > *t.durationMinutes; }))
                    invalid("기존 배정 구간이 채택 기간을 넘습니다. 배정 구간을 명시적으로 조정한 뒤 다시 채택하세요.");
            }
    if (s.schedule || s.calendars)
        result.calculationProfile = effortProfile;
    result.planLeveling.reset();
    validatePlan(result);
    return result;
}
void validateXerScheduleAdoptions(const Project &p) {
    if (p.xerScheduleAdoptions.size() > 128)
        invalid("일정 채택 이력은 최대128건입니다.");
    std::set<std::string> ids;
    std::uint64_t revision = 0;
    const PlanInput *previous = nullptr;
    std::size_t rows = 0;
    for (const auto &e : p.xerScheduleAdoptions) {
        calculationCheckpoint();
        for (const auto *input : {&e.before, &e.after, &e.source.input})
            rows += input->tasks.size() + input->wbsNodes.size() + input->calendars.size() + input->resources.size() +
                    input->assignments.size();
        if (rows > 300000 || !ids.insert(e.id).second || !label(e.sourceDocumentId) || e.recordedRevision < 2 ||
            e.recordedRevision <= revision || e.recordedRevision > p.revision ||
            (previous && e.recordedRevision == revision + 1 && e.before != *previous) ||
            adoptXerSchedule(e.before, e, e.id) != e.after)
            invalid("저장된 일정 채택 이력·revision·원본/연결표와 전후 계획이 일치하지 않습니다.");
        revision = e.recordedRevision;
        previous = &e.after;
    }
}
} // namespace ppm
