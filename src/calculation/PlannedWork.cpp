#include "calculation/PlannedWork.h"
#include "calculation/Effort.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
namespace jobs {
namespace {
using Minute = std::int64_t;
using Interval = std::pair<Minute, Minute>;
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID, {message});
}
struct Basis {
    Project plan;
    std::size_t taskIndex = 0, assignmentIndex = 0;
    PlannedWorkResult result;
    std::vector<Interval> available;
    std::vector<WorkSpan> demand;
    explicit Basis(const Project &source, const std::string &taskId, const std::string &resourceId) : plan(source) {
        calculationCheckpoint();
        validate(source);
        auto task = std::find_if(plan.tasks.begin(), plan.tasks.end(), [&](const auto &t) { return t.id == taskId; });
        auto assignment = std::find_if(plan.assignments.begin(), plan.assignments.end(),
                                       [&](const auto &a) { return a.taskId == taskId && a.resourceId == resourceId; });
        if (task == plan.tasks.end() || assignment == plan.assignments.end())
            invalid("작업·자원 배정을 확인하세요.");
        if (task->taskType != TaskType::FixedDuration || task->workDriven || task->durationMinutes.value_or(0) <= 0)
            invalid("공수 변환은 기간이 확정된 고정 기간 작업만 지원합니다.");
        if (std::any_of(plan.activities.begin(), plan.activities.end(),
                        [&](const auto &a) { return a.taskId == taskId && a.kind != "steps"; }))
            invalid("지원 활동의 공수 변환은 지원하지 않습니다.");
        taskIndex = std::size_t(task - plan.tasks.begin());
        assignmentIndex = std::size_t(assignment - plan.assignments.begin());
        plan.calculationProfile = effortProfile;
        plan.planLeveling.reset();
        plan.remainingLeveling.reset();
        plan.levelingDecisions.clear();
        const auto schedule = calculatePlan(plan);
        if (schedule.status != CalculationStatus::Valid)
            invalid("정상 논리 계획을 계산한 뒤 공수를 변환하세요.");
        const auto row = std::find_if(schedule.tasks.begin(), schedule.tasks.end(),
                                      [&](const auto &r) { return r.taskId == taskId; });
        if (row == schedule.tasks.end())
            invalid("계획 날짜가 없습니다.");
        result.documentId = source.documentId;
        result.revision = source.revision;
        result.taskId = taskId;
        result.resourceId = resourceId;
        result.dates = *row;
        result.periods = assignment->periods;
        ExecutionInputs inputs(plan);
        inputs.assignments[taskId] = {{taskId, resourceId, 1}};
        TaskExecution capacity(plan, *task, &inputs);
        const WorkCalendar clock(plan);
        const auto origin = capacity.calendar().offset(row->start);
        for (const auto &s : capacity.spans(*row)) {
            const auto start = capacity.calendar().offset(clock.boundary(s.start, false)) - origin;
            available.emplace_back(start, start + s.finish - s.start);
        }
        TaskExecution model(plan, *task);
        for (const auto &s : model.spans(*row))
            if (s.resourceId == resourceId) {
                const auto start = model.calendar().offset(clock.boundary(s.start, false)) - origin;
                demand.push_back({resourceId, start, start + s.finish - s.start, s.units});
            }
    }
    Minute availableWithin(Minute from, Minute to) const {
        Minute total = 0;
        for (const auto &[a, b] : available) {
            calculationCheckpoint();
            total += std::max(Minute{0}, std::min(to, b) - std::max(from, a));
        }
        return total;
    }
    PlannedWorkBucket bucket(Minute from, Minute to) const {
        PlannedWorkBucket out;
        out.amount = {from, to, 0};
        const WorkCalendar calendar(plan, plan.tasks[taskIndex]);
        const auto origin = calendar.offset(result.dates.start);
        out.start = calendar.boundary(origin + from, false);
        out.finish = calendar.boundary(origin + to, true);
        out.availableMinutes = availableWithin(from, to);
        for (const auto &s : demand) {
            calculationCheckpoint();
            out.amount.workUnitMinutes +=
                std::max(Minute{0}, std::min(to, s.finish) - std::max(from, s.start)) * s.units;
        }
        if (out.amount.workUnitMinutes > maxWork)
            invalid("구간 공수가 허용 범위를 넘습니다.");
        return out;
    }
};
void append(std::vector<AssignmentPeriod> &out, Minute from, Minute to, int units) {
    if (from == to)
        return;
    if (!out.empty() && out.back().toMinute == from && out.back().units == units)
        out.back().toMinute = to;
    else
        out.push_back({from, to, units});
}
} // namespace
std::string plannedWorkHours(Minute value) {
    if (value < 0 || value > maxWork)
        invalid("공수 범위를 확인하세요.");
    const auto scaled = (value * 10000 + 3000) / 6000;
    auto fraction = std::to_string(scaled % 10000);
    return std::to_string(scaled / 10000) + "." + std::string(4 - fraction.size(), '0') + fraction;
}
Minute parsePlannedWorkHours(const std::string &value) {
    if (value.empty() || value.size() > 32)
        invalid("공수를 소수 4자리 이하 시간으로 입력하세요.");
    Minute whole = 0, fraction = 0;
    int digits = 0;
    bool dot = false;
    for (char c : value) {
        if (c == '.' && !dot) {
            dot = true;
            continue;
        }
        if (c < '0' || c > '9' || (dot && ++digits > 4))
            invalid("공수는 음수·지수·쉼표 없이 소수 4자리 이하 시간입니다.");
        if (dot)
            fraction = fraction * 10 + c - '0';
        else {
            whole = whole * 10 + c - '0';
            if (whole > maxWork / 6000 + 1)
                invalid("공수가 허용 범위를 넘습니다.");
        }
    }
    if (value == "." || value.front() == '.' || value.back() == '.')
        invalid("숫자 형식을 확인하세요.");
    while (digits++ < 4)
        fraction *= 10;
    const auto amount = ((whole * 10000 + fraction) * 6000 + 5000) / 10000;
    if (amount > maxWork)
        invalid("공수가 허용 범위를 넘습니다.");
    return amount;
}
PlannedWorkResult describePlannedWork(const Project &source, const std::string &taskId, const std::string &resourceId,
                                      PlannedWorkPeriod period, std::shared_ptr<CalculationControl> control) {
    EvaluationScope scope(std::move(control));
    if (period != PlannedWorkPeriod::Day && period != PlannedWorkPeriod::Week && period != PlannedWorkPeriod::Month)
        invalid("기간 단위를 확인하세요.");
    Basis basis(source, taskId, resourceId);
    using namespace std::chrono;
    const auto parse = [](const std::string &s) {
        return sys_days{year{std::stoi(s.substr(0, 4))} / unsigned(std::stoi(s.substr(5, 2))) /
                        unsigned(std::stoi(s.substr(8, 2)))};
    };
    const auto date = [](sys_days day) {
        const year_month_day ymd{day};
        char text[32];
        std::snprintf(text, sizeof(text), "%04d-%02u-%02u 00:00", int(ymd.year()), unsigned(ymd.month()),
                      unsigned(ymd.day()));
        return std::string(text);
    };
    const WorkCalendar calendar(basis.plan, basis.plan.tasks[basis.taskIndex]);
    const auto origin = calendar.offset(basis.result.dates.start),
               duration = *basis.plan.tasks[basis.taskIndex].durationMinutes;
    std::set<Minute> cuts{0, duration};
    for (auto day = parse(basis.result.dates.start); day <= parse(basis.result.dates.finish);) {
        calculationCheckpoint();
        if (period == PlannedWorkPeriod::Day)
            day += days{1};
        else if (period == PlannedWorkPeriod::Week)
            day += days{8 - int(weekday{day}.iso_encoding())};
        else {
            auto ymd = year_month_day{day};
            day = sys_days{ymd.year() / ymd.month() / 1 + months{1}};
        }
        // The finish stamp is inside the horizon, but the next bucket boundary may be outside it.
        if (day > parse(basis.result.dates.finish))
            break;
        const auto at = calendar.offset(date(day)) - origin;
        if (at > 0 && at < duration)
            cuts.insert(at);
    }
    if (cuts.size() > 1025)
        invalid("기간 행은 1,024개까지입니다. 주 또는 월 단위를 선택하세요.");
    for (auto it = cuts.begin(), next = std::next(it); next != cuts.end(); ++it, ++next)
        basis.result.buckets.push_back(basis.bucket(*it, *next));
    return std::move(basis.result);
}
PlannedWorkResult convertPlannedWork(const Project &source, const std::string &taskId, const std::string &resourceId,
                                     const std::vector<PlannedWorkAmount> &amounts,
                                     std::shared_ptr<CalculationControl> control) {
    EvaluationScope scope(std::move(control));
    if (amounts.empty() || amounts.size() > 1024)
        invalid("변환할 구간은 1~1,024개입니다.");
    Basis basis(source, taskId, resourceId);
    const auto duration = *basis.plan.tasks[basis.taskIndex].durationMinutes;
    std::vector<AssignmentPeriod> overrides;
    Minute previous = 0;
    for (const auto &amount : amounts) {
        calculationCheckpoint();
        const auto from = amount.fromMinute, to = amount.toMinute, work = amount.workUnitMinutes;
        if (from < previous || from >= to || to > duration || work < 0 || work > maxWork)
            invalid("정렬·비중복·현재 기간 안의 구간과 공수 범위를 확인하세요.");
        previous = to;
        if (basis.bucket(from, to).amount.workUnitMinutes == work)
            continue;
        const auto available = basis.availableWithin(from, to);
        if ((!available && work) || work > available * 100000)
            invalid("자원 근무시간이 없거나 필요한 배정률이 100,000%를 넘습니다.");
        const auto rate = available ? work / available : 0, remainder = available ? work % available : 0;
        Minute split = from, needed = remainder;
        for (const auto &[a, b] : basis.available) {
            calculationCheckpoint();
            if (!needed)
                break;
            const auto begin = std::max(from, a), end = std::min(to, b);
            if (begin >= end)
                continue;
            const auto consumed = std::min(needed, end - begin);
            split = begin + consumed;
            needed -= consumed;
        }
        if (remainder)
            append(overrides, from, split, int(rate + 1));
        append(overrides, split, to, int(rate));
    }
    if (overrides.empty()) {
        for (const auto &amount : amounts)
            basis.result.buckets.push_back(basis.bucket(amount.fromMinute, amount.toMinute));
        calculationCheckpoint();
        return std::move(basis.result);
    }
    std::set<Minute> cuts{0, duration};
    const auto original = basis.result.periods;
    for (const auto &v : original) {
        cuts.insert(v.fromMinute);
        cuts.insert(v.toMinute);
    }
    for (const auto &v : overrides) {
        cuts.insert(v.fromMinute);
        cuts.insert(v.toMinute);
    }
    std::vector<AssignmentPeriod> merged;
    std::size_t a = 0, b = 0;
    for (auto it = cuts.begin(), next = std::next(it); next != cuts.end(); ++it, ++next) {
        calculationCheckpoint();
        while (a < original.size() && original[a].toMinute <= *it)
            ++a;
        while (b < overrides.size() && overrides[b].toMinute <= *it)
            ++b;
        if (b < overrides.size() && overrides[b].fromMinute <= *it)
            append(merged, *it, *next, overrides[b].units);
        else if (a < original.size() && original[a].fromMinute <= *it)
            append(merged, *it, *next, original[a].units);
    }
    basis.plan.assignments[basis.assignmentIndex].periods = merged;
    validate(basis.plan);
    // Certify exact integer work using the shared execution model rather than the inverse formula alone.
    ExecutionInputs inputs(basis.plan);
    TaskExecution model(basis.plan, basis.plan.tasks[basis.taskIndex], &inputs);
    const WorkCalendar clock(basis.plan);
    const auto origin = model.calendar().offset(basis.result.dates.start);
    basis.demand.clear();
    for (const auto &s : model.spans(basis.result.dates))
        if (s.resourceId == resourceId) {
            const auto start = model.calendar().offset(clock.boundary(s.start, false)) - origin;
            basis.demand.push_back({resourceId, start, start + s.finish - s.start, s.units});
        }
    for (const auto &amount : amounts) {
        auto result = basis.bucket(amount.fromMinute, amount.toMinute);
        if (result.amount.workUnitMinutes != amount.workUnitMinutes)
            invalid("변환된 배정률의 공수 검산이 일치하지 않습니다.");
        basis.result.buckets.push_back(std::move(result));
    }
    basis.result.periods = std::move(merged);
    calculationCheckpoint();
    return std::move(basis.result);
}
} // namespace jobs
