#include "calculation/ResourcePlanning.h"
#include <algorithm>
#include <map>
#include <numeric>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(Diagnostic{"RESOURCE_PLANNING_INVALID", {s}, {}, "RESOURCE_PLANNING_INVALID"});
}
// Exact product/division without an overflowing intermediate (portable to MSVC).
std::pair<std::int64_t, std::int64_t> multiplyDivide(std::int64_t value, std::int64_t multiplier,
                                                     std::int64_t divisor) {
    std::int64_t quotient = 0, remainder = 0;
    for (int bit = 62; bit >= 0; --bit) {
        quotient *= 2;
        remainder *= 2;
        if ((value >> bit) & 1) {
            quotient += multiplier / divisor;
            remainder += multiplier % divisor;
        }
        quotient += remainder / divisor;
        remainder %= divisor;
    }
    return {quotient, remainder};
}
struct Basis {
    Project p;
    std::size_t task;
    TaskSchedule row;
    std::int64_t duration;
    std::vector<WorkSpan> spans;
    Basis(const Project &source, const std::string &id) : p(source) {
        p.calculationProfile = effortProfile;
        p.planLeveling.reset();
        p.remainingLeveling.reset();
        p.levelingDecisions.clear();
        validate(p);
        auto t = std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &v) { return v.id == id; });
        if (t == p.tasks.end())
            invalid("현재 작업을 확인하세요.");
        task = std::size_t(t - p.tasks.begin());
        const auto plan = calculatePlan(p);
        if (plan.status != CalculationStatus::Valid)
            invalid("정상 논리 계획을 먼저 계산하세요.");
        auto r = std::find_if(plan.tasks.begin(), plan.tasks.end(), [&](const auto &v) { return v.taskId == id; });
        if (r == plan.tasks.end())
            invalid("작업의 계획 실행이 없습니다.");
        row = *r;
        TaskExecution model(p, *t);
        duration = model.duration(row);
        spans = model.spans(row);
        if (duration <= 0 || duration > 5000000)
            invalid("양수 계산 기간(최대5,000,000분)이 필요합니다.");
    }
};
void append(std::vector<AssignmentPeriod> &out, std::int64_t a, std::int64_t b, int units) {
    if (a >= b)
        return;
    if (!out.empty() && out.back().toMinute == a && out.back().units == units)
        out.back().toMinute = b;
    else
        out.push_back({a, b, units});
}
std::vector<AssignmentPeriod> preserved(const Assignment &a, const Basis &b) {
    WorkCalendar clock(b.p), calendar(b.p, b.p.tasks[b.task]);
    const auto origin = calendar.offset(b.row.start);
    std::vector<AssignmentPeriod> result;
    std::int64_t at = 0;
    for (const auto &s : b.spans)
        if (s.resourceId == a.resourceId) {
            const auto begin = calendar.offset(clock.boundary(s.start, false)) - origin;
            append(result, at, begin, 0);
            append(result, begin, begin + s.finish - s.start, s.units);
            at = begin + s.finish - s.start;
        }
    append(result, at, b.duration, 0);
    for (const auto &v : a.periods)
        if (v.toMinute > b.duration)
            append(result, std::max(v.fromMinute, b.duration), v.toMinute, v.units);
    return result;
}
std::int64_t workFor(const std::vector<WorkSpan> &spans, const std::string &resource) {
    std::int64_t total = 0;
    for (const auto &s : spans)
        if (s.resourceId == resource)
            total += (s.finish - s.start) * s.units;
    return total;
}
} // namespace
NativeCurve mspWorkContour(int code) {
    // Published 5%-duration work percentages, stored in hundredths of a percent.
    static const std::vector<std::vector<int>> weights = {
        {0, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500},
        {0, 350, 350, 350, 350, 350, 350, 350, 350, 350, 350, 650, 650, 650, 650, 650, 650, 650, 650, 650, 650},
        {0, 650, 650, 650, 650, 650, 650, 650, 650, 650, 650, 350, 350, 350, 350, 350, 350, 350, 350, 350, 350},
        {0, 130, 250, 380, 510, 760, 1010, 760, 510, 380, 250, 250, 250, 380, 510, 760, 1010, 760, 510, 380, 250},
        {0, 120, 250, 380, 500, 750, 1010, 1010, 1010, 880, 750, 630, 500, 500, 500, 380, 250, 200, 150, 130, 100},
        {0, 100, 130, 150, 200, 250, 380, 500, 500, 500, 630, 750, 880, 1010, 1010, 1010, 750, 500, 380, 250, 120},
        {0, 50, 50, 150, 150, 400, 400, 750, 750, 1150, 1150, 1150, 1150, 750, 750, 400, 400, 150, 150, 50, 50},
        {0, 100, 100, 350, 350, 550, 550, 750, 750, 750, 750, 750, 750, 750, 750, 550, 550, 350, 350, 100, 100}};
    static const std::vector<std::string> names = {"Flat",       "Back Loaded", "Front Loaded", "Double Peak",
                                                   "Early Peak", "Late Peak",   "Bell",         "Turtle"};
    if (code < 0 || code > 7)
        invalid("MS Project의 기본 곡선 코드는0~7입니다. 수동 곡선은 시간대별 공수가 필요합니다.");
    NativeCurve c;
    c.source = "mspdi";
    c.externalId = std::to_string(code);
    c.name = names[code];
    c.mspCode = code;
    c.weights = weights[code];
    for (auto &weight : c.weights)
        weight *= 10000; // Millionths of one percent.
    return c;
}
Project materializeResourceWork(const Project &source, const std::string &taskId, const std::string &resourceId,
                                const std::vector<PlannedWorkAmount> &amounts) {
    Basis b(source, taskId);
    auto selected = std::find_if(b.p.assignments.begin(), b.p.assignments.end(),
                                 [&](const auto &a) { return a.taskId == taskId && a.resourceId == resourceId; });
    if (selected == b.p.assignments.end())
        invalid("현재 작업·자원 배정을 확인하세요.");
    const auto index = std::size_t(selected - b.p.assignments.begin());
    auto fixed = b.p;
    auto &temporary = fixed.tasks[b.task];
    temporary.taskType = TaskType::FixedDuration;
    temporary.workDriven = false;
    temporary.workUnitMinutes.reset();
    temporary.durationMinutes = b.duration;
    temporary.constraintType = ConstraintType::MSO;
    temporary.constraintAt = b.row.start;
    fixed.calculationProfile = effortProfile;
    for (auto &a : fixed.assignments)
        if (a.taskId == taskId)
            a.periods = preserved(a, b);
    const auto converted = convertPlannedWork(fixed, taskId, resourceId, amounts);
    auto out = source;
    out.calculationProfile = effortProfile;
    if (out.tasks[b.task].taskType != TaskType::FixedDuration) {
        for (std::size_t i = 0; i < out.assignments.size(); ++i)
            if (out.assignments[i].taskId == taskId)
                out.assignments[i].periods = fixed.assignments[i].periods;
    }
    out.assignments[index].periods = converted.periods;
    if (out.tasks[b.task].taskType != TaskType::FixedDuration) {
        auto physical = fixed;
        physical.assignments[index].periods = converted.periods;
        const auto sum = TaskExecution(physical, physical.tasks[b.task]).work(b.row);
        out.tasks[b.task].workDriven = true;
        out.tasks[b.task].workUnitMinutes = sum;
    }
    auto check = out;
    check.planLeveling.reset();
    check.remainingLeveling.reset();
    check.levelingDecisions.clear();
    validate(check);
    const auto plan = calculatePlan(check);
    if (plan.status != CalculationStatus::Valid)
        invalid("변환 후 계획을 계산할 수 없습니다.");
    const auto &row =
        *std::find_if(plan.tasks.begin(), plan.tasks.end(), [&](const auto &r) { return r.taskId == taskId; });
    const auto spans = TaskExecution(check, check.tasks[b.task]).spans(row);
    WorkCalendar clock(check), cal(check, check.tasks[b.task]);
    const auto origin = cal.offset(row.start);
    for (const auto &target : amounts) {
        std::int64_t work = 0;
        for (const auto &s : spans)
            if (s.resourceId == resourceId) {
                const auto at = cal.offset(clock.boundary(s.start, false)) - origin;
                work += std::max(std::int64_t{0},
                                 std::min(target.toMinute, at + s.finish - s.start) - std::max(target.fromMinute, at)) *
                        s.units;
            }
        if (work != target.workUnitMinutes)
            invalid("현재 달력·제약에서 요청한 구간 공수를 정확히 유지할 수 없습니다.");
    }
    for (const auto &a : source.assignments)
        if (a.taskId == taskId && a.resourceId != resourceId &&
            workFor(spans, a.resourceId) != workFor(b.spans, a.resourceId))
            invalid("다른 자원 배정의 계획 공수를 보존할 수 없습니다. 달력/제약을 검토하세요.");
    return out;
}
Project applyNativeCurve(const Project &source, const std::string &task, const std::string &resource, NativeCurve c,
                         std::optional<std::int64_t> totalWork) {
    Basis b(source, task);
    auto a = std::find_if(source.assignments.begin(), source.assignments.end(),
                          [&](const auto &v) { return v.taskId == task && v.resourceId == resource; });
    if (a == source.assignments.end())
        invalid("곡선의 현재 배정이 없습니다.");
    const auto work = totalWork.value_or(workFor(b.spans, resource));
    if (work < 0 || work > maxWork || c.weights.size() != 21 || c.weights[0] != 0 ||
        std::any_of(c.weights.begin(), c.weights.end(), [](int v) { return v < 0 || v > 100000000; }) ||
        std::accumulate(c.weights.begin(), c.weights.end(), std::int64_t{0}) != 100000000)
        invalid("21개 곡선 비중 합계100%와 총공수를 확인하세요.");
    if (b.duration < 20)
        invalid("21지점 곡선은20근무분 이상의 계획 기간이 필요합니다.");
    std::vector<PlannedWorkAmount> amounts;
    std::vector<std::pair<std::int64_t, std::size_t>> remainder;
    std::int64_t allocated = 0;
    for (int i = 0; i < 20; ++i) {
        const auto [part, residual] = multiplyDivide(work, c.weights[i + 1], 100000000);
        amounts.push_back({b.duration * i / 20, b.duration * (i + 1) / 20, part});
        allocated += part;
        remainder.emplace_back(residual, std::size_t(i));
    }
    std::stable_sort(remainder.begin(), remainder.end(),
                     [](const auto &x, const auto &y) { return x.first > y.first; });
    for (std::size_t i = 0; allocated < work; ++i, ++allocated)
        ++amounts[remainder[i].second].workUnitMinutes;
    auto out = materializeResourceWork(source, task, resource, amounts);
    auto target = std::find_if(out.assignments.begin(), out.assignments.end(),
                               [&](const auto &v) { return v.taskId == task && v.resourceId == resource; });
    c.durationMinutes = b.duration;
    c.workUnitMinutes = work;
    c.baseUnits = target->units;
    c.projectedPeriods = target->periods;
    target->nativeCurve = std::move(c);
    validateResourcePlanning(out);
    return out;
}
bool nativeCurveCurrent(const Assignment &a, std::int64_t duration, std::int64_t work) {
    return a.nativeCurve && a.nativeCurve->durationMinutes == duration && a.nativeCurve->workUnitMinutes == work &&
           a.nativeCurve->baseUnits == a.units && a.nativeCurve->projectedPeriods == a.periods;
}
CurveExport exportResourceCurve(const Project &source, const Assignment &a) {
    const auto task =
        std::find_if(source.tasks.begin(), source.tasks.end(), [&](const auto &t) { return t.id == a.taskId; });
    if (task != source.tasks.end() && (usesWork(*task) ? task->workUnitMinutes == 0 : task->durationMinutes == 0)) {
        auto p = source;
        p.planLeveling.reset();
        p.remainingLeveling.reset();
        p.levelingDecisions.clear();
        const auto plan = calculatePlan(p);
        if (plan.status != CalculationStatus::Valid)
            invalid("0공수 작업의 계획 시점을 계산할 수 없습니다.");
        CurveExport out;
        out.definition = mspWorkContour(0);
        out.definition.durationMinutes = 0;
        out.definition.workUnitMinutes = 0;
        out.dates =
            *std::find_if(plan.tasks.begin(), plan.tasks.end(), [&](const auto &r) { return r.taskId == a.taskId; });
        return out;
    }
    Basis b(source, a.taskId);
    CurveExport out;
    out.dates = b.row;
    for (const auto &s : b.spans)
        if (s.resourceId == a.resourceId)
            out.spans.push_back(s);
    const auto total = workFor(out.spans, a.resourceId);
    if (nativeCurveCurrent(a, b.duration, total) && !a.nativeCurve->weights.empty()) {
        out.definition = *a.nativeCurve;
        out.exact = true;
        return out;
    }
    auto &c = out.definition;
    c.source = "native-21";
    c.externalId = "native";
    c.name = "PPM current planned work";
    c.durationMinutes = b.duration;
    c.workUnitMinutes = total;
    c.baseUnits = a.units;
    c.projectedPeriods = a.periods;
    c.weights = {0};
    WorkCalendar clock(b.p), cal(b.p, b.p.tasks[b.task]);
    const auto origin = cal.offset(b.row.start);
    std::vector<std::pair<std::int64_t, std::size_t>> remainder;
    int allocated = 0;
    for (int i = 0; i < 20; ++i) {
        std::int64_t part = 0;
        for (const auto &s : out.spans) {
            const auto at = cal.offset(clock.boundary(s.start, false)) - origin;
            part += std::max(std::int64_t{0}, std::min(b.duration * (i + 1) / 20, at + s.finish - s.start) -
                                                  std::max(b.duration * i / 20, at)) *
                    s.units;
        }
        const auto [whole, residual] =
            total ? multiplyDivide(part, 100000000, total) : std::pair<std::int64_t, std::int64_t>{5000000, 0};
        const auto weight = int(whole);
        c.weights.push_back(weight);
        allocated += weight;
        remainder.emplace_back(residual, std::size_t(i + 1));
    }
    std::stable_sort(remainder.begin(), remainder.end(),
                     [](const auto &x, const auto &y) { return x.first > y.first; });
    for (std::size_t i = 0; allocated < 100000000; ++i, ++allocated)
        ++c.weights[remainder[i].second];
    return out;
}
std::vector<CurveTimephase> curveTimephases(const Project &p, const Assignment &a, const CurveExport &curve) {
    auto task = *std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &t) { return t.id == a.taskId; });
    task.taskType = TaskType::FixedDuration;
    task.workDriven = false;
    task.workUnitMinutes.reset();
    ExecutionInputs inputs(p);
    inputs.assignments[a.taskId] = {{a.taskId, a.resourceId, 1}};
    const auto available = TaskExecution(p, task, &inputs).spans(curve.dates);
    WorkCalendar clock(p);
    std::vector<CurveTimephase> out;
    for (const auto &s : curve.spans) {
        calculationCheckpoint();
        const auto start = clock.boundary(s.start, false), finish = clock.boundary(s.finish, true);
        const auto work = (s.finish - s.start) * s.units;
        bool empty = true;
        if (!out.empty() && out.back().units == s.units) {
            const auto from = clock.offset(out.back().finish);
            auto at = std::lower_bound(available.begin(), available.end(), from,
                                       [](const auto &v, auto x) { return v.finish <= x; });
            for (; at != available.end() && at->start < s.start; ++at)
                if (std::max(from, at->start) < std::min(s.start, at->finish)) {
                    empty = false;
                    break;
                }
            if (empty) {
                out.back().finish = finish;
                out.back().work += work;
                continue;
            }
        }
        out.push_back({start, finish, work, s.units});
    }
    if (out.size() > 1024)
        invalid("XML 시간대별 계획 공수는 배정당1,024구간까지입니다.");
    if (out.empty())
        out.push_back({curve.dates.start, curve.dates.finish, 0, 0});
    return out;
}
} // namespace jobs
