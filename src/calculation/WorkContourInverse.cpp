#include "calculation/WorkContourInverse.h"
#include <algorithm>
#include <map>
#include <set>
namespace jobs {
void certifyWorkContourDurations(const Project &p, const std::vector<std::size_t> &indices) {
    std::vector<std::size_t> curved;
    for (auto i : indices)
        if (p.tasks.at(i).taskType == TaskType::FixedWork &&
            std::any_of(p.assignments.begin(), p.assignments.end(),
                        [&](const auto &a) { return a.taskId == p.tasks[i].id && !a.periods.empty(); }))
            curved.push_back(i);
    if (curved.empty())
        return;
    auto basis = p;
    basis.planLeveling.reset();
    basis.remainingLeveling.reset();
    basis.levelingDecisions.clear();
    const auto plan = calculatePlan(basis);
    if (plan.status != CalculationStatus::Valid)
        throw AppError(ErrorCode::INVERSE_PLAN_REQUIRED);
    for (auto i : curved) {
        const auto row = std::find_if(plan.tasks.begin(), plan.tasks.end(),
                                      [&](const auto &v) { return v.taskId == p.tasks[i].id; });
        TaskExecution model(basis, basis.tasks[i]);
        if (row == plan.tasks.end() || model.duration(*row) != p.tasks[i].durationMinutes ||
            model.work(*row) != p.tasks[i].workUnitMinutes)
            throw AppError(
                ErrorCode::ASSIGNMENT_PERIOD_INVALID,
                {"최종 논리 일정에서 요청 기간·고정 공수가 일치하지 않습니다. 제약·선후행·달력을 검토하세요."});
    }
}
void changeAssignmentContour(Project &p, const std::string &taskId, const std::string &resourceId,
                             std::vector<AssignmentPeriod> periods) {
    auto a = std::find_if(p.assignments.begin(), p.assignments.end(),
                          [&](const auto &v) { return v.taskId == taskId && v.resourceId == resourceId; });
    if (a == p.assignments.end())
        throw AppError(ErrorCode::ASSIGNMENT_INPUT_INVALID);
    if (a->periods == periods)
        return;
    auto t = std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &v) { return v.id == taskId; });
    if (t == p.tasks.end())
        throw AppError(ErrorCode::TASK_NOT_FOUND);
    if (t->taskType == TaskType::FixedUnits && !t->workDriven) {
        auto basis = p;
        basis.planLeveling.reset();
        basis.remainingLeveling.reset();
        basis.levelingDecisions.clear();
        const auto plan = calculatePlan(basis);
        if (plan.status != CalculationStatus::Valid)
            throw AppError(ErrorCode::FIXED_UNITS_PLAN_REQUIRED);
        const auto row =
            std::find_if(plan.tasks.begin(), plan.tasks.end(), [&](const auto &v) { return v.taskId == taskId; });
        const auto work = TaskExecution(basis, *t).work(*row);
        if (work > 0) {
            t->workUnitMinutes = work;
            t->workDriven = true;
        }
    }
    if (!periods.empty())
        p.calculationProfile = effortProfile;
    a->periods = std::move(periods);
}
namespace {
using Minute = std::int64_t;
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID, {message});
}
void append(std::vector<AssignmentPeriod> &v, Minute a, Minute b, int units) {
    if (a >= b)
        return;
    if (!v.empty() && v.back().toMinute == a && v.back().units == units)
        v.back().toMinute = b;
    else
        v.push_back({a, b, units});
}
std::vector<AssignmentPeriod> overlay(const std::vector<AssignmentPeriod> &original,
                                      const std::vector<AssignmentPeriod> &changes) {
    std::set<Minute> cuts;
    for (const auto &v : original) {
        cuts.insert(v.fromMinute);
        cuts.insert(v.toMinute);
    }
    for (const auto &v : changes) {
        cuts.insert(v.fromMinute);
        cuts.insert(v.toMinute);
    }
    std::vector<AssignmentPeriod> result;
    std::size_t old = 0, edit = 0;
    for (auto at = cuts.begin(); at != cuts.end();) {
        const auto a = *at++;
        if (at == cuts.end())
            break;
        const auto b = *at;
        while (old < original.size() && original[old].toMinute <= a)
            ++old;
        while (edit < changes.size() && changes[edit].toMinute <= a)
            ++edit;
        if (edit < changes.size() && changes[edit].fromMinute <= a)
            append(result, a, b, changes[edit].units);
        else if (old < original.size() && original[old].fromMinute <= a)
            append(result, a, b, original[old].units);
    }
    if (result.size() > 1024)
        invalid("역산 후 배정당 1,024개 구간 한도를 넘습니다.");
    return result;
}
} // namespace
void scaleWorkContours(Project &p, std::size_t index, Minute target, Minute duration, const TaskSchedule &source) {
    if (duration <= 0 || duration > 5000000)
        invalid("곡선 역산은 양수 기간이 필요합니다.");
    auto task = p.tasks.at(index);
    task.taskType = TaskType::FixedDuration;
    task.workDriven = false;
    task.workUnitMinutes.reset();
    task.durationMinutes = duration;
    TaskExecution model(p, task);
    auto spans = model.spans(source);
    Minute work = 0;
    for (const auto &s : spans)
        work += (s.finish - s.start) * s.units;
    if (!work && target)
        invalid("목표 기간 안의 양수 곡선·자원 근무시간이 없습니다.");
    if (p.tasks[index].taskType == TaskType::FixedWork &&
        (!target || spans.empty() || std::max_element(spans.begin(), spans.end(), [](const auto &a, const auto &b) {
                                         return a.finish < b.finish;
                                     })->finish != source.earlyFinishMinutes))
        invalid("고정 공수는 지정 완료 시각의 양수 자원 근무분이 필요합니다.");
    if (target == work)
        return;
    struct Slice {
        WorkSpan span;
        int units;
    };
    std::vector<Slice> scaled;
    Minute left = target;
    for (const auto &s : spans) {
        calculationCheckpoint();
        const auto units = work ? Minute(s.units) * target / work : 0;
        if (units > 100000)
            invalid("역산 배정률이 100,000%를 넘습니다.");
        scaled.push_back({s, int(units)});
        left -= (s.finish - s.start) * units;
    }
    std::sort(scaled.begin(), scaled.end(), [](const auto &a, const auto &b) {
        if (a.span.finish != b.span.finish)
            return a.span.finish > b.span.finish;
        return a.span.resourceId < b.span.resourceId;
    });
    const WorkCalendar clock(p);
    const auto origin = model.calendar().offset(source.start);
    std::map<std::string, std::vector<AssignmentPeriod>> changes;
    for (const auto &v : scaled) {
        calculationCheckpoint();
        const auto &s = v.span;
        const auto from = model.calendar().offset(clock.boundary(s.start, false)) - origin;
        const auto to = from + s.finish - s.start;
        const auto extra = v.units < 100000 ? std::min(left, to - from) : 0;
        left -= extra;
        auto &out = changes[s.resourceId];
        // Allocate integer remainders at the end, so positive fixed work can
        // finish at the requested final working minute even for a tiny target.
        out.push_back({from, to - extra, v.units});
        if (extra)
            out.push_back({to - extra, to, v.units + 1});
    }
    if (left)
        invalid("정수 배정률 한도 안에서 목표 공수를 배분할 수 없습니다.");
    auto candidate = p;
    for (auto &a : candidate.assignments)
        if (a.taskId == task.id) {
            auto &v = changes[a.resourceId];
            std::erase_if(v, [](const auto &x) { return x.fromMinute >= x.toMinute; });
            std::sort(v.begin(), v.end(), [](const auto &x, const auto &y) { return x.fromMinute < y.fromMinute; });
            a.periods = overlay(a.periods, v);
        }
    auto verifyTask = p.tasks[index];
    verifyTask.durationMinutes = duration;
    if (verifyTask.taskType != TaskType::FixedWork)
        verifyTask = task;
    TaskExecution verify(candidate, verifyTask);
    auto row = verify.dates(verify.offset(source.start));
    if (row.start != source.start || row.finish != source.finish || verify.work(row) != target)
        invalid("지정 기간·공수의 곡선 검산이 일치하지 않습니다.");
    p.assignments = std::move(candidate.assignments);
}
} // namespace jobs
