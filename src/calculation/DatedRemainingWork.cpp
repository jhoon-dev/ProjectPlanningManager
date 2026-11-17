#include "calculation/DatedRemainingWork.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
namespace jobs {
namespace {
using Minute = std::int64_t;
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::REMAINING_WORK_INVALID, {message}, {}, "DATED_REMAINING_INVALID");
}
std::pair<Minute, Minute> dayRange(const WorkCalendar &clock, const std::string &date) {
    const auto begin = clock.offset(date + " 00:00");
    // All execution endpoints are strictly inside the supported horizon.
    return {begin, begin + 1440};
}
std::string nextDate(const std::string &s) {
    using namespace std::chrono;
    auto date = year_month_day{sys_days{year{std::stoi(s.substr(0, 4))} / unsigned(std::stoi(s.substr(5, 2))) /
                                        unsigned(std::stoi(s.substr(8, 2)))} +
                               days{1}};
    char out[20];
    std::snprintf(out, sizeof(out), "%04d-%02u-%02u", int(date.year()), unsigned(date.month()), unsigned(date.day()));
    return out;
}
} // namespace
std::vector<WorkSpan> applyDatedRemainingWork(const std::optional<std::string> &statusAt,
                                              const std::vector<RemainingWorkPlan> &plans, const TaskSchedule &row,
                                              const WorkCalendar &clock, const std::vector<WorkSpan> &availability,
                                              std::vector<WorkSpan> spans) {
    if (plans.empty() || (!row.remainingStart && !row.completedActual))
        return spans;
    for (const auto &plan : plans) {
        calculationCheckpoint();
        if (!statusAt || plan.statusAt != *statusAt)
            invalid("상태일이 변경되었습니다. 날짜별 잔여 공수를 다시 적용하거나 해제하세요: " + plan.taskId);
        if (row.completedActual)
            invalid("완료 작업의 날짜별 잔여 공수를 해제하세요: " + plan.taskId);
        const auto begin = clock.offset(*row.remainingStart), end = clock.offset(row.finish);
        for (const auto &day : plan.days) {
            calculationCheckpoint();
            if (day.date < row.remainingStart->substr(0, 10) || day.date > row.finish.substr(0, 10))
                invalid("현재 잔여 실행 구간 밖 날짜를 재검토하세요: " + day.date);
            const auto [start, finish] = dayRange(clock, day.date);
            const auto lo = std::max(begin, start), hi = std::min(end, finish);
            Minute available = 0;
            for (const auto &s : availability)
                if (s.resourceId == plan.resourceId)
                    available += std::max(Minute{0}, std::min(hi, s.finish) - std::max(lo, s.start));
            const auto work = day.workUnitMinutes;
            if ((!available && work) || work > available * 100000)
                invalid(day.date + ": 자원 근무시간이 없거나 필요한 배정률이 100,000%를 넘습니다.");
            std::vector<WorkSpan> kept;
            for (const auto &s : spans) {
                if (s.resourceId != plan.resourceId || s.finish <= start || s.start >= finish) {
                    kept.push_back(s);
                    continue;
                }
                if (s.start < start)
                    kept.push_back({s.resourceId, s.start, start, s.units});
                if (s.finish > finish)
                    kept.push_back({s.resourceId, finish, s.finish, s.units});
            }
            auto remainder = available ? work % available : 0;
            const auto rate = available ? work / available : 0;
            Minute certified = 0;
            for (const auto &s : availability)
                if (s.resourceId == plan.resourceId) {
                    const auto a = std::max(lo, s.start), b = std::min(hi, s.finish);
                    if (a >= b)
                        continue;
                    const auto extra = std::min(remainder, b - a);
                    remainder -= extra;
                    if (extra) {
                        kept.push_back({s.resourceId, a, a + extra, int(rate + 1)});
                        certified += extra * (rate + 1);
                    }
                    if (a + extra < b && rate) {
                        kept.push_back({s.resourceId, a + extra, b, int(rate)});
                        certified += (b - a - extra) * rate;
                    }
                }
            if (certified != work)
                invalid("날짜별 정수 공수 검산이 일치하지 않습니다.");
            spans = std::move(kept);
        }
    }
    return spans;
}
void validateRemainingWorkExecution(const Project &p, const ScheduleResult &schedule) {
    if (p.remainingWork.empty() || !schedule.isForecast || schedule.status != CalculationStatus::Valid)
        return;
    std::map<std::string, const TaskSchedule *> rows;
    for (const auto &r : schedule.tasks)
        rows[r.taskId] = &r;
    ExecutionInputs inputs(p);
    for (const auto &t : p.tasks) {
        if (std::none_of(p.remainingWork.begin(), p.remainingWork.end(),
                         [&](const auto &x) { return x.taskId == t.id; }))
            continue;
        calculationCheckpoint();
        if (!rows.contains(t.id))
            invalid("작업의 잔여 실행 결과가 없습니다.");
        TaskExecution model(p, pendingTask(p, t), &inputs);
        (void)model.spans(*rows.at(t.id));
    }
}
RemainingWorkDescription describeRemainingWork(const Project &source, const std::string &taskId,
                                               const std::string &resourceId,
                                               std::shared_ptr<CalculationControl> control) {
    EvaluationScope scope(std::move(control));
    validate(source);
    if (!source.statusAt)
        invalid("상태일을 먼저 설정하세요.");
    auto p = source;
    p.remainingWork.clear();
    p.planLeveling.reset();
    p.remainingLeveling.reset();
    p.levelingDecisions.clear();
    p.calculationProfile = effortProfile;
    auto task = std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &t) { return t.id == taskId; });
    if (task == p.tasks.end() ||
        (usesWork(*task) ? task->workUnitMinutes.value_or(0) <= 0 : task->durationMinutes.value_or(0) <= 0))
        invalid("양수 기간/공수가 있는 잔여 작업을 선택하세요.");
    if (std::none_of(p.assignments.begin(), p.assignments.end(),
                     [&](const auto &a) { return a.taskId == taskId && a.resourceId == resourceId; }))
        invalid("현재 자원 배정을 선택하세요.");
    const auto forecast = calculate(p);
    if (forecast.status != CalculationStatus::Valid)
        invalid("정상 잔여 예측을 먼저 계산하세요.");
    const auto found =
        std::find_if(forecast.tasks.begin(), forecast.tasks.end(), [&](const auto &r) { return r.taskId == taskId; });
    if (found == forecast.tasks.end() || found->completedActual || !found->remainingStart)
        invalid("완료하지 않은 잔여 작업을 선택하세요.");
    const auto pending = pendingTask(p, *task);
    TaskExecution inherited(p, pending);
    ExecutionInputs inputs(p);
    inputs.assignments[taskId] = {{taskId, resourceId, 1}};
    auto capacityTask = pending;
    // Calendar capacity must not be trimmed to a work target at the artificial 1% rate.
    capacityTask.taskType = TaskType::FixedDuration;
    capacityTask.workDriven = false;
    capacityTask.workUnitMinutes.reset();
    TaskExecution capacity(p, capacityTask, &inputs);
    const WorkCalendar clock(p);
    std::map<std::string, RemainingWorkDay> days;
    // Include task working days, even when the assigned resource is unavailable.
    for (const auto &[a, b] : capacity.calendar().segments(*found->remainingStart, found->finish)) {
        for (auto day = a.substr(0, 10); day <= b.substr(0, 10); day = nextDate(day)) {
            if (days.size() >= 1024 && !days.contains(day))
                invalid("잔여 날짜 행은 1,024개까지입니다.");
            days[day].date = day;
            if (day == "9999-12-31")
                break;
        }
    }
    RemainingWorkDescription out;
    out.dates = *found;
    auto capacityRow = *found;
    for (const auto &plan : source.remainingWork)
        if (plan.taskId == taskId && plan.resourceId == resourceId) {
            out.storedStatusAt = plan.statusAt;
            for (const auto &day : plan.days) {
                auto &v = days[day.date];
                v.date = day.date;
                v.target = day.workUnitMinutes;
                v.outside = day.date < found->remainingStart->substr(0, 10) ||
                            (!usesWork(*task) && day.date > found->finish.substr(0, 10));
                if (usesWork(*task) && day.date >= found->remainingStart->substr(0, 10))
                    capacityRow.finish = std::max(capacityRow.finish, day.date + " 23:59");
            }
        }
    auto add = [&](const std::vector<WorkSpan> &spans, bool available) {
        for (const auto &s : spans)
            if (s.resourceId == resourceId) {
                auto at = s.start;
                while (at < s.finish) {
                    calculationCheckpoint();
                    const auto stamp = clock.boundary(at, false);
                    const auto day = stamp.substr(0, 10);
                    const auto [a, b] = dayRange(clock, day);
                    (void)a;
                    const auto end = std::min(b, s.finish);
                    if (!available || days.contains(day)) {
                        auto &v = days[day];
                        v.date = day;
                        if (available)
                            v.availableMinutes += end - at;
                        else
                            v.inheritedWork += (end - at) * s.units;
                    }
                    at = end;
                }
            }
    };
    add(capacity.spans(capacityRow), true);
    add(inherited.spans(*found), false);
    if (days.size() > 1024)
        invalid("현재/저장 날짜 합계가 1,024개를 넘습니다. 저장 입력을 해제하고 다시 작성하세요.");
    for (auto &[date, day] : days) {
        (void)date;
        out.days.push_back(std::move(day));
    }
    return out;
}
} // namespace jobs
