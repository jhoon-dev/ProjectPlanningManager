#include "calculation/ResourceAvailabilityQuery.h"
#include "calculation/ScheduleFormulaScope.h"
#include "calculation/XmlProgressReports.h"
#include "domain/XmlRollUp.h"
#include "calculation/FinancialPeriodReports.h"
#include "domain/ResourcePlan.h"
#include "domain/MaterialActuals.h"
#include "domain/FormulaFields.h"
#include "calculation/FormulaFieldReports.h"
#include "calculation/ActivitySummary.h"
#include "domain/Activities.h"
#include "formatting/DiagnosticText.h"
#include "formatting/PlanTokens.h"
#include "calculation/Reports.h"
#include "calculation/PeriodActualReports.h"
#include "calculation/ExternalEvmReports.h"
#include "calculation/Constraints.h"
#include "calculation/Effort.h"
#include "domain/Wbs.h"
#include "domain/Classification.h"
#include "domain/XmlRollDown.h"
#include "domain/Productivity.h"
#include <algorithm>
#include <set>
#include <map>
#include <unordered_map>
#include <stdexcept>
namespace ppm {
namespace {
ReportCell text(std::string s) {
    return {std::move(s), CellFormat::Text};
}
ReportCell number(std::int64_t n, CellFormat format = CellFormat::Integer) {
    return {n, format};
}
ReportCell optional(std::optional<std::int64_t> n, CellFormat format) {
    return n ? number(*n, format) : ReportCell{{}, format};
}
ReportCell ratio(std::optional<double> n) {
    return n ? ReportCell{*n, CellFormat::Ratio} : ReportCell{{}, CellFormat::Ratio};
}
ReportCell stamp(const std::optional<std::string> &s) {
    return s ? text(*s) : ReportCell{};
}
std::string state(CalculationStatus s) {
    return s == CalculationStatus::Cancelled    ? "취소"
           : s == CalculationStatus::Valid      ? "유효"
           : s == CalculationStatus::Incomplete ? "입력 불완전"
           : s == CalculationStatus::Stale      ? "오래된 결과"
                                                : "충돌";
}
std::string path(const PlanInput &p, std::string id) {
    std::vector<std::string> names;
    while (!id.empty()) {
        auto it = std::find_if(p.wbsNodes.begin(), p.wbsNodes.end(), [&](const auto &n) { return n.id == id; });
        if (it == p.wbsNodes.end())
            break;
        names.push_back(it->name);
        id = it->parentId;
    }
    std::string result;
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        if (!result.empty())
            result += " / ";
        result += *it;
    }
    return result.empty() ? "프로젝트 전체" : result;
}
bool inRange(const ReportOptions &o, const std::string &d) {
    return (!o.fromDate || d >= *o.fromDate) && (!o.toDate || d <= *o.toDate);
}
} // namespace
bool includeTable(const ReportTable &t, ReportFamily f) {
    return f == ReportFamily::All || t.family == ReportFamily::All || t.family == f;
}
ProjectReport buildReport(const Project &p, const ReportOptions &o) {
    validate(p);
    if (o.reportId.empty() || !validUtcStamp(o.generatedAt) || (o.fromDate && !validDate(*o.fromDate)) ||
        (o.toDate && !validDate(*o.toDate)) || (o.fromDate && o.toDate && *o.fromDate > *o.toDate))
        throw AppError(ErrorCode::REPORT_OPTIONS_INVALID, {});
    ProjectReport report;
    report.projectName = p.name;
    report.documentId = p.documentId;
    report.revision = p.revision;
    report.currency = p.currency;
    report.options = o;
    report.statusAt = p.statusAt;
    report.scopeName = path(p, o.wbsId);
    const auto rows = wbsTaskRows(p, o.wbsId);
    std::set<std::string> ids;
    std::vector<std::string> idList;
    for (auto row : rows) {
        ids.insert(p.tasks[row].id);
        idList.push_back(p.tasks[row].id);
    }
    const auto comparison = compareToBaseline(p, o.baselineId);
    const auto &active = comparison.forecast;
    ScheduleFormulaScope formulaSchedule(p, &active, ScheduleFormulaMode::Active, false);
    const bool materialFloat = active.calculationPolicy.ends_with("resource-material-float-v1");
    const bool repartition = materialFloat || active.calculationPolicy.ends_with("resource-repartition-float-v3");
    report.scheduleStatus = active.status;
    const auto costs = analyzeCosts(p, o.baselineId, o.formula, o.wbsId.empty() ? std::nullopt : std::optional{idList});
    const auto load = resourceLoad(p, active);
    auto scoped = p;
    std::erase_if(scoped.assignments, [&](const auto &a) { return !ids.contains(a.taskId); });
    const auto ownLoad = resourceLoad(scoped, active);
    std::set<std::string> resourceIds, assignedTasks;
    for (const auto &a : scoped.assignments) {
        resourceIds.insert(a.resourceId);
        assignedTasks.insert(a.taskId);
    }
    std::map<std::pair<std::string, std::string>, std::int64_t> ownWork;
    for (const auto &d : ownLoad.days)
        ownWork[{d.resourceId, d.date}] = d.demandUnitMinutes;
    std::unordered_map<std::string, const TaskSchedule *> plans, forecasts;
    for (const auto &s : comparison.plan.tasks)
        if (comparison.plan.status == CalculationStatus::Valid)
            plans[s.taskId] = &s;
    for (const auto &s : active.tasks)
        if (active.status == CalculationStatus::Valid)
            forecasts[s.taskId] = &s;
    std::unordered_map<std::string, const TaskComparison *> comparisons;
    for (const auto &t : comparison.tasks)
        comparisons[t.taskId] = &t;
    const auto rollup = summarizeWbs(p, &active, o.wbsId);
    std::int64_t complete = 0, reported = 0, critical = 0, forecastCritical = 0, delayed = 0, comparable = 0,
                 overMinutes = 0, unknownDays = 0, overloadDays = 0;
    for (auto row : rows) {
        const auto &id = p.tasks[row].id;
        const auto *e = progressAsOf(p, id);
        if (e) {
            ++reported;
            if (e->actualFinish)
                ++complete;
        }
        if (plans.contains(id) && plans.at(id)->critical)
            ++critical;
        if (forecasts.contains(id) && hasScheduleFloat(active, *forecasts.at(id)) && forecasts.at(id)->critical)
            ++forecastCritical;
        const auto &c = *comparisons.at(id);
        if (c.forecastFinishVariance) {
            ++comparable;
            if (*c.forecastFinishVariance > 0)
                ++delayed;
        }
    }
    for (const auto &d : load.days)
        if (resourceIds.contains(d.resourceId) && inRange(o, d.date)) {
            overMinutes += d.overloadMinutes;
            if (!d.capacity)
                ++unknownDays;
            if (d.overloadMinutes)
                ++overloadDays;
        }
    auto kpi = [&](const std::string &id, const std::string &title, ReportCell value, const std::string &unit,
                   const std::string &note) { report.kpis.push_back({id, title, unit, note, std::move(value)}); };
    kpi("tasks", "선택 작업", number(std::int64_t(rows.size())), "개", "현재 WBS의 고유 작업 수");
    kpi("completed", "실제 완료", p.statusAt ? number(complete) : ReportCell{}, "개",
        "상태일 기준 완료 보고. 기간/물리 진척 평균이 아닙니다.");
    kpi("finish", p.statusAt ? "잔여 예측 완료" : "계획 완료", stamp(rollup.finish), "",
        "선택 작업 범위. 논리 일정은 자원 타당성을 보장하지 않습니다.");
    kpi("critical", "계획 임계 작업",
        comparison.plan.status == CalculationStatus::Valid &&
                (!comparison.plan.resourceLeveled || comparison.plan.levelingFloatCalculated)
            ? number(critical)
            : ReportCell{},
        "개", "현재 계획 기준. 평준화 여유의 구간 유지/재생성 기준은 출처와 진단을 따릅니다.");
    if (p.statusAt)
        kpi("forecastCritical", "잔여 예측 임계 작업",
            active.status == CalculationStatus::Valid &&
                    (active.forecastFloatCalculated || active.levelingFloatCalculated)
                ? number(forecastCritical)
                : ReportCell{},
            "개", "현재 예측 완료일 기준. 완료 실적 제외 · 평준화 여유의 기준은 출처 참조");
    kpi("delayed", "기준선 대비 지연", comparable ? number(delayed) : ReportCell{}, "개",
        "완료 편차를 비교할 수 있는 " + std::to_string(comparable) + "개 작업 중 지연");
    kpi("overload", "과부하 자원시간",
        active.status == CalculationStatus::Valid && !unknownDays ? number(overMinutes, CellFormat::Hours)
                                                                  : ReportCell{},
        "h",
        "일별 명세 범위의 공유 자원 수요. 미정 가용량 " + std::to_string(unknownDays) + "건, 알려진 초과 " +
            std::to_string(overMinutes) + "분");
    kpi("plan", "현재 계획 예산", optional(costs.plan, CellFormat::Money), p.currency, "현재 선택 작업 범위");
    kpi("actual", "누적 실제원가", optional(costs.actual, CellFormat::Money), p.currency,
        "상태일 누적 보고. 단가로 소급 산정하지 않습니다.");
    kpi("bac", "기준선 예산 BAC", optional(costs.evm.bac, CellFormat::Money), p.currency,
        "선택 기준선 코호트. 현재 WBS 선택 시 작업 ID 교집합");
    kpi("ev", "획득가치 EV", optional(costs.evm.ev, CellFormat::Money), p.currency,
        "기준선에 고정된 물리 진척/완료 0/100 측정법");
    kpi("cpi", "원가 효율 CPI", ratio(costs.evm.cpi), "배", "합계 EV / 합계 AC. 0분모/누락은 미정");
    kpi("eac", "완료 예상원가 EAC", optional(costs.evm.eac, CellFormat::Money), p.currency, o.formula);
    auto table = [&](std::string id, std::string title, ReportFamily family, std::string note,
                     std::vector<ReportColumn> columns) -> ReportTable & {
        report.tables.push_back({std::move(id), std::move(title), std::move(note), family, std::move(columns), {}});
        return report.tables.back();
    };
    auto &dashboard = table(
        "dashboard", "대시보드", ReportFamily::All,
        "미정은 확인된 0과 다릅니다. 금액/완료일은 전체 계획 또는 상태일 기준이며 일별 필터는 명세에만 적용합니다.",
        {{"지표", 26}, {"값", 25}, {"단위", 10}, {"근거", 65}});
    for (const auto &k : report.kpis)
        dashboard.rows.push_back({text(k.title), k.value, text(k.unit), text(k.note)});
    dashboard.rows.push_back(
        {text("일정 계산 상태"), text(state(active.status)), text(""), text(active.calculationPolicy)});
    dashboard.rows.push_back({text("일정 실적 보고"), p.statusAt ? number(reported) : ReportCell{}, text("개"),
                              text("선택 " + std::to_string(rows.size()) + "개 작업 중 상태일 기준")});
    dashboard.rows.push_back({text("자원 배정 작업"), number(std::int64_t(assignedTasks.size())), text("개"),
                              text("미배정 작업은 수요 검증에 포함되지 않습니다.")});
    dashboard.rows.push_back({text("과부하 자원·일"),
                              active.status == CalculationStatus::Valid ? number(overloadDays) : ReportCell{},
                              text("건"), text("선택 자원의 전체 프로젝트 공유 수요. 일별 명세 기간 기준")});
    auto &schedule = table("schedule", "계획 일정", ReportFamily::Schedule,
                           "전체 실행 그래프를 계산한 후 WBS 작업을 선택합니다. 총여유는 각 작업 달력의 "
                           "근무시간(h)입니다. 미계산/충돌 날짜를 과거 캐시로 채우지 않습니다.",
                           {{"작업 ID", 28},
                            {"작업", 24},
                            {"WBS", 24},
                            {"기간(분)", 12},
                            {"계획 시작", 21},
                            {"계획 완료", 21},
                            {"여유(h)", 12},
                            {"임계", 10},
                            {"작업 달력", 30}});
    for (auto row : rows) {
        const auto &t = p.tasks[row];
        auto *s = plans.contains(t.id) ? plans.at(t.id) : nullptr;
        schedule.rows.push_back(
            {text(t.id), text(t.name), text(t.wbsId.empty() ? "미분류" : path(p, t.wbsId)),
             optional(isEffort(p) && s ? std::optional<std::int64_t>{TaskExecution(p, t).duration(*s)}
                      : usesWork(t)    ? std::nullopt
                                       : t.durationMinutes,
                      CellFormat::Integer),
             s ? text(s->start) : ReportCell{}, s ? text(s->finish) : ReportCell{},
             s && hasScheduleFloat(comparison.plan, *s) ? number(s->totalFloatMinutes, CellFormat::Hours)
                                                        : ReportCell{},
             s ? text(comparison.plan.resourceLeveled
                          ? (comparison.plan.levelingFloatCalculated ? (s->critical ? "평준화 임계" : "평준화 여유")
                                                                     : "평준화 채택 · 임계 미산정")
                      : s->critical ? "임계"
                                    : "여유")
               : ReportCell{},
             text(taskCalendarName(p, t))});
        if (comparison.plan.levelingFloatCalculated)
            schedule.rows.back().push_back(s && hasScheduleFloat(comparison.plan, *s)
                                               ? number(s->freeFloatMinutes, CellFormat::Hours)
                                               : ReportCell{});
    }
    if (comparison.plan.levelingFloatCalculated)
        schedule.columns.push_back({"자유여유(h)", 18});
    if (hasConstraints(p)) {
        auto &timing =
            table("constraints", "작업 제약과 마감", ReportFamily::Schedule,
                  "날짜 제약은 선행/달력과 함께 적용합니다. 마감일은 경고 기준이며 CPM 여유를 바꾸지 않습니다. 마감 "
                  "여유는 해당 작업 달력 근무시간입니다. 비근무 경과는 0근무분이어도 civil 시각으로 초과를 판정합니다.",
                  {{"작업 ID", 28},
                   {"작업", 24},
                   {"제약", 20},
                   {"제약 시각", 21},
                   {"마감일", 21},
                   {"활성 완료", 21},
                   {"마감 여유(h)", 16},
                   {"마감 상태", 14}});
        for (auto row : rows) {
            const auto &t = p.tasks[row];
            const auto *s = forecasts.contains(t.id) ? forecasts.at(t.id) : nullptr;
            timing.rows.push_back({text(t.id), text(t.name),
                                   text(enumToken(t.constraintType) + " · " + constraintName(t.constraintType)),
                                   stamp(t.constraintAt), stamp(t.deadlineAt), s ? text(s->finish) : ReportCell{},
                                   s ? optional(deadlineSlack(p, t, s->finish), CellFormat::Hours) : ReportCell{},
                                   text(!t.deadlineAt               ? "미설정"
                                        : !s                        ? "계산 불가"
                                        : s->finish > *t.deadlineAt ? "마감 초과"
                                                                    : "마감 이내")});
        }
    }
    if (isEffort(p)) {
        auto &effort =
            table("effort", "공수와 작업 유형", ReportFamily::Schedule,
                  "기간은 작업 달력의 활성 근무분, 공수는 자원 달력 교집합 배정량입니다. 잔여 공수는 명시 보고값이며 "
                  "실제 공수를 역산하지 않습니다. 입력 기간은 공수 기준 작업의 일정 계산에 사용하지 않습니다.",
                  {{"작업 ID", 28},
                   {"작업", 24},
                   {"유형", 16},
                   {"계산 기준", 12},
                   {"입력 공수(h)", 16},
                   {"계획 공수(h)", 16},
                   {"계산 기간(h)", 16},
                   {"명시 잔여 공수(h)", 20}});
        for (auto index : rows) {
            const auto &t = p.tasks[index];
            const auto *e = progressAsOf(p, t.id);
            std::optional<std::int64_t> work, duration;
            if (plans.contains(t.id)) {
                TaskExecution model(p, t);
                work = model.work(*plans.at(t.id));
                duration = model.duration(*plans.at(t.id));
            }
            effort.rows.push_back(
                {text(t.id), text(t.name), text(taskTypeName(t.taskType)), text(usesWork(t) ? "공수" : "기간"),
                 optional(t.workUnitMinutes, CellFormat::WorkHours), optional(work, CellFormat::WorkHours),
                 optional(duration, CellFormat::Hours),
                 optional(e ? e->remainingWorkUnitMinutes : std::optional<std::int64_t>{}, CellFormat::WorkHours)});
        }
        auto &breaks = table("taskBreaks", "분할 작업 중단", ReportFamily::Schedule,
                             "절대 시각 [중단 시작, 재개). 작업만 중단하며 다른 작업의 자원은 계속 사용할 수 있습니다. "
                             "일정 이동 후에도 중단 시각은 유지됩니다.",
                             {{"작업 ID", 28}, {"작업", 24}, {"중단 시작", 21}, {"재개 시각", 21}});
        for (auto index : rows)
            for (const auto &b : p.tasks[index].breaks)
                breaks.rows.push_back(
                    {text(p.tasks[index].id), text(p.tasks[index].name), text(b.start), text(b.finish)});
        auto &calendars = table("resourceCalendars", "자원별 근무 달력", ReportFamily::Resources,
                                "빈 자원 달력은 프로젝트 기본 달력입니다. 작업 달력과 교집합을 사용하며 일별 가용량 "
                                "예외는 이 근무시간 안의 최대 배정률입니다.",
                                {{"자원 ID", 28}, {"자원", 24}, {"달력 ID", 28}, {"달력", 36}});
        for (const auto &r : p.resources)
            if (resourceIds.contains(r.id)) {
                Task t;
                t.calendarId = r.calendarId;
                calendars.rows.push_back({text(r.id), text(r.name), text(r.calendarId), text(taskCalendarName(p, t))});
            }
        auto &spans =
            table("executionWork", "실행 배정 구간", ReportFamily::Resources,
                  "활성 일정의 잔여/계획 배정입니다. 중단·비근무 시간 제외, 마지막 1분 공수 배분 포함. 날짜 필터와 "
                  "겹치는 구간만 표시하며 공수는 잘린 구간에 한정합니다.",
                  {{"작업 ID", 28}, {"자원 ID", 28}, {"시작", 21}, {"완료", 21}, {"배정률", 12}, {"공수(h)", 14}});
        WorkCalendar clock(p);
        if (active.status == CalculationStatus::Valid)
            for (auto index : rows) {
                const auto &t = p.tasks[index];
                auto pending = p.statusAt ? pendingTask(p, t) : t;
                for (auto span : executionSpans(p, pending, *forecasts.at(t.id))) {
                    const auto begin = clock.boundary(span.start, false), end = clock.boundary(span.finish, true);
                    if (o.fromDate && end <= *o.fromDate + " 00:00")
                        continue;
                    if (o.toDate && begin > *o.toDate + " 23:59")
                        continue;
                    if (o.fromDate && begin < *o.fromDate + " 00:00")
                        span.start = clock.offset(*o.fromDate + " 00:00");
                    if (o.toDate && end > *o.toDate + " 23:59")
                        span.finish = clock.offset(*o.toDate + " 23:59") + 1;
                    if (span.finish > span.start)
                        spans.rows.push_back(
                            {text(t.id), text(span.resourceId), text(clock.boundary(span.start, false)),
                             text(clock.boundary(span.finish, true)), number(span.units, CellFormat::Units),
                             number((span.finish - span.start) * span.units, CellFormat::WorkHours)});
                }
            }
    }
    auto &forecast =
        table("forecast", "실적과 예측", ReportFamily::Schedule,
              "실제 날짜를 보존합니다. 편차는 기준선 달력, 잔여 총/자유여유는 현재 작업 달력 근무시간입니다. "
              "완료 실적의 미래 여유는 적용 대상이 아닙니다. 평준화 후 여유/임계의 구간 유지·재생성 기준을 출처 "
              "표에 정책을 "
              "기록합니다.",
              {{"작업 ID", 28},
               {"작업", 24},
               {"실제 시작", 21},
               {"실제 완료", 21},
               {"잔여(h)", 12},
               {"예측 완료", 21},
               {"완료 편차(h)", 14},
               {"기준선 시작", 21},
               {"기준선 완료", 21},
               {"잔여 재개", 21},
               {"잔여 총여유(h)", 16},
               {"잔여 자유여유(h)", 18},
               {"예측 임계/상태", 18}});
    for (auto row : rows) {
        const auto &t = p.tasks[row];
        const auto &c = *comparisons.at(t.id);
        const auto *s = active.isForecast && forecasts.contains(t.id) ? forecasts.at(t.id) : nullptr;
        const bool floatKnown = s && hasScheduleFloat(active, *s);
        forecast.rows.push_back(
            {text(t.id), text(t.name), stamp(c.actualStart), stamp(c.actualFinish),
             optional(c.remainingMinutes, CellFormat::Hours), stamp(c.forecastFinish),
             optional(c.forecastFinishVariance, CellFormat::Hours), stamp(c.baselineStart), stamp(c.baselineFinish),
             s ? stamp(s->remainingStart) : ReportCell{},
             floatKnown ? number(s->totalFloatMinutes, CellFormat::Hours) : ReportCell{},
             floatKnown ? number(s->freeFloatMinutes, CellFormat::Hours) : ReportCell{},
             s ? text(s->completedActual ? "실제 완료"
                      : floatKnown ? (active.resourceLeveled ? (s->critical ? "평준화 임계" : "평준화 여유")
                                                             : (s->critical ? "예측 임계" : "예측 여유"))
                                   : "미산정")
               : ReportCell{}});
    }
    auto &links = table("dependencies", "실행 선행관계", ReportFamily::Schedule,
                        "선택 WBS 밖 관계도 유지합니다. 지연/선행은 후행 작업 달력 근무분입니다.",
                        {{"작업 ID", 28},
                         {"작업", 30},
                         {"선행 ID", 28},
                         {"선행 작업", 30},
                         {"관계", 8},
                         {"지연/선행(m)", 16},
                         {"후행 달력", 30}});
    for (auto row : rows)
        for (const auto &d : dependencies(p.tasks[row])) {
            auto it =
                std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &t) { return t.id == d.predecessorId; });
            links.rows.push_back({text(p.tasks[row].id), text(p.tasks[row].name), text(d.predecessorId),
                                  text(it == p.tasks.end() ? "누락 참조" : it->name), text(enumToken(d.type)),
                                  number(d.lagMinutes), text(taskCalendarName(p, p.tasks[row]))});
        }
    auto &assignments =
        table("assignments", "자원 배정", ReportFamily::Resources,
              "기본 배정률은 기간별 대체 구간 밖에 적용됩니다. 기간별 배정 정의 표와 함께 확인하세요. 100%는 자원 "
              "1단위이며 단가/등급은 생산성 "
              "배수가 아닙니다.",
              {{"작업 ID", 28}, {"작업", 25}, {"자원 ID", 28}, {"자원", 25}, {"종류", 12}, {"배정률", 12}});
    std::unordered_map<std::string, const Resource *> resources;
    for (const auto &r : p.resources)
        resources[r.id] = &r;
    std::unordered_map<std::string, std::string> names;
    for (const auto &t : p.tasks)
        names[t.id] = t.name;
    for (const auto &a : scoped.assignments)
        assignments.rows.push_back(
            {text(a.taskId), text(names.at(a.taskId)), text(a.resourceId), text(resources.at(a.resourceId)->name),
             text(resources.at(a.resourceId)->type == "labor" ? "인력" : "장비"), number(a.units, CellFormat::Units)});
    if (hasAssignmentPeriods(scoped)) {
        auto &periods = table("assignmentPeriods", "기간별 계획 배정 정의", ReportFamily::Resources,
                              "작업 시작 기준 근무분의 반열린 구간 [시작, 끝). 0%는 수요 없음, 빈 구간과 기간 연장은 "
                              "기본 배정률. 작업 기간 밖 구간은 보존되지만 계산에서 제외됩니다. 잔여 예측은 원래 "
                              "기간에서 잔여 기간을 뺀 위치부터 재생합니다. 실제 실적은 추정하지 않습니다.",
                              {{"작업 ID", 28},
                               {"작업", 25},
                               {"자원 ID", 28},
                               {"자원", 25},
                               {"시작 근무분", 16},
                               {"끝 근무분", 16},
                               {"배정률", 12}});
        for (const auto &a : scoped.assignments)
            for (const auto &v : a.periods)
                periods.rows.push_back({text(a.taskId), text(names.at(a.taskId)), text(a.resourceId),
                                        text(resources.at(a.resourceId)->name), number(v.fromMinute),
                                        number(v.toMinute), number(v.units, CellFormat::Units)});
    }
    auto &dailyLoad = table("resourceLoad", "기간별 자원 수요", ReportFamily::Resources,
                            "선택 자원에 대한 전체 프로젝트 공유 수요입니다. 선택 공수와 전체 공수를 구분하고 수요가 "
                            "있는 날짜만 표시합니다. 상태일 설정 시 잔여 수요만 산정합니다.",
                            {{"자원 ID", 28},
                             {"자원", 23},
                             {"날짜", 15},
                             {"선택 공수(h)", 14},
                             {"전체 공수(h)", 14},
                             {"가용률", 12},
                             {"전체 최대배정", 16}});
    for (const auto &d : load.days)
        if (resourceIds.contains(d.resourceId) && inRange(o, d.date))
            dailyLoad.rows.push_back({text(d.resourceId), text(resources.at(d.resourceId)->name), text(d.date),
                                      number(ownWork[{d.resourceId, d.date}], CellFormat::WorkHours),
                                      number(d.demandUnitMinutes, CellFormat::WorkHours),
                                      d.capacityVaries ? text(d.capacity ? "기간 내 변동" : "변동·미정 포함")
                                      : d.capacity ? number(*d.capacity, CellFormat::Units)
                                                   : ReportCell{},
                                      number(d.peakUnits, CellFormat::Units)});
    auto &exceptions = table("resourceOverload", "자원 과부하와 미정", ReportFamily::Resources,
                             "동시간대 최대 수요로 판단합니다. 과부하 시간은 자원별 구간 합계이며 프로젝트 경과시간과 "
                             "다릅니다. 미정 가용량을 0%로 취급하지 않습니다.",
                             {{"자원 ID", 28},
                              {"자원", 23},
                              {"날짜", 15},
                              {"최대 사용률", 14},
                              {"과부하(h)", 14},
                              {"초과 공수(h)", 14},
                              {"상태", 16}});
    for (const auto &d : load.days)
        if (resourceIds.contains(d.resourceId) && inRange(o, d.date) && (!d.capacity || d.overloadMinutes))
            exceptions.rows.push_back(
                {text(d.resourceId), text(resources.at(d.resourceId)->name), text(d.date),
                 !d.capacityVaries && d.capacity && *d.capacity > 0
                     ? ReportCell{double(d.peakUnits) / *d.capacity, CellFormat::Units}
                     : ReportCell{},
                 d.capacity ? number(d.overloadMinutes, CellFormat::Hours) : ReportCell{},
                 d.capacity ? number(d.excessUnitMinutes, CellFormat::WorkHours) : ReportCell{},
                 text(d.capacity ? (*d.capacity == 0 ? "가용 0% 초과" : "과부하") : "가용량 미정")});
    auto &costRows = table(
        "costs", "작업별 원가", ReportFamily::Costs,
        "금액 단위 " + p.currency +
            ". 현재 예산/AC는 현재 작업, BAC/PV/EV는 선택 기준선 코호트입니다. 단가로 실제원가를 재평가하지 않습니다.",
        {{"작업 ID", 28}, {"작업", 25}, {"현재 예산", 17}, {"BAC", 17}, {"PV", 17}, {"EV", 17}, {"AC", 17}});
    for (const auto &r : costs.tasks)
        costRows.rows.push_back({text(r.taskId), text(r.name), optional(r.plan, CellFormat::Money),
                                 optional(r.evm.bac, CellFormat::Money), optional(r.evm.pv, CellFormat::Money),
                                 optional(r.evm.ev, CellFormat::Money), optional(r.evm.ac, CellFormat::Money)});
    auto &evmRows = table(
        "evm", "획득가치 분석", ReportFamily::Costs,
        "CV=EV-AC, SV=EV-PV는 금액 편차이며 날짜 지연이 아닙니다. CPI/SPI는 합계의 비율, EAC 가정은 " + o.formula +
            "입니다.",
        {{"작업 ID", 28}, {"작업", 25}, {"CV", 17}, {"SV", 17}, {"CPI", 12}, {"SPI", 12}, {"EAC", 17}, {"ETC", 17}});
    for (const auto &r : costs.tasks)
        evmRows.rows.push_back({text(r.taskId), text(r.name), optional(r.evm.cv, CellFormat::Money),
                                optional(r.evm.sv, CellFormat::Money), ratio(r.evm.cpi), ratio(r.evm.spi),
                                optional(r.evm.eac, CellFormat::Money), optional(r.evm.etc, CellFormat::Money)});
    auto &groups = table(
        "costGroups", "원가 집계", ReportFamily::Costs,
        "현재/기준선 WBS를 구분합니다. 상위/하위 행을 다시 더하지 마세요. 현재 선택 작업 ID만 각 구조에 집계합니다.",
        {{"범위", 32}, {"현재 예산", 17}, {"현재 실제", 17}, {"BAC", 17}, {"EV", 17}, {"AC", 17}, {"CPI", 12}});
    groups.rows.push_back({text("현재 선택 범위"), optional(costs.plan, CellFormat::Money),
                           optional(costs.actual, CellFormat::Money), text("해당없음"), text("해당없음"),
                           text("해당없음"), text("해당없음")});
    groups.rows.push_back({text("기준선 선택 코호트"), text("해당없음"), text("해당없음"),
                           optional(costs.evm.bac, CellFormat::Money), optional(costs.evm.ev, CellFormat::Money),
                           optional(costs.evm.ac, CellFormat::Money), ratio(costs.evm.cpi)});
    for (const auto &g : costs.currentWbs) {
        auto members = wbsTaskRows(p, g.id);
        if (std::none_of(members.begin(), members.end(), [&](auto row) { return ids.contains(p.tasks[row].id); }))
            continue;
        groups.rows.push_back({text("현재 WBS " + path(p, g.id)), optional(g.plan, CellFormat::Money),
                               optional(g.actual, CellFormat::Money), text("해당없음"), text("해당없음"),
                               text("해당없음"), text("해당없음")});
    }
    const Baseline *baseline = nullptr;
    for (const auto &b : p.baselines)
        if (b.id == o.baselineId)
            baseline = &b;
    for (const auto &g : costs.baselineWbs) {
        if (!baseline)
            break;
        auto members = wbsTaskRows(baseline->input, g.id);
        if (!o.wbsId.empty() && std::none_of(members.begin(), members.end(),
                                             [&](auto row) { return ids.contains(baseline->input.tasks[row].id); }))
            continue;
        groups.rows.push_back({text("기준선 WBS " + path(baseline->input, g.id)), text("해당없음"), text("해당없음"),
                               optional(g.evm.bac, CellFormat::Money), optional(g.evm.ev, CellFormat::Money),
                               optional(g.evm.ac, CellFormat::Money), ratio(g.evm.cpi)});
    }
    auto &days = table("costDays", "기간별 원가", ReportFamily::Costs,
                       "일별 계획/예산과 누적 PV/EV/AC를 구분합니다. 현재 실제와 기준선 코호트 AC는 범위가 다를 수 "
                       "있습니다. 상태일 이후 EV/AC를 예측하지 않습니다.",
                       {{"날짜", 16},
                        {"현재 계획 일별", 19},
                        {"기준선 예산 일별", 20},
                        {"PV 누적", 18},
                        {"EV 누적", 18},
                        {"기준선 AC 누적", 19},
                        {"현재 실제 누적", 19}});
    for (const auto &d : costs.days)
        if (inRange(o, d.date)) {
            days.rows.push_back({text(d.date), optional(d.plan, CellFormat::Money),
                                 optional(d.budget, CellFormat::Money), optional(d.pv, CellFormat::Money),
                                 optional(d.ev, CellFormat::Money), optional(d.ac, CellFormat::Money),
                                 optional(d.currentActual, CellFormat::Money)});
            report.costTrend.push_back(d);
        }
    auto &registry = table("resourceRegistry", "자원 등록과 가용량", ReportFamily::Resources,
                           "전체 프로젝트에서는 미배정 자원도 표시합니다. 날짜별 가용량은 해당 날짜에만 기본 가용량을 "
                           "대체합니다. 미정과 0%를 구분합니다.",
                           {{"자원 ID", 28}, {"자원", 25}, {"종류", 14}, {"적용 날짜", 18}, {"가용률", 14}});
    for (const auto &r : p.resources)
        if (o.wbsId.empty() || resourceIds.contains(r.id)) {
            registry.rows.push_back({text(r.id), text(r.name), text(r.type == "labor" ? "인력" : "장비"), text("기본"),
                                     r.maxUnits ? number(*r.maxUnits, CellFormat::Units) : ReportCell{}});
            if (r.availableFrom || r.availableTo)
                registry.rows.push_back(
                    {text(r.id),
                     text(r.name),
                     text("가용 경계"),
                     text(r.availableFrom.value_or("제한 없음") + " ~ " + r.availableTo.value_or("제한 없음")),
                     {}});
            for (const auto &v : r.availability)
                registry.rows.push_back({text(r.id), text(r.name), text("기간"),
                                         text(v.from.value_or("제한 없음") + " ~ " + v.to.value_or("제한 없음")),
                                         v.units ? number(*v.units, CellFormat::Units) : ReportCell{}});
            for (const auto &d : r.exceptions)
                registry.rows.push_back({text(r.id), text(r.name), text(r.type == "labor" ? "인력" : "장비"),
                                         text(d.date), d.units ? number(*d.units, CellFormat::Units) : ReportCell{}});
        }
    if (!p.resources.empty()) {
        const auto at = p.statusAt.value_or(p.startDate + " 09:00");
        auto &remaining = table("resourceRemainingAvailability", "현재 부하와 잔여 가용량", ReportFamily::Resources,
                                "기준 시각은 상태일, 없으면 프로젝트 시작일09:00입니다. 자원 근무 달력/가용기간을 "
                                "반영하고 전체 프로젝트의 배정 수요를 차감합니다. 음수는 부족분이며 추가 배정 "
                                "가능량은0 이상입니다. 예측 상태일 이전의 실제 수요는 추정하지 않습니다.",
                                {{"자원 ID", 22},
                                 {"자원", 22},
                                 {"조회 시각", 24},
                                 {"유효 가용률", 14},
                                 {"배정 수요", 14},
                                 {"잔여 가용률", 14},
                                 {"추가 배정 가능", 14},
                                 {"부족", 14},
                                 {"상태", 28}});
        try {
            ResourceAvailabilityQuery query(p, active);
            for (const auto &r : p.resources)
                if (o.wbsId.empty() || resourceIds.contains(r.id)) {
                    const auto point = query.at(r.id, at);
                    remaining.rows.push_back(
                        {text(r.id), text(r.name), text(at),
                         point.capacity ? number(*point.capacity, CellFormat::Units) : ReportCell{},
                         point.demand ? number(*point.demand, CellFormat::Units) : ReportCell{},
                         point.remaining ? number(*point.remaining, CellFormat::Units) : ReportCell{},
                         point.freeUnits ? number(*point.freeUnits, CellFormat::Units) : ReportCell{},
                         point.shortage ? number(*point.shortage, CellFormat::Units) : ReportCell{},
                         text(point.issue)});
                }
        } catch (const CalculationCancelled &) {
            throw;
        } catch (const std::exception &e) {
            remaining.rows.push_back({{}, {}, text(at), {}, {}, {}, {}, {}, text(e.what())});
        }
    }
    if (hasClassification(p)) {
        const auto keys = attributeKeys(p);
        if (rows.size() * keys.size() > 1000000 / 6)
            throw AppError(ErrorCode::REPORT_CELL_LIMIT, {});
        const auto labels = attributeLabels(p);
        auto &attributes = table(
            "taskClassification", "작업 사용자 필드와 코드", ReportFamily::Schedule,
            "선택 WBS의 작업 분류 명세입니다. 그룹·검색은 임시 작업 보기이며 이 보고서의 WBS 범위를 바꾸지 않습니다. "
            "미설정과 숫자 0·빈 문자열을 구분하며 원가/공수에 합산하지 않습니다.",
            {{"작업 ID", 28}, {"작업", 24}, {"분류 키", 28}, {"필드/코드", 24}, {"값", 32}, {"단위", 12}});
        for (const auto row : rows)
            for (const auto &key : keys) {
                const auto &task = p.tasks[row];
                const auto value = resolvedAttributeValue(p, task, key);
                ReportCell cell;
                const auto *field = customField(p, key);
                if (value)
                    cell = field && field->type == CustomFieldType::Cost
                               ? number(parseFieldCost(*value, p.currency), CellFormat::Money)
                           : field && field->type == CustomFieldType::Number
                               ? number(parseFieldNumber(*value), CellFormat::FieldNumber)
                               : text(attributeText(labels, key, *value));
                attributes.rows.push_back({text(task.id), text(task.name), text(key), text(attributeName(p, key)), cell,
                                           text(attributeUnit(p, key))});
            }
    }
    const RollUpValues summaryValues(p);
    if (!summaryValues.fields().empty()) {
        auto &summaries = table("xmlRollUp", "XML 사용자 필드 요약", ReportFamily::Schedule,
                                "하위 사용자 필드의 요약입니다. 일정·공수·원가 지표에 합산하지 않습니다. 자동 계산과 "
                                "명시 원본 값을 구분합니다. 평균은 설정된 계층 가중치를 따릅니다.",
                                {{"WBS ID", 24},
                                 {"요약", 24},
                                 {"필드", 24},
                                 {"계산 방식", 24},
                                 {"값", 24},
                                 {"단위", 12},
                                 {"출처", 24},
                                 {"상태", 32}});
        auto append = [&](const std::string &id, const std::string &name) {
            for (const auto *f : summaryValues.fields()) {
                const auto key = "field:" + f->id;
                const auto &c = summaryValues.cell(id, key);
                ReportCell value;
                if (c.value)
                    value = f->type == CustomFieldType::Number
                                ? number(parseFieldNumber(*c.value), CellFormat::FieldNumber)
                            : f->type == CustomFieldType::Cost
                                ? number(parseFieldCost(*c.value, p.currency), CellFormat::Money)
                                : text(attributeText(p, key, *c.value));
                const auto automatic = f->xmlRollUp && f->xmlRollUp->calculationType == 1;
                const auto method = automatic ? f->xmlRollUp->rollupType : -1;
                summaries.rows.push_back(
                    {text(id), text(name), text(f->name),
                     text(automatic ? rollUpMethodName(method)
                          : f->xmlRollUp && f->xmlRollUp->calculationType == 2 && f->xmlFormula &&
                                  f->xmlFormula->enabled
                              ? "XML 계산식"
                              : "명시 값"),
                     value, text(method == 2 || method == 6 || method == 7 ? "개" : attributeUnit(p, key)),
                     text(c.origin), text(c.issue)});
            }
        };
        if (o.wbsId.empty())
            append("", p.name);
        const auto subtree = wbsSubtree(p, o.wbsId);
        for (const auto &g : p.wbsNodes)
            if (o.wbsId.empty() || subtree.contains(g.id))
                append(g.id, g.name);
    }
    for (const auto scope : {FieldScope::Project, FieldScope::Resource}) {
        const auto keys = attributeKeys(p, scope);
        if (keys.empty())
            continue;
        const auto labels = attributeLabels(p, scope);
        auto &objects =
            table(scope == FieldScope::Project ? "projectClassification" : "resourceClassification",
                  scope == FieldScope::Project ? "프로젝트 사용자 필드" : "자원 사용자 필드",
                  scope == FieldScope::Project ? ReportFamily::Schedule : ReportFamily::Resources,
                  scope == FieldScope::Project
                      ? "프로젝트 전체 값입니다. WBS별 값이나 합계가 아니며 일정/공수/원가에 합산하지 않습니다."
                      : "전체 범위는 미배정 자원을 포함하고, WBS 범위는 해당 작업에 배정된 자원만 표시합니다. 값은 "
                        "공수/원가에 합산하지 않습니다.",
                  {{"객체 ID", 28}, {"프로젝트/자원", 25}, {"필드 키", 28}, {"필드", 24}, {"값", 32}, {"단위", 12}});
        const auto append = [&](const std::string &id, const std::string &name, const auto &values,
                                const Resource *resource = nullptr) {
            if (objects.rows.size() + keys.size() > 1000000 / 6)
                throw AppError(ErrorCode::REPORT_CELL_LIMIT);
            for (const auto &key : keys) {
                const auto value =
                    resource ? resolvedAttributeValue(p, *resource, key) : resolvedProjectAttributeValue(p, key);
                (void)values;
                const auto *field = customField(p, key, scope);
                ReportCell cell;
                if (value)
                    cell = field->type == CustomFieldType::Cost
                               ? number(parseFieldCost(*value, p.currency), CellFormat::Money)
                           : field->type == CustomFieldType::Number
                               ? number(parseFieldNumber(*value), CellFormat::FieldNumber)
                               : text(attributeText(labels, key, *value));
                objects.rows.push_back(
                    {text(id), text(name), text(key), text(field->name), cell, text(attributeUnit(p, key, scope))});
            }
        };
        if (scope == FieldScope::Project)
            append(p.documentId, p.name, p.projectAttributes);
        else
            for (const auto &resource : p.resources)
                if (o.wbsId.empty() || resourceIds.contains(resource.id))
                    append(resource.id, resource.name, resource.attributes, &resource);
    }
    if (!p.assignmentFields.empty()) {
        const auto keys = attributeKeys(p, FieldScope::Assignment);
        auto &attributes = table("assignmentClassification", "배정 사용자 필드", ReportFamily::Resources,
                                 "작업/자원 배정의 수동 값입니다. 공수/원가에 합산하지 않습니다.",
                                 {{"작업 ID", 28}, {"자원 ID", 28}, {"필드", 24}, {"값", 32}, {"단위", 12}});
        std::set<std::string> includedTasks;
        for (auto index : rows)
            includedTasks.insert(p.tasks[index].id);
        const RollDownValues inherited(p);
        for (const auto &a : p.assignments) {
            if (!includedTasks.contains(a.taskId))
                continue;
            if (attributes.rows.size() + keys.size() > 1000000 / 5)
                throw AppError(ErrorCode::REPORT_CELL_LIMIT);
            for (const auto &key : keys) {
                const auto *f = customField(p, key, FieldScope::Assignment);
                const auto v = inherited.value(a, key);
                ReportCell cell;
                if (v)
                    cell = f->type == CustomFieldType::Cost ? number(parseFieldCost(*v, p.currency), CellFormat::Money)
                           : f->type == CustomFieldType::Number
                               ? number(parseFieldNumber(*v), CellFormat::FieldNumber)
                               : text(attributeText(p, key, *v, FieldScope::Assignment));
                attributes.rows.push_back({text(a.taskId), text(a.resourceId), text(f->name), cell,
                                           text(attributeUnit(p, key, FieldScope::Assignment))});
            }
        }
    }
    if (active.levelingFloatCalculated) {
        auto &resourceOrder = table(
            "resourceOrder", "자원 순서 관계", ReportFamily::Schedule,
            repartition ? "현재 배치의 참고 인계 · 재분할 후보에서 소멸 가능 · 진단의 재배치 증인과 구분"
                        : "전체 일정에서 도출한 자원 슬롯 흐름 · 원본 선후행 관계와 구분 · 여유는 이 순서를 고정함",
            {{"선행 ID", 24}, {"후행 ID", 24}, {"자원 ID", 24}, {"단위(%)", 14}, {"상태", 18}});
        const bool segments = std::any_of(active.resourceOrderLinks.begin(), active.resourceOrderLinks.end(),
                                          [](const auto &link) { return !link.releaseAt.empty(); });
        if (segments) {
            resourceOrder.columns.push_back({"선행 실행 완료", 28});
            resourceOrder.columns.push_back({"후행 실행 시작", 28});
        }
        for (const auto &link : active.resourceOrderLinks) {
            resourceOrder.rows.push_back({text(link.predecessorId), text(link.taskId), text(link.resourceId),
                                          number(link.units), text(link.driving ? "driving" : "여유")});
            if (segments) {
                resourceOrder.rows.back().push_back(text(link.releaseAt));
                resourceOrder.rows.back().push_back(text(link.acquireAt));
            }
        }
    }
    auto &diagnostics = table(
        "diagnostics", "진단", ReportFamily::All,
        "입력 불완전/충돌/미정은 원자료와 함께 보존합니다. 날짜/금액이 없다는 사실을 정상 0으로 해석하지 않습니다.",
        {{"분야", 16}, {"코드", 27}, {"설명", 76}, {"관련 작업 ID", 30}});
    auto append = [&](const std::string &source, const std::vector<Diagnostic> &ds) {
        for (const auto &d : ds) {
            std::string refs;
            for (const auto &id : d.taskIds) {
                if (!refs.empty())
                    refs += ", ";
                refs += id;
            }
            diagnostics.rows.push_back({text(source), text(d.code), text(diagnosticMessage(d)), text(refs)});
        }
    };
    append("계획 일정", comparison.plan.diagnostics);
    append("잔여 예측", active.diagnostics);
    append("공유 자원", load.diagnostics);
    append("원가/EVM", costs.diagnostics);
    auto &source = table("source", "출처와 설정", ReportFamily::All,
                         "재계산된 메모리 스냅샷의 정적 보고서입니다. 엑셀 셀 편집이 PPM 원본을 변경하거나 앱 계산을 "
                         "재실행하지 않습니다.",
                         {{"항목", 32}, {"값", 100}});
    auto meta = [&](const std::string &key, const std::string &value) {
        source.rows.push_back({text(key), text(value)});
    };
    meta("프로젝트", p.name);
    meta("문서 ID", p.documentId);
    meta("입력 revision", std::to_string(p.revision));
    meta("원본 상태", o.unsaved ? "미저장 메모리 revision" : "저장된 revision");
    meta("보고서 ID", o.reportId);
    meta("생성 UTC", o.generatedAt);
    meta("상태일(civil)", p.statusAt.value_or("미설정"));
    meta("현재 WBS 범위", report.scopeName);
    if (hasXmlScheduleReferences(p, true))
        meta("XML 일정 참조 기준", xmlScheduleCaption(p));
    meta("WBS ID", o.wbsId.empty() ? "프로젝트 루트" : o.wbsId);
    meta("일별 명세 시작", o.fromDate.value_or("제한 없음"));
    meta("일별 명세 끝", o.toDate.value_or("제한 없음"));
    meta("일별 필터 의미", "일별 자원/원가 명세·추이에만 적용. 누적 금액·완료일·작업 수는 전체 계획/상태일 기준");
    meta("기준선 ID", baseline ? baseline->id : "없음");
    meta("기준선 계산 엔진", baseline ? baseline->engineVersion : "없음");
    if (baseline) {
        meta("기준선", baseline->name);
        meta("기준선 버전", std::to_string(baseline->version));
        meta("승인자", baseline->approvedBy);
        meta("승인 사유", baseline->reason);
        meta("기준선 생성 UTC", baseline->createdAt);
        meta("기준선 원본 ID", baseline->sourceDocumentId);
        meta("기준선 입력 revision", std::to_string(baseline->inputRevision));
    }
    meta("현재 계획 계산 엔진", comparison.plan.engineVersion);
    meta("계산 엔진", active.engineVersion);
    meta("계획 계산 정책", comparison.plan.calculationPolicy);
    meta("활성 일정 정책", active.calculationPolicy);
    if (active.resourceLeveled)
        meta("평준화 후 여유", active.levelingFloatCalculated
                                   ? (materialFloat ? "자재 입고·재고 검산 / 구간 재생성 / 작업별 재배치 증인"
                                      : repartition ? "구간 재생성 / 최종 반환 순서 유지 / 작업별 재배치 증인"
                                                    : "자원 슬롯 순서 고정 / resource-order-float-v1")
                                   : "미산정: " + active.levelingFloatReason);
    if (p.progressPolicy) {
        const auto &s = *p.progressPolicy;
        meta("진척 정책 담당자", s.adoptedBy);
        meta("진척 정책 사유", s.reason);
        meta("진척 정책 채택 UTC", s.adoptedAt);
        meta("진척 정책 검토 원본", s.sourceDocumentId + " rev " + std::to_string(s.sourceRevision));
    }
    meta("계산 profile", p.calculationProfile);
    meta("원가 정책", p.costProfile);
    meta("EAC 가정", o.formula);
    meta("통화", p.currency);
    meta("기본 달력", p.calendar + " / 월-금 09-12, 13-18 / 1일 480 근무분");
    std::string holidays;
    for (const auto &h : p.holidays) {
        if (!holidays.empty())
            holidays += ", ";
        holidays += h;
    }
    meta("기본 달력 휴일", holidays.empty() ? "없음" : holidays);
    for (const auto &c : p.calendars) {
        std::string detail = c.name + " [" + c.id + "] 요일:";
        for (int day : c.weekdays)
            detail += " " + std::to_string(day);
        detail += " 시간대(자정 후 분):";
        for (const auto &s : c.periods)
            detail += " " + std::to_string(s.begin) + "-" + std::to_string(s.end);
        detail += " 휴일:";
        for (const auto &d : c.holidays)
            detail += " " + d;
        meta("작업 달력 정의", detail);
    }
    for (auto i : rows)
        meta("작업 달력 배정", p.tasks[i].id + " / " + p.tasks[i].name + " / " + taskCalendarName(p, p.tasks[i]));
    meta("자원 범위", "선택 작업 배정 자원. 다른 WBS 작업의 공유 수요를 포함해 가용량과 비교");
    meta("원가 범위",
         "현재 WBS의 작업 ID. EVM은 선택 기준선과의 교집합. 전체 프로젝트 선택 시 삭제된 기준선 작업도 포함");
    meta("미정 표시", "미정은 누락/분모 0/비교 불가를 뜻하며 숫자 0과 구분. 해당없음은 별도 표시");
    meta("보고서 정책", "ppm-report-snapshot-v1");
    if (p.planLeveling || p.remainingLeveling) {
        const auto &s = p.planLeveling ? *p.planLeveling : *p.remainingLeveling;
        meta("평준화 채택 원본", s.sourceDocumentId + " / rev " + std::to_string(s.sourceRevision));
        meta("평준화 채택", std::string(p.planLeveling ? "계획" : "잔여 예측") + " / " + s.policy + " / " + s.priority +
                                " / " + std::to_string(s.horizonWorkDays) + "근무일 / " + s.adoptedAt + " / " +
                                s.reason + " / 자원 반영 여유·임계 (계산 여부와 구간 기준은 활성 일정 출처 참조)");
    }
    if (!p.periodActuals.empty()) {
        std::set<std::string> actualIds;
        for (auto i : rows)
            actualIds.insert(p.tasks[i].id);
        auto actual = periodActualTables(p, actualIds, o.fromDate, o.toDate);
        report.tables.insert(report.tables.end(), std::make_move_iterator(actual.begin()),
                             std::make_move_iterator(actual.end()));
    }
    if (!p.activities.empty() || !p.stepReports.empty()) {
        std::set<std::string> selectedIds;
        for (auto i : rows)
            selectedIds.insert(p.tasks[i].id);
        ReportTable stepTable{"activitySteps",
                              "Steps 물리 진척",
                              "보고 시점의 가중치 · 기간/공수 진척과 합산하지 않음",
                              ReportFamily::Schedule,
                              {{"작업 ID", 22},
                               {"Step", 25},
                               {"가중치", 12},
                               {"Step 진척(%)", 16},
                               {"Steps 합계(%)", 16},
                               {"상태일", 24},
                               {"물리 원천", 16}},
                              {}};
        for (const auto &a : p.activities)
            if (a.kind == "steps" && selectedIds.contains(a.taskId)) {
                const auto *reported = p.statusAt ? stepsAsOf(p, a.taskId, *p.statusAt) : nullptr;
                if (reported)
                    for (const auto &v : reported->steps)
                        stepTable.rows.push_back(
                            {text(a.taskId), text(v.step.name), number(v.step.weight),
                             number(v.basisPoints, CellFormat::PhysicalPercent),
                             number(weightedStepProgress(reported->steps), CellFormat::PhysicalPercent),
                             text(reported->statusAt), text(a.physicalFromSteps ? "Steps" : "수동")});
                else
                    for (const auto &v : a.steps)
                        stepTable.rows.push_back({text(a.taskId),
                                                  text(v.name),
                                                  number(v.weight),
                                                  {},
                                                  {},
                                                  {},
                                                  text(a.physicalFromSteps ? "Steps" : "수동")});
            }
        report.tables.push_back(std::move(stepTable));
        if (o.wbsId.empty()) {
            ReportTable support{
                "supportActivities",
                "LOE·WBS Summary 지원 구간",
                "프로젝트 전체 · 실제 작업에서 파생 · 작업 수/공수/예산에 중복 합산하지 않음 · CPM 여유 해당없음",
                ReportFamily::Schedule,
                {{"지원 활동", 28},
                 {"종류", 18},
                 {"시작", 24},
                 {"완료", 24},
                 {"근무 기간(분)", 18},
                 {"원본 작업 수", 16},
                 {"미산정 사유", 32}},
                {}};
            for (const auto &a : activityIntervals(p, active))
                support.rows.push_back({text(a.name), text(a.kind), stamp(a.start), stamp(a.finish),
                                        optional(a.durationMinutes, CellFormat::Integer),
                                        number(std::int64_t(a.sourceTasks)), text(a.reason)});
            report.tables.push_back(std::move(support));
        }
    }
    if (!p.materialActuals.empty()) {
        const auto quantity = [](std::int64_t n) {
            return (n < 0 ? "-" : "") + formatMaterialQuantity(n < 0 ? -n : n);
        };
        auto &ledger =
            table("materialActuals", "실제 자재 원장", ReportFamily::Resources,
                  "프로젝트 전체 추가 전용 기록 · 정정은 이전 거래 값을 대체 · 실제 원가를 자동 생성하지 않음",
                  {{"기록 ID", 22},
                   {"종류", 14},
                   {"자재", 18},
                   {"작업/소모", 22},
                   {"연결 입고", 22},
                   {"시각", 28},
                   {"수량", 18},
                   {"단위", 12},
                   {"상태일", 28},
                   {"이전 기록", 22},
                   {"작성자", 18},
                   {"사유", 28}});
        for (const auto &e : p.materialActuals)
            ledger.rows.push_back({text(e.id), text(e.kind), text(e.materialId),
                                   text(e.useId.empty() ? e.taskId : e.useId), text(e.receiptId), text(e.at),
                                   text(quantity(e.quantityMilli)), text(e.unit), text(e.statusAt),
                                   text(e.previousEntryId), text(e.reporter), text(e.reason)});
        if (p.statusAt) {
            auto &stocks = table("actualMaterialStock", "상태일 실제 자재 재고", ReportFamily::Resources,
                                 "개시 재고 + 실제 입고 - 실제 소모 · 현재 상태일까지 알려진 정정 · 미확인 재고는 자동 "
                                 "대기에 사용하지 않음",
                                 {{"자재", 20},
                                  {"단위", 12},
                                  {"상태일", 28},
                                  {"개시", 18},
                                  {"입고", 18},
                                  {"소모", 18},
                                  {"잔여", 18},
                                  {"확인", 16}});
            for (const auto &s : actualMaterialStock(p, *p.statusAt))
                stocks.rows.push_back({text(s.materialId), text(s.unit), text(s.at),
                                       s.opening ? text(quantity(*s.opening)) : ReportCell{},
                                       text(quantity(s.received)), text(quantity(s.consumed)),
                                       s.remaining ? text(quantity(*s.remaining)) : ReportCell{},
                                       text(s.confirmed ? "확인" : "미확인")});
        }
    }
    if (hasResourcePlan(p)) {
        auto &materials = table("materials", "자재 재고·소모", ReportFamily::Resources,
                                "재고는 프로젝트 전체, 소모·원가는 현재 WBS 범위 · 수량은 소수 3자리 · 빈 단가/재고는 "
                                "미정 · 재입고를 추정하지 않음",
                                {{"자재 ID", 22},
                                 {"이름", 24},
                                 {"단위", 12},
                                 {"재고", 18},
                                 {"범위 소모", 18},
                                 {"전체 소모", 18},
                                 {"단가", 18}});
        for (const auto &m : p.resourcePlan.materials) {
            std::int64_t scope = 0, total = 0;
            for (const auto &u : p.resourcePlan.uses)
                if (u.materialId == m.id) {
                    total += u.quantityMilli;
                    if (ids.contains(u.taskId))
                        scope += u.quantityMilli;
                }
            materials.rows.push_back({text(m.id), text(m.name), text(m.unit),
                                      m.stockMilli ? text(formatMaterialQuantity(*m.stockMilli)) : ReportCell{},
                                      text(formatMaterialQuantity(scope)), text(formatMaterialQuantity(total)),
                                      optional(m.unitPriceMinor, CellFormat::Money)});
        }
        auto &uses = table(
            "materialUses", "작업별 자재 소모 원가", ReportFamily::Costs,
            "통화 최소 단위 정수로 반올림 · 예산에 1회 합산 · 실제 원가를 생성하지 않음",
            {{"소모 ID", 22}, {"작업 ID", 22}, {"자재 ID", 22}, {"수량", 18}, {"발생 방식", 16}, {"계획 원가", 20}});
        for (const auto &u : p.resourcePlan.uses)
            if (ids.contains(u.taskId)) {
                const auto m = std::find_if(p.resourcePlan.materials.begin(), p.resourcePlan.materials.end(),
                                            [&](const auto &m) { return m.id == u.materialId; });
                uses.rows.push_back({text(u.id), text(u.taskId), text(u.materialId),
                                     text(formatMaterialQuantity(u.quantityMilli)), text(u.accrual),
                                     optional(materialCost(*m, u.quantityMilli), CellFormat::Money)});
            }
        if (hasMaterialInventory(p)) {
            if (p.resourcePlan.autoDelayRemainingForReceipts) {
                auto &policy = table("remainingMaterialDelay", "잔여 자재 자동 대기", ReportFamily::Resources,
                                     "상태일 실제 재고 확인·명시 잔여 소모·미래 계획 입고의 미입고 잔량을 사용",
                                     {{"항목", 28}, {"설정", 60}});
                policy.rows.push_back({text("계산 정책"), text(remainingMaterialPolicy)});
                policy.rows.push_back({text("자동 지연"), text("사용")});
            }
            if (p.resourcePlan.autoDelayForReceipts) {
                auto &policy =
                    table("materialAutoDelay", "자재 입고 자동 대기", ReportFamily::Resources,
                          "우선순위에 따른 직렬 예약 · 자원/선후관계와 재고를 함께 검사 · 계획 평준화 비교/채택에 적용",
                          {{"항목", 28}, {"설정", 60}});
                policy.rows.push_back({text("계산 정책"), text(materialArrivalPolicy)});
                policy.rows.push_back({text("자동 지연"), text("사용")});
            }
            auto &receipts =
                table("materialReceipts", "계획 자재 입고", ReportFamily::Resources,
                      "프로젝트 전체 입고 계획 · 실제 입고/원가 원장이 아님 · 재고 부족 시점은 계획 재고 조회에서 확인",
                      {{"입고 ID", 22}, {"자재 ID", 22}, {"입고 시각", 28}, {"입고 수량", 18}});
            for (const auto &r : p.resourcePlan.receipts)
                receipts.rows.push_back(
                    {text(r.id), text(r.materialId), text(r.at), text(formatMaterialQuantity(r.quantityMilli))});
            auto &opening = table("materialOpening", "자재 개시 재고 기준", ReportFamily::Resources,
                                  "해당 분의 입고/소모 직전 재고 · 기준 이전 계획 소모는 다시 차감하지 않음",
                                  {{"자재 ID", 22}, {"기준 시각", 28}, {"개시 재고", 20}});
            for (const auto &m : p.resourcePlan.materials)
                opening.rows.push_back({text(m.id), text(m.stockAt.value_or(p.startDate + " 09:00")),
                                        m.stockMilli ? text(formatMaterialQuantity(*m.stockMilli)) : ReportCell{}});
        }
        auto &expenses =
            table("expenses", "비용계정별 계획 비용", ReportFamily::Costs,
                  "직접 입력 비용과 자재·자원 원가는 각각 1회 합산 · 빈 금액은 미정 · 실제 지출과 별도",
                  {{"비용 ID", 22}, {"작업 ID", 22}, {"항목", 26}, {"비용계정", 22}, {"금액", 20}, {"발생 방식", 16}});
        for (const auto &e : p.resourcePlan.expenses)
            if (ids.contains(e.taskId))
                expenses.rows.push_back({text(e.id), text(e.taskId), text(e.name), text(e.account),
                                         optional(e.amountMinor, CellFormat::Money), text(e.accrual)});
        auto &roles = table("roleDemands", "역할 수요·구체 자원 연결", ReportFamily::Resources,
                            "연결된 역할은 기존 배정 1건으로 계산 · 미연결 역할은 일정 미완성",
                            {{"수요 ID", 22}, {"작업 ID", 22}, {"역할", 24}, {"자원 ID", 22}, {"배정률(%)", 18}});
        for (const auto &d : p.resourcePlan.demands)
            if (ids.contains(d.taskId)) {
                const auto r = std::find_if(p.resourcePlan.roles.begin(), p.resourcePlan.roles.end(),
                                            [&](const auto &r) { return r.id == d.roleId; });
                roles.rows.push_back({text(d.id), text(d.taskId), text(r->name),
                                      d.resourceId.empty() ? ReportCell{} : text(d.resourceId), number(d.units)});
            }
    }
    if (!p.remainingWork.empty()) {
        auto &dated = table(
            "datedRemainingWork", "날짜별 잔여 공수 입력", ReportFamily::Resources,
            "빈 날짜는 기존 잔여 곡선 · 0은 수요 없음 · 공수 기준은 잔여 총공수에서 날짜 목표를 확보하고 나머지를 "
            "기존 곡선으로 채워 완료 계산 · 기간 기준은 잔여 기간 유지 · 실제공수/계획 예산과 별개 · 상태일 변경 시 "
            "재검토",
            {{"작업 ID", 22},
             {"자원 ID", 22},
             {"입력 기준 상태일", 24},
             {"날짜", 16},
             {"목표 잔여 공수(h)", 20},
             {"상태일 일치", 20}});
        for (const auto &v : p.remainingWork)
            if (ids.contains(v.taskId))
                for (const auto &d : v.days)
                    dated.rows.push_back(
                        {text(v.taskId), text(v.resourceId), text(v.statusAt), text(d.date),
                         number(d.workUnitMinutes, CellFormat::WorkHours),
                         text(p.statusAt && *p.statusAt == v.statusAt ? "일치 · 실행 결과 확인" : "재검토 필요")});
    }
    if (hasResourcePlanning(p)) {
        auto &rates = table(
            "resourceProductivity", "자원 생산성 정의", ReportFamily::Resources,
            "물량/자원시간의 명시 측정·가정 · 단가/가용량과 별개 · 정의 수정은 기존 계획을 자동 변경하지 않음",
            {{"자원 ID", 24}, {"물량 단위", 16}, {"시간당 물량", 18}, {"기준일", 16}, {"근거", 40}, {"작성자", 22}});
        for (const auto &r : p.resources)
            if (r.productivity && (o.wbsId.empty() || resourceIds.contains(r.id))) {
                const auto &v = *r.productivity;
                rates.rows.push_back({text(r.id), text(v.unit),
                                      ReportCell{v.quantityMilliPerHour / 1000.0, CellFormat::FieldNumber},
                                      text(v.effectiveDate), text(v.reference), text(v.reporter)});
            }
        const auto estimatesIndex = report.tables.size();
        table("productivityEstimates", "물량·생산성 공수 산정", ReportFamily::Resources,
              "당시 생산성 스냅샷의 산정 근거 · 이후 곡선/달력/공수 편집 뒤 현재 계획과 재대조 · 실제공수/관측 원장을 "
              "생성하지 않음",
              {{"작업 ID", 24},
               {"자원 ID", 24},
               {"물량", 18},
               {"단위", 14},
               {"시간당 물량", 18},
               {"산정 공수(h)", 20},
               {"반영 방식", 22},
               {"기준일/근거", 40},
               {"작성자/사유", 40},
               {"기록 UTC", 26}});
        const auto curvesIndex = report.tables.size();
        table("nativeCurves", "외부 고유 곡선 원본", ReportFamily::Resources,
              "원래21지점 또는 XML 시간대별 투영 정의 · 현재 계획과 다른 원본도 유지 · 원래 파일/정확한 실행은 교환 "
              "진단과 source.ppm 참조",
              {{"작업 ID", 24},
               {"자원 ID", 24},
               {"출처/원본 ID", 26},
               {"이름", 24},
               {"당시 기간(분)", 18},
               {"당시 공수(h)", 18},
               {"구간 정의", 24},
               {"21지점 비중(%)", 64},
               {"기록 근거", 40}});
        for (const auto &a : p.assignments)
            if (ids.contains(a.taskId)) {
                if (a.productivityEstimate) {
                    const auto &e = *a.productivityEstimate;
                    report.tables[estimatesIndex].rows.push_back(
                        {text(a.taskId), text(a.resourceId),
                         ReportCell{e.quantityMilli / 1000.0, CellFormat::FieldNumber}, text(e.rate.unit),
                         ReportCell{e.rate.quantityMilliPerHour / 1000.0, CellFormat::FieldNumber},
                         number(e.workUnitMinutes, CellFormat::WorkHours),
                         text(e.application == "duration-rates-v1" ? "기간 유지" : "배정률 유지·기간 역산"),
                         text(e.rate.effectiveDate + " / " + e.rate.reference), text(e.reporter + " / " + e.reason),
                         text(e.recordedAt)});
                }
                if (a.nativeCurve) {
                    const auto &c = *a.nativeCurve;
                    std::string weights;
                    for (int w : c.weights) {
                        if (!weights.empty())
                            weights += ", ";
                        const auto fraction = std::to_string(w % 1000000);
                        weights += std::to_string(w / 1000000) + "." + std::string(6 - fraction.size(), '0') + fraction;
                    }
                    report.tables[curvesIndex].rows.push_back(
                        {text(a.taskId), text(a.resourceId), text(c.source + " / " + c.externalId), text(c.name),
                         number(c.durationMinutes), number(c.workUnitMinutes, CellFormat::WorkHours),
                         text(c.projectedPeriods == a.periods && c.baseUnits == a.units ? "구간 일치 · 일정 재검토"
                                                                                        : "현재 구간과 다름"),
                         text(weights.empty() ? "수동 시간대별 계획 공수" : weights), text(c.reference)});
                }
            }
    }
    const auto formulaTables = formulaFieldTables(p, ids, o.wbsId.empty() ? std::nullopt : std::optional{resourceIds});
    report.tables.insert(report.tables.end(), formulaTables.begin(), formulaTables.end());
    appendXmlProgressAudit(report, p, ids);
    std::size_t cells = 0;
    for (const auto &t : report.tables) {
        for (const auto &r : t.rows)
            if (r.size() != t.columns.size())
                throw AppError(ErrorCode::REPORT_COLUMN_MISMATCH, {});
        cells += t.rows.size() * t.columns.size();
    }
    if (cells > 1000000)
        throw AppError(ErrorCode::REPORT_CELL_LIMIT, {});
    appendExternalEvmTables(report, p, o.wbsId.empty() ? std::nullopt : std::optional{idList});
    appendFinancialPeriodHistory(report, p);
    return report;
}
} // namespace ppm
