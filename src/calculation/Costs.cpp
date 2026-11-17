#include "domain/ResourcePlan.h"
#include "domain/ExternalEvm.h"
#include "domain/Activities.h"
#include "domain/Diagnostics.h"
#include "calculation/Costs.h"
#include "calculation/PeriodEvm.h"
#include "calculation/WorkCalendar.h"
#include "calculation/Effort.h"
#include "calculation/Tracking.h"
#include "domain/Wbs.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <stdexcept>
#include <limits>
#include <memory>
#include <chrono>
#include <cstdio>
namespace jobs {
namespace {
using Minute = std::int64_t;
std::string dayEnd(const std::string &date) {
    // No supported timestamp follows the final minute of year 9999.
    if (date == "9999-12-31")
        return date + " 23:59";
    using namespace std::chrono;
    const auto next =
        year_month_day{sys_days{year{std::stoi(date.substr(0, 4))} / unsigned(std::stoi(date.substr(5, 2))) /
                                unsigned(std::stoi(date.substr(8, 2)))} +
                       days{1}};
    char out[32];
    std::snprintf(out, sizeof(out), "%04d-%02u-%02u 00:00", int(next.year()), unsigned(next.month()),
                  unsigned(next.day()));
    return out;
}
Money add(Money a, Money b) {
    if (!a || !b || *a > maxMoney - *b)
        return {};
    return *a + *b;
}
Money subtract(Money a, Money b) {
    if (!a || !b)
        return {};
    return *a - *b;
}
// Exact nonnegative integer half-up rounding, without nonportable 128-bit arithmetic.
Money fraction(std::int64_t a, std::int64_t b, std::int64_t denominator) {
    const auto q = a / denominator, r = a % denominator;
    if (b && (q > maxMoney / b || r > (std::numeric_limits<std::int64_t>::max() - denominator / 2) / b))
        return {};
    return add(q * b, (r * b + denominator / 2) / denominator);
}
Money estimate(long double n) {
    if (!std::isfinite(n) || n < 0 || n > maxMoney)
        return {};
    return std::int64_t(std::floor(n + 0.5L));
}
void derive(Evm &e, const std::string &formula) {
    e.cv = subtract(e.ev, e.ac);
    e.sv = subtract(e.ev, e.pv);
    if (e.ev && e.ac && *e.ac > 0)
        e.cpi = double(*e.ev) / double(*e.ac);
    if (e.ev && e.pv && *e.pv > 0)
        e.spi = double(*e.ev) / double(*e.pv);
    if (!e.bac || !e.ev || !e.ac || !e.estimateSupported || *e.ev > *e.bac)
        return;
    auto remaining = *e.bac - *e.ev;
    if (formula == "budget-remaining-v1")
        e.etc = remaining;
    else if (e.cpi && *e.cpi > 0 && (formula != "cpi-spi-v1" || (e.spi && *e.spi > 0))) {
        auto value = static_cast<long double>(remaining) * *e.ac / *e.ev;
        if (formula == "cpi-spi-v1")
            value = value * *e.pv / *e.ev;
        e.etc = estimate(value);
    }
    e.eac = add(e.ac, e.etc);
}
struct Segment {
    Minute start, finish;
    int units;
    Money rate;
    Minute dayWorkBefore = 0;
    std::string resourceId;
};
struct Bucket {
    Minute day;
    std::vector<Segment> spans;
    Money before = 0, total = 0;
};
struct Budget {
    std::string startStamp, finishStamp, accrual = "uniform", credit = "physical";
    Minute start = 0, finish = 0;
    Money fixed;
    std::vector<PlannedCharge> charges;
    Money extraAt(const std::string &stamp, const WorkCalendar &clock, bool inclusive) const {
        Money total = 0;
        for (const auto &c : charges) {
            Money value = 0;
            if (c.accrual == "uniform" && finish > start) {
                const auto n = elapsed(stamp, clock);
                if (n > 0)
                    value = c.amount ? fraction(*c.amount, n, elapsed(finishStamp, clock)) : Money{};
            } else {
                const auto &event = c.accrual == "start" ? startStamp : finishStamp;
                if (inclusive ? stamp >= event : stamp > event)
                    value = c.amount;
            }
            total = add(total, value);
        }
        return total;
    }
    Money extraOn(const std::string &date, const WorkCalendar &clock) const {
        Money total = 0;
        for (const auto &c : charges) {
            Money value = 0;
            if (c.accrual == "uniform" && finish > start) {
                const auto a = elapsed(date + " 00:00", clock), b = elapsed(dayEnd(date), clock),
                           n = elapsed(finishStamp, clock);
                if (b > a && n > 0)
                    value = c.amount ? subtract(fraction(*c.amount, b, n), fraction(*c.amount, a, n)) : Money{};
            } else if (date == (c.accrual == "start" ? startStamp : finishStamp).substr(0, 10))
                value = c.amount;
            total = add(total, value);
        }
        return total;
    }
    std::vector<Bucket> days;
    std::vector<Diagnostic> issues;
    bool valid = false;
    std::shared_ptr<WorkCalendar> work;
    Minute elapsed(const std::string &stamp, const WorkCalendar &clock) const {
        if (stamp <= startStamp)
            return 0;
        const auto &cal = work ? *work : clock;
        return cal.offset(std::min(stamp, finishStamp)) - cal.offset(startStamp);
    }
    Money on(const std::string &date, const WorkCalendar &calendar) const {
        if (!valid)
            return {};
        if (date < startStamp.substr(0, 10) || date > finishStamp.substr(0, 10))
            return 0;
        Money resource = 0;
        const auto offset = calendar.offset(date + " 09:00");
        auto it = std::lower_bound(days.begin(), days.end(), offset / calendar.dayMinutes(),
                                   [](const auto &d, Minute day) { return d.day < day; });
        if (it != days.end() && it->day == offset / calendar.dayMinutes() &&
            calendar.boundary(it->day * calendar.dayMinutes(), false).substr(0, 10) == date)
            resource = it->total;
        Money f = 0;
        if (accrual == "uniform" && finish > start) {
            const auto before = elapsed(date + " 00:00", calendar);
            const auto after = elapsed(dayEnd(date), calendar);
            const auto total = elapsed(finishStamp, calendar);
            if (after > before && total > 0)
                f = fixed ? subtract(fraction(*fixed, after, total), fraction(*fixed, before, total)) : Money{};
        } else if (date == (accrual == "start" ? startStamp : finishStamp).substr(0, 10))
            f = fixed;
        return add(add(resource, f), extraOn(date, calendar));
    }
    Money at(const std::string &stamp, const WorkCalendar &calendar, bool inclusive = true) const {
        if (!valid)
            return {};
        if (stamp < startStamp)
            return 0;
        auto offset = stamp >= finishStamp ? finish : calendar.offset(stamp);
        auto elapsedWork = elapsed(stamp, calendar);
        Money resource = 0;
        if (!days.empty() && elapsedWork > 0) {
            auto it = std::lower_bound(days.begin(), days.end(), offset / calendar.dayMinutes(),
                                       [](const auto &d, Minute day) { return d.day < day; });
            if (it == days.end())
                resource = add(days.back().before, days.back().total);
            else {
                resource = it->before;
                for (const auto &s : it->spans) {
                    auto n = std::clamp(offset - s.start, Minute{0}, s.finish - s.start);
                    if (n > 0)
                        resource =
                            add(resource, s.rate ? subtract(fraction(*s.rate, s.dayWorkBefore + n * s.units, 6000),
                                                            fraction(*s.rate, s.dayWorkBefore, 6000))
                                                 : Money{});
                }
            }
        }
        Money f = 0;
        const auto &event = accrual == "start" ? startStamp : finishStamp;
        const bool earned = inclusive ? stamp >= event : stamp > event;
        if (accrual == "uniform" && finish > start) {
            if (elapsedWork > 0)
                f = fixed ? fraction(*fixed, elapsedWork, elapsed(finishStamp, calendar)) : Money{};
        } else if (earned)
            f = fixed;
        return add(add(resource, f), extraAt(stamp, calendar, inclusive));
    }
};
const TaskCost *config(const PlanInput &p, const std::string &id) {
    for (const auto &c : p.taskCosts)
        if (c.taskId == id)
            return &c;
    return nullptr;
}
using CalendarCache = std::map<std::string, std::shared_ptr<WorkCalendar>>;
Budget budget(const PlanInput &p, const Task &task, const TaskSchedule *row, const WorkCalendar &calendar,
              CalendarCache &cache, const Project *context = nullptr) {
    Budget b;
    b.charges = plannedCharges(p, task.id);
    if (const auto *c = config(p, task.id)) {
        b.fixed = c->fixedMinor;
        b.accrual = c->accrual;
        b.credit = c->credit;
    }
    if (!b.fixed)
        b.issues.push_back({"COST_FIXED_UNKNOWN", {}, {task.id}});
    if (!row) {
        b.issues.push_back({"COST_SCHEDULE_INVALID", {}, {task.id}});
        return b;
    }
    b.valid = true;
    b.start = row->earlyStartMinutes;
    b.finish = row->earlyFinishMinutes;
    b.startStamp = row->start;
    b.finishStamp = row->finish;
    if (isPdm(p)) {
        auto &shared = cache[isEffort(p) ? task.id : task.calendarId];
        if (!shared)
            shared = std::make_shared<WorkCalendar>(p, task);
        b.work = shared;
    }
    std::vector<std::pair<Minute, Minute>> workSpans;
    if (b.work) {
        for (const auto &[begin, end] : b.work->segments(row->start, row->finish))
            workSpans.emplace_back(calendar.offset(begin), calendar.offset(end));
    } else
        workSpans.emplace_back(b.start, b.finish);
    std::map<Minute, Bucket> days;
    const auto effortSpans = isEffort(p)
                                 ? (context ? executionSpans(*context, task, *row) : executionSpans(p, task, *row))
                                 : std::vector<WorkSpan>{};
    for (const auto &a : p.assignments)
        if (a.taskId == task.id) {
            const auto r = std::find_if(p.resources.begin(), p.resources.end(),
                                        [&](const auto &x) { return x.id == a.resourceId; });
            bool warned = false;
            std::map<Minute, Minute> workInDay;
            std::vector<WorkSpan> assigned;
            if (isEffort(p)) {
                for (const auto &s : effortSpans)
                    if (s.resourceId == a.resourceId)
                        assigned.push_back(s);
            } else
                for (const auto &[begin, finish] : workSpans)
                    assigned.push_back({a.resourceId, begin, finish, a.units});
            for (const auto &s : assigned)
                for (auto at = s.start; at < s.finish;) {
                    const auto finish = s.finish;
                    const auto end = std::min(finish, (at / calendar.dayMinutes() + 1) * calendar.dayMinutes());
                    auto date = calendar.boundary(at, false).substr(0, 10);
                    const Resource::Rate *rate = nullptr;
                    for (const auto &v : r->rates)
                        if (v.effectiveDate <= date && (!rate || v.effectiveDate > rate->effectiveDate))
                            rate = &v;
                    Money amount = rate ? rate->hourlyMinor : Money{};
                    if (!amount && !warned) {
                        Diagnostic issue{"COST_RATE_UNKNOWN", {r->name, r->id, date}, {task.id}};
                        issue.resourceIds = {r->id};
                        b.issues.push_back(std::move(issue));
                        warned = true;
                    }
                    auto &d = days[at / calendar.dayMinutes()];
                    d.day = at / calendar.dayMinutes();
                    auto &before = workInDay[d.day];
                    d.spans.push_back({at, end, s.units, amount, before, a.resourceId});
                    d.total = add(d.total, amount ? subtract(fraction(*amount, before + (end - at) * s.units, 6000),
                                                             fraction(*amount, before, 6000))
                                                  : Money{});
                    before += (end - at) * s.units;
                    at = end;
                }
        }
    Money prefix = 0;
    for (auto &[day, d] : days) {
        (void)day;
        d.before = prefix;
        prefix = add(prefix, d.total);
        b.days.push_back(std::move(d));
    }
    if (!b.at(b.finishStamp, calendar) && b.issues.empty())
        b.issues.push_back({"COST_MONEY_LIMIT", {}, {task.id}});
    return b;
}
const ProgressEntry *progressAt(const Project &p, const std::string &id, const std::string &stamp) {
    const ProgressEntry *result = nullptr;
    for (const auto &e : p.progress)
        if (e.taskId == id && e.statusAt <= stamp && (!result || e.statusAt >= result->statusAt))
            result = &e;
    return result;
}
Money actual(const Project &p, const std::string &id, const std::string &stamp) {
    auto *e = costAsOf(p, id, stamp);
    return e ? e->actualMinor : Money{};
}
Money earned(const Project &p, const std::string &id, const Budget &b, Money bac, const std::string &stamp) {
    if (!bac)
        return {};
    if (*bac == 0)
        return 0;
    if (b.credit == "zero-hundred") {
        const auto *e = progressAt(p, id, stamp);
        return e ? Money{e->actualFinish ? *bac : 0} : Money{};
    }
    const auto definition = std::find_if(p.activities.begin(), p.activities.end(), [&](const auto &a) {
        return a.kind == "steps" && a.taskId == id && a.physicalFromSteps;
    });
    if (definition != p.activities.end()) {
        const auto physical = stepsPhysicalAsOf(p, id, stamp);
        return physical ? fraction(*bac, *physical, 10000) : Money{};
    }
    const auto *e = costAsOf(p, id, stamp);
    return e && e->physicalBasisPoints ? fraction(*bac, *e->physicalBasisPoints, 10000) : Money{};
}
Evm combine(const std::vector<CostTaskRow> &rows, const std::set<std::string> &ids, const std::string &formula,
            bool hasStatus) {
    Evm e;
    e.bac = e.pv = e.ev = e.ac = 0;
    for (const auto &row : rows)
        if (row.baselineMember && ids.contains(row.taskId)) {
            e.bac = add(e.bac, row.evm.bac);
            e.pv = add(e.pv, row.evm.pv);
            e.ev = add(e.ev, row.evm.ev);
            e.ac = add(e.ac, row.evm.ac);
            e.estimateSupported = e.estimateSupported && row.evm.estimateSupported;
        }
    if (!hasStatus) {
        e.pv.reset();
        e.ev.reset();
        e.ac.reset();
    }
    derive(e, formula);
    return e;
}
} // namespace
static std::vector<PlannedCostDay> plannedCostDaysImpl(const PlanInput &p, const ScheduleResult &schedule,
                                                       const std::string &from, const std::string &to,
                                                       bool includeFixed, const Project *context) {
    EvaluationScope evaluation;
    if (!validDate(from) || !validDate(to) || from > to || schedule.isForecast ||
        schedule.status != CalculationStatus::Valid)
        throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
    WorkCalendar calendar(p);
    CalendarCache calendars;
    std::unordered_map<std::string, const TaskSchedule *> rows;
    for (const auto &row : schedule.tasks)
        rows.emplace(row.taskId, &row);
    std::vector<PlannedCostDay> out;
    auto append = [&](const std::string &task, const std::string &resource, const std::string &date, Money amount) {
        calculationCheckpoint();
        if (out.size() >= 1000000)
            throw AppError(ErrorCode::REPORT_CELL_LIMIT);
        out.push_back({task, resource, date, amount});
    };
    for (const auto &task : p.tasks) {
        calculationCheckpoint();
        if (!rows.contains(task.id))
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        const auto &row = *rows.at(task.id);
        if (row.finish.substr(0, 10) < from || row.start.substr(0, 10) > to)
            continue;
        const auto b = budget(p, task, &row, calendar, calendars, context);
        for (const auto &day : b.days) {
            const auto date = calendar.boundary(day.day * calendar.dayMinutes(), false).substr(0, 10);
            if (date < from || date > to)
                continue;
            std::map<std::string, Money> amounts;
            for (const auto &s : day.spans) {
                if (!amounts.contains(s.resourceId))
                    amounts[s.resourceId] = 0;
                amounts[s.resourceId] =
                    add(amounts[s.resourceId],
                        s.rate ? subtract(fraction(*s.rate, s.dayWorkBefore + (s.finish - s.start) * s.units, 6000),
                                          fraction(*s.rate, s.dayWorkBefore, 6000))
                               : Money{});
            }
            for (const auto &[resource, amount] : amounts)
                append(task.id, resource, date, amount);
        }
        if (!includeFixed)
            continue;
        auto date = std::max(from, row.start.substr(0, 10));
        const auto last = std::min(to, row.finish.substr(0, 10));
        while (date <= last) {
            Money fixed = 0;
            if (b.accrual == "uniform" && b.finish > b.start) {
                const auto before = b.elapsed(date + " 00:00", calendar), after = b.elapsed(dayEnd(date), calendar),
                           total = b.elapsed(b.finishStamp, calendar);
                if (after > before && total > 0)
                    fixed = b.fixed ? subtract(fraction(*b.fixed, after, total), fraction(*b.fixed, before, total))
                                    : Money{};
            } else if (date == (b.accrual == "start" ? b.startStamp : b.finishStamp).substr(0, 10))
                fixed = b.fixed;
            append(task.id, {}, date, add(fixed, b.extraOn(date, calendar)));
            if (date == last)
                break;
            date = dayEnd(date).substr(0, 10);
        }
    }
    return out;
}
std::vector<PlannedCostDay> plannedCostDays(const PlanInput &p, const ScheduleResult &s, const std::string &from,
                                            const std::string &to, bool fixed) {
    return plannedCostDaysImpl(p, s, from, to, fixed, nullptr);
}
std::vector<PlannedCostDay> plannedCostDays(const Project &p, const ScheduleResult &s, const std::string &from,
                                            const std::string &to, bool fixed) {
    return plannedCostDaysImpl(p, s, from, to, fixed, &p);
}
CostAnalysis analyzeCosts(const Project &p, const std::string &baselineId, const std::string &formula,
                          const std::optional<std::vector<std::string>> &taskScope) {
    validate(p);
    if (formula != "budget-remaining-v1" && formula != "cpi-v1" && formula != "cpi-spi-v1")
        throw AppError(ErrorCode::EVM_FORMULA_UNSUPPORTED, {});
    CostAnalysis out;
    out.documentId = p.documentId;
    out.revision = p.revision;
    out.statusAt = p.statusAt;
    out.currency = p.currency;
    out.baselineId = baselineId;
    out.formula = formula;
    const Baseline *baseline = nullptr;
    for (const auto &b : p.baselines)
        if (b.id == baselineId)
            baseline = &b;
    if (!baselineId.empty() && !baseline)
        throw AppError(ErrorCode::BASELINE_NOT_FOUND, {});
    if (baseline)
        verifyBaseline(*baseline);
    std::set<std::string> selected;
    if (taskScope) {
        std::set<std::string> known;
        for (const auto &t : p.tasks)
            known.insert(t.id);
        if (baseline)
            for (const auto &t : baseline->input.tasks)
                known.insert(t.id);
        for (const auto &id : *taskScope) {
            if (!known.contains(id))
                throw AppError(ErrorCode::COST_TASK_NOT_FOUND, {});
            selected.insert(id);
        }
    }
    const bool compatible = baseline && baseline->input.currency == p.currency;
    const ExternalEvmIndex external(p, baselineId);
    if (baseline && !compatible)
        out.diagnostics.push_back({"CURRENCY_MISMATCH", {}, {}, "CURRENCY_MISMATCH"});
    if (!baseline)
        out.diagnostics.push_back({"NO_COST_BASELINE", {}, {}, "NO_COST_BASELINE"});
    if (!p.statusAt)
        out.diagnostics.push_back({"NO_COST_STATUS", {}, {}, "NO_COST_STATUS"});
    auto plan = calculatePlan(p);
    WorkCalendar calendar(p);
    std::optional<WorkCalendar> baselineCalendar;
    if (baseline)
        baselineCalendar.emplace(baseline->input);
    std::unordered_map<std::string, const TaskSchedule *> dates, frozenDates;
    for (const auto &row : plan.tasks)
        if (plan.status == CalculationStatus::Valid)
            dates.emplace(row.taskId, &row);
    if (baseline)
        for (const auto &row : baseline->schedule)
            frozenDates.emplace(row.taskId, &row);
    std::map<std::string, Budget> current, frozen;
    CalendarCache currentCalendars, frozenCalendars;
    for (const auto &t : p.tasks)
        current.emplace(t.id,
                        budget(p, t, dates.contains(t.id) ? dates.at(t.id) : nullptr, calendar, currentCalendars));
    if (compatible)
        for (const auto &t : baseline->input.tasks)
            frozen.emplace(t.id, budget(baseline->input, t, frozenDates.at(t.id), *baselineCalendar, frozenCalendars));
    if (taskScope) {
        std::erase_if(current, [&](const auto &item) { return !selected.contains(item.first); });
        std::erase_if(frozen, [&](const auto &item) { return !selected.contains(item.first); });
    }
    std::vector<Task> all;
    for (const auto &t : p.tasks)
        if (current.contains(t.id))
            all.push_back(t);
    if (baseline)
        for (const auto &t : baseline->input.tasks)
            if ((!taskScope || selected.contains(t.id)) && !current.contains(t.id))
                all.push_back(t);
    out.plan = 0;
    out.actual = p.statusAt ? Money{0} : Money{};
    std::set<std::string> cohort;
    for (const auto &t : all) {
        CostTaskRow row;
        row.taskId = t.id;
        row.name = t.name;
        row.baselineMember = baseline && frozenDates.contains(t.id);
        if (current.contains(t.id)) {
            const auto &b = current.at(t.id);
            row.plan = b.at(b.finishStamp, calendar);
            row.issues = b.issues;
            out.plan = add(out.plan, row.plan);
            if (p.statusAt)
                out.actual = add(out.actual, actual(p, t.id, *p.statusAt));
        }
        if (p.statusAt) {
            row.evm.ac = actual(p, t.id, *p.statusAt);
            if (const auto *e = costAsOf(p, t.id, *p.statusAt))
                row.physicalBasisPoints = e->physicalBasisPoints;
            if (std::any_of(p.activities.begin(), p.activities.end(), [&](const auto &a) {
                    return a.kind == "steps" && a.taskId == t.id && a.physicalFromSteps;
                }))
                row.physicalBasisPoints = stepsPhysicalAsOf(p, t.id, *p.statusAt);
        }
        if (row.evm.ac && current.contains(t.id))
            ++out.actualCoverage;
        if (row.baselineMember) {
            cohort.insert(t.id);
            ++out.baselineTasks;
        }
        if (frozen.contains(t.id)) {
            const auto &b = frozen.at(t.id);
            row.credit = b.credit;
            row.evm.bac = b.at(b.finishStamp, *baselineCalendar);
            for (const auto &issue : b.issues) {
                Diagnostic frozen{"COST_BASELINE_INPUT", {}, {t.id}};
                frozen.causes = {issue};
                row.issues.push_back(std::move(frozen));
            }
            if (p.statusAt) {
                row.evm.pv = b.at(*p.statusAt, *baselineCalendar);
                row.evm.ev = earned(p, t.id, b, row.evm.bac, *p.statusAt);
                const auto adopted = external.at(t.id, *p.statusAt);
                if (adopted.usePv)
                    row.evm.pv = adopted.pv;
                if (adopted.useEv) {
                    row.evm.ev = adopted.ev;
                    row.credit = "external-ev";
                }
                if ((adopted.useEv || adopted.usePv) && row.evm.bac &&
                    ((row.evm.ev && *row.evm.ev > *row.evm.bac) || (row.evm.pv && *row.evm.pv > *row.evm.bac))) {
                    row.evm.estimateSupported = false;
                    row.issues.push_back({"EXTERNAL_EVM_BAC_MISMATCH", {}, {t.id}});
                }
            }
        }
        if (row.evm.ev && row.baselineMember)
            ++out.evCoverage;
        if (p.statusAt && !row.evm.ac)
            row.issues.push_back({"COST_ACTUAL_UNKNOWN", {}, {t.id}});
        if (p.statusAt && row.baselineMember && !row.evm.ev)
            row.issues.push_back({"COST_EV_UNKNOWN", {}, {t.id}});
        derive(row.evm, formula);
        out.tasks.push_back(std::move(row));
    }
    if (compatible)
        out.evm = combine(out.tasks, cohort, formula, p.statusAt.has_value());
    for (const auto &n : p.wbsNodes) {
        CostGroup group;
        group.id = n.id;
        group.name = n.name;
        group.plan = 0;
        group.actual = p.statusAt ? Money{0} : Money{};
        for (auto index : wbsTaskRows(p, n.id)) {
            const auto &id = p.tasks[index].id;
            if (!current.contains(id))
                continue;
            group.plan = add(group.plan, current.at(id).at(current.at(id).finishStamp, calendar));
            if (p.statusAt)
                group.actual = add(group.actual, actual(p, id, *p.statusAt));
        }
        out.currentWbs.push_back(std::move(group));
    }
    if (compatible)
        for (const auto &n : baseline->input.wbsNodes) {
            std::set<std::string> ids;
            for (auto index : wbsTaskRows(baseline->input, n.id))
                ids.insert(baseline->input.tasks[index].id);
            CostGroup group;
            group.id = n.id;
            group.name = n.name;
            group.evm = combine(out.tasks, ids, formula, p.statusAt.has_value());
            out.baselineWbs.push_back(std::move(group));
        }
    std::set<std::string> days;
    auto addDays = [&](const auto &budgets, const WorkCalendar &cal) {
        for (const auto &[id, b] : budgets) {
            (void)id;
            if (!b.valid)
                continue;
            days.insert(b.startStamp.substr(0, 10));
            days.insert(b.finishStamp.substr(0, 10));
            for (auto m = b.start / cal.dayMinutes() * cal.dayMinutes(); m < b.finish; m += cal.dayMinutes())
                days.insert(cal.boundary(m, false).substr(0, 10));
        }
    };
    addDays(current, calendar);
    if (compatible)
        addDays(frozen, *baselineCalendar);
    if (compatible && !p.externalEvmAdoptions.empty()) {
        std::optional<std::string> from, to;
        std::map<std::string, const ExternalEvmAdoption *> latest;
        for (const auto &a : p.externalEvmAdoptions)
            if (a.baselineId == baselineId)
                for (const auto &id : a.taskIds)
                    latest[id] = &a;
        for (const auto &a : p.externalEvmAdoptions)
            if (a.baselineId == baselineId && !a.withdrawn) {
                const auto *source = externalEvmSource(p, a.financialRevisionId);
                for (const auto &r : a.rows) {
                    const auto &period = source->periods[r.periodIndex];
                    if ((taskScope && !selected.contains(period.taskId)) || latest.at(period.taskId) != &a)
                        continue;
                    auto begin = period.start.substr(0, 10), end = period.finish.substr(0, 10);
                    from = from ? std::min(*from, begin) : begin;
                    to = to ? std::max(*to, end) : end;
                }
            }
        if (from && to) {
            using namespace std::chrono;
            auto parse = [](const std::string &s) {
                return sys_days{year{std::stoi(s.substr(0, 4))} / unsigned(std::stoi(s.substr(5, 2))) /
                                unsigned(std::stoi(s.substr(8, 2)))};
            };
            for (auto date = parse(*from); date <= parse(*to); date += std::chrono::days{1}) {
                calculationCheckpoint();
                if (days.size() >= 20000)
                    throw AppError(ErrorCode::REPORT_CELL_LIMIT);
                const year_month_day d{date};
                char value[32];
                std::snprintf(value, sizeof(value), "%04d-%02u-%02u", int(d.year()), unsigned(d.month()),
                              unsigned(d.day()));
                days.insert(value);
            }
        }
    }
    if (p.statusAt) {
        days.insert(p.statusAt->substr(0, 10));
        for (const auto &e : p.costEntries)
            if (e.statusAt <= *p.statusAt && (!taskScope || selected.contains(e.taskId)))
                days.insert(e.statusAt.substr(0, 10));
        for (const auto &e : p.progress)
            if (e.statusAt <= *p.statusAt && (!taskScope || selected.contains(e.taskId)))
                days.insert(e.statusAt.substr(0, 10));
    }
    for (const auto &date : days) {
        const auto end = date + " 23:59";
        const auto boundary = dayEnd(date);
        CostDay d;
        d.date = date;
        d.plan = 0;
        d.budget = d.pv = compatible ? Money{0} : Money{};
        for (const auto &[id, b] : current) {
            (void)id;
            d.plan = add(d.plan, b.on(date, calendar));
        }
        for (const auto &[id, b] : frozen) {
            (void)id;
            d.budget = add(d.budget, b.on(date, *baselineCalendar));
            const auto adopted = external.at(id, boundary, date == "9999-12-31");
            d.pv = add(d.pv, adopted.usePv ? adopted.pv : b.at(boundary, *baselineCalendar, date == "9999-12-31"));
        }
        if (p.statusAt && date <= p.statusAt->substr(0, 10)) {
            d.currentActual = 0;
            const auto at = std::min(end, *p.statusAt);
            for (const auto &[id, b] : current) {
                (void)b;
                d.currentActual = add(d.currentActual, actual(p, id, at));
            }
        }
        if (compatible && p.statusAt && date <= p.statusAt->substr(0, 10)) {
            const auto at = std::min(end, *p.statusAt);
            d.ev = d.ac = 0;
            for (const auto &[id, b] : frozen) {
                const auto adopted = external.at(id, at);
                d.ev = add(d.ev,
                           adopted.useEv ? adopted.ev : earned(p, id, b, b.at(b.finishStamp, *baselineCalendar), at));
                d.ac = add(d.ac, actual(p, id, at));
            }
            if (date == p.statusAt->substr(0, 10))
                d.pv = out.evm.pv;
        }
        out.days.push_back(std::move(d));
    }
    for (const auto &row : out.tasks)
        for (const auto &issue : row.issues) {
            Diagnostic summary{"COST_INPUT_OR_LIMIT", {}, {row.taskId}};
            summary.causes = {issue};
            out.diagnostics.push_back(std::move(summary));
        }
    std::set<std::string> currentIds;
    for (const auto &t : p.tasks)
        if (current.contains(t.id))
            currentIds.insert(t.id);
    if (baseline && cohort != currentIds)
        out.diagnostics.push_back({"BASELINE_SCOPE", {}, {}, "BASELINE_SCOPE"});
    out.diagnostics.push_back({"EVM_POLICY", {}, {}, "EVM_POLICY"});
    if (std::any_of(p.externalEvmAdoptions.begin(), p.externalEvmAdoptions.end(),
                    [&](const auto &a) { return a.baselineId == baselineId; }))
        out.diagnostics.push_back({"EXTERNAL_EVM_POLICY", {}, {}});
    return out;
}
namespace {
template <typename T> using Timelines = std::unordered_map<std::string, std::vector<const T *>>;
template <typename T> Timelines<T> timelines(const std::vector<T> &entries) {
    Timelines<T> out;
    for (const auto &e : entries)
        out[e.taskId].push_back(&e);
    for (auto &[id, rows] : out) {
        (void)id;
        std::stable_sort(rows.begin(), rows.end(), [](auto a, auto b) { return a->statusAt < b->statusAt; });
    }
    return out;
}
template <typename T> const T *atBoundary(const Timelines<T> &index, const std::string &id, const EvmBoundary &at) {
    if (!at.observed || !index.contains(id))
        return nullptr;
    const auto &rows = index.at(id);
    auto end = at.inclusive ? std::upper_bound(rows.begin(), rows.end(), at.stamp,
                                               [](const auto &stamp, const auto *r) { return stamp < r->statusAt; })
                            : std::lower_bound(rows.begin(), rows.end(), at.stamp,
                                               [](const auto *r, const auto &stamp) { return r->statusAt < stamp; });
    return end == rows.begin() ? nullptr : *std::prev(end);
}
} // namespace
Evm nativeEvmIndicators(Evm e, const std::string &formula) {
    if (formula != "budget-remaining-v1" && formula != "cpi-v1" && formula != "cpi-spi-v1")
        throw AppError(ErrorCode::EVM_FORMULA_UNSUPPORTED);
    e.cv.reset();
    e.sv.reset();
    e.cpi.reset();
    e.spi.reset();
    e.eac.reset();
    e.etc.reset();
    derive(e, formula);
    return e;
}
std::vector<PeriodEvmCurve> nativeEvmCurves(const Project &p, const std::string &baselineId, const std::string &formula,
                                            const std::vector<EvmBoundary> &boundaries,
                                            const std::optional<std::vector<std::string>> &scope) {
    EvaluationScope evaluation;
    validate(p);
    if (formula != "budget-remaining-v1" && formula != "cpi-v1" && formula != "cpi-spi-v1")
        throw AppError(ErrorCode::EVM_FORMULA_UNSUPPORTED);
    const auto baseline =
        std::find_if(p.baselines.begin(), p.baselines.end(), [&](const auto &b) { return b.id == baselineId; });
    if (baseline == p.baselines.end())
        throw AppError(ErrorCode::BASELINE_NOT_FOUND);
    verifyBaseline(*baseline);
    if (baseline->input.currency != p.currency)
        throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
    if (boundaries.size() > 7320)
        throw AppError(ErrorCode::REPORT_CELL_LIMIT);
    for (const auto &at : boundaries)
        if (!validStamp(at.stamp) || (at.observed && (!p.statusAt || at.stamp > *p.statusAt)))
            throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
    std::set<std::string> selected, known;
    for (const auto &t : baseline->input.tasks)
        known.insert(t.id);
    for (const auto &t : p.tasks)
        known.insert(t.id);
    if (scope)
        for (const auto &id : *scope) {
            if (!known.contains(id))
                throw AppError(ErrorCode::COST_TASK_NOT_FOUND);
            selected.insert(id);
        }
    const auto count = std::count_if(baseline->input.tasks.begin(), baseline->input.tasks.end(),
                                     [&](const auto &t) { return !scope || selected.contains(t.id); });
    if (std::size_t(count) * boundaries.size() > 40000)
        throw AppError(ErrorCode::REPORT_CELL_LIMIT);
    auto costs = timelines(p.costEntries);
    const ExternalEvmIndex external(p, baselineId);
    auto progress = timelines(p.progress);
    auto steps = timelines(p.stepReports);
    std::set<std::string> fromSteps;
    for (const auto &a : p.activities)
        if (a.kind == "steps" && a.physicalFromSteps)
            fromSteps.insert(a.taskId);
    std::unordered_map<std::string, const TaskSchedule *> dates;
    for (const auto &row : baseline->schedule)
        dates.emplace(row.taskId, &row);
    WorkCalendar calendar(baseline->input);
    CalendarCache calendars;
    std::vector<PeriodEvmCurve> out;
    for (const auto &t : baseline->input.tasks) {
        calculationCheckpoint();
        if (scope && !selected.contains(t.id))
            continue;
        const auto b = budget(baseline->input, t, dates.at(t.id), calendar, calendars);
        const auto bac = b.at(b.finishStamp, calendar);
        PeriodEvmCurve curve{t.id, t.name, t.wbsId, b.credit, {}};
        for (const auto &at : boundaries) {
            calculationCheckpoint();
            PeriodEvmPoint point;
            auto &e = point.value;
            e.bac = bac;
            e.pv = b.at(at.stamp, calendar, at.inclusive);
            if (const auto *c = atBoundary(costs, t.id, at)) {
                e.ac = c->actualMinor;
                point.acReportId = c->id;
                if (b.credit != "zero-hundred" && !fromSteps.contains(t.id)) {
                    if (bac && c->physicalBasisPoints)
                        e.ev = fraction(*bac, *c->physicalBasisPoints, 10000);
                    point.evReportId = c->id;
                }
            }
            if (at.observed) {
                if (b.credit == "zero-hundred") {
                    if (const auto *r = atBoundary(progress, t.id, at)) {
                        e.ev = r->actualFinish ? bac : (bac ? Money{0} : Money{});
                        point.evReportId = r->id;
                    }
                } else if (fromSteps.contains(t.id)) {
                    if (const auto *r = atBoundary(steps, t.id, at)) {
                        if (bac)
                            e.ev = fraction(*bac, weightedStepProgress(r->steps), 10000);
                        point.evReportId = r->id;
                    }
                }
                if (bac && *bac == 0)
                    e.ev = 0;
            }
            const auto adopted = external.at(t.id, at.stamp, at.inclusive);
            if (adopted.usePv)
                e.pv = adopted.pv;
            if (at.observed && adopted.useEv) {
                e.ev = adopted.ev;
                point.evReportId = adopted.adoptionId;
                curve.credit = "external-ev";
            }
            if ((adopted.useEv || adopted.usePv) && bac && ((e.ev && *e.ev > *bac) || (e.pv && *e.pv > *bac)))
                e.estimateSupported = false;
            derive(e, formula);
            curve.points.push_back(std::move(point));
        }
        out.push_back(std::move(curve));
    }
    return out;
}
} // namespace jobs
