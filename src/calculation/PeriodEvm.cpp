#include "calculation/FinancialPeriodReports.h"
#include "calculation/PeriodEvm.h"
#include "calculation/ExternalEvmReports.h"
#include "domain/PeriodActuals.h"
#include "domain/Wbs.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <set>
namespace jobs {
namespace {
using namespace std::chrono;
sys_days day(const std::string &s) {
    return sys_days{year{std::stoi(s.substr(0, 4))} / unsigned(std::stoi(s.substr(5, 2))) /
                    unsigned(std::stoi(s.substr(8, 2)))};
}
std::string date(sys_days d) {
    const year_month_day ymd{d};
    char out[32];
    std::snprintf(out, sizeof(out), "%04d-%02u-%02u", int(ymd.year()), unsigned(ymd.month()), unsigned(ymd.day()));
    return out;
}
Money sum(Money a, Money b) {
    if (!a || !b || (*b > 0 && *a > maxMoney - *b) || (*b < 0 && *a < -maxMoney - *b))
        return {};
    return *a + *b;
}
Money delta(Money a, Money b) {
    if (!a || !b)
        return {};
    return sum(a, -*b);
}
ReportCell text(std::string s) {
    return {std::move(s), CellFormat::Text};
}
ReportCell money(Money n) {
    return {n ? decltype(ReportCell::value){*n} : decltype(ReportCell::value){}, CellFormat::Money};
}
ReportCell ratio(Money numerator, Money denominator) {
    return numerator && denominator && *numerator >= 0 && *denominator > 0
               ? ReportCell{double(*numerator) / double(*denominator), CellFormat::Ratio}
               : ReportCell{};
}
struct Period {
    std::string from, to;
    EvmBoundary opening, closing;
};
std::vector<Period> periods(const Project &p, const std::string &from, const std::string &to,
                            const std::string &bucket) {
    std::vector<Period> out;
    const auto last = day(to);
    for (auto start = day(from); start <= last;) {
        calculationCheckpoint();
        if (out.size() == 3660)
            throw AppError(ErrorCode::REPORT_CELL_LIMIT);
        auto end = start;
        if (bucket == "week")
            end += days{7 - weekday{start}.iso_encoding()};
        else if (bucket == "month") {
            const year_month_day ymd{start};
            end = sys_days{ymd.year() / ymd.month() / std::chrono::last};
        }
        end = std::min(end, last);
        Period row;
        row.from = date(start);
        row.to = date(end);
        row.opening.stamp = row.from + " 00:00";
        row.closing.stamp = row.to == "9999-12-31" ? row.to + " 23:59" : date(end + days{1}) + " 00:00";
        row.closing.inclusive = row.to == "9999-12-31";
        row.opening.observed = p.statusAt && row.opening.stamp <= *p.statusAt;
        row.closing.observed = p.statusAt && row.opening.stamp <= *p.statusAt;
        if (row.closing.observed && row.closing.stamp > *p.statusAt) {
            row.closing.stamp = *p.statusAt;
            row.closing.inclusive = true;
        }
        out.push_back(std::move(row));
        start = end + days{1};
    }
    return out;
}
const std::vector<ReportColumn> columns = {{"시작일", 14},
                                           {"종료일", 14},
                                           {"마감 경계", 25},
                                           {"범위 ID", 24},
                                           {"범위", 26},
                                           {"BAC", 18},
                                           {"개시 PV", 18},
                                           {"마감 PV", 18},
                                           {"기간 PV", 18},
                                           {"개시 EV", 18},
                                           {"마감 EV", 18},
                                           {"기간 EV", 18},
                                           {"개시 AC", 18},
                                           {"마감 AC", 18},
                                           {"기간 AC", 18},
                                           {"기간 CV", 18},
                                           {"기간 SV", 18},
                                           {"기간 CPI", 14},
                                           {"기간 SPI", 14},
                                           {"마감 EAC", 18},
                                           {"마감 ETC", 18},
                                           {"현재 날짜배분", 20},
                                           {"보고증감-날짜배분", 24},
                                           {"개시 EV 근거", 16},
                                           {"마감 EV 근거", 16},
                                           {"개시 AC 근거", 16},
                                           {"마감 AC 근거", 16},
                                           {"개시 원가 보고 ID", 28},
                                           {"마감 원가 보고 ID", 28},
                                           {"개시 EV 보고 ID", 28},
                                           {"마감 EV 보고 ID", 28}};
struct Total {
    Evm opening, closing;
    Money dated = 0;
    std::size_t tasks = 0, openingEv = 0, closingEv = 0, openingAc = 0, closingAc = 0;
    Total() {
        opening.bac = opening.pv = opening.ev = opening.ac = 0;
        closing = opening;
    }
    void add(const PeriodEvmPoint &a, const PeriodEvmPoint &b, Money allocated) {
        opening.estimateSupported = opening.estimateSupported && a.value.estimateSupported;
        closing.estimateSupported = closing.estimateSupported && b.value.estimateSupported;
        opening.bac = sum(opening.bac, a.value.bac);
        opening.pv = sum(opening.pv, a.value.pv);
        opening.ev = sum(opening.ev, a.value.ev);
        opening.ac = sum(opening.ac, a.value.ac);
        closing.bac = sum(closing.bac, b.value.bac);
        closing.pv = sum(closing.pv, b.value.pv);
        closing.ev = sum(closing.ev, b.value.ev);
        closing.ac = sum(closing.ac, b.value.ac);
        dated = sum(dated, allocated);
        ++tasks;
        openingEv += a.value.ev.has_value();
        closingEv += b.value.ev.has_value();
        openingAc += a.value.ac.has_value();
        closingAc += b.value.ac.has_value();
    }
};
std::vector<ReportCell> cells(const Period &period, const std::string &id, const std::string &name, Total t,
                              const std::string &formula, const PeriodEvmPoint &opening = {},
                              const PeriodEvmPoint &closing = {}) {
    if (!period.opening.observed)
        t.opening.ev = t.opening.ac = {};
    if (!period.closing.observed) {
        t.closing.ev = t.closing.ac = {};
        t.dated.reset();
    }
    const auto pv = delta(t.closing.pv, t.opening.pv), ev = delta(t.closing.ev, t.opening.ev),
               ac = delta(t.closing.ac, t.opening.ac);
    const auto end = nativeEvmIndicators(t.closing, formula);
    auto coverage = [&](std::size_t n) { return text(std::to_string(n) + "/" + std::to_string(t.tasks)); };
    return {text(period.from),
            text(period.to),
            text(period.closing.stamp + (period.closing.inclusive ? " 이하" : " 미만")),
            text(id),
            text(name),
            money(end.bac),
            money(t.opening.pv),
            money(end.pv),
            money(pv),
            money(t.opening.ev),
            money(end.ev),
            money(ev),
            money(t.opening.ac),
            money(end.ac),
            money(ac),
            money(delta(ev, ac)),
            money(delta(ev, pv)),
            ratio(ev, ac),
            ratio(ev, pv),
            money(end.eac),
            money(end.etc),
            money(t.dated),
            money(delta(ac, t.dated)),
            coverage(t.openingEv),
            coverage(t.closingEv),
            coverage(t.openingAc),
            coverage(t.closingAc),
            text(opening.acReportId),
            text(closing.acReportId),
            text(opening.evReportId),
            text(closing.evReportId)};
}
} // namespace
ProjectReport buildPeriodEvmReport(const Project &p, const ReportOptions &options, const std::string &bucket) {
    EvaluationScope evaluation;
    validate(p);
    if (options.reportId.empty() || !validUtcStamp(options.generatedAt) ||
        (options.fromDate && !validDate(*options.fromDate)) || (options.toDate && !validDate(*options.toDate)) ||
        (bucket != "day" && bucket != "week" && bucket != "month"))
        throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
    const auto baseline =
        std::find_if(p.baselines.begin(), p.baselines.end(), [&](const auto &b) { return b.id == options.baselineId; });
    if (baseline == p.baselines.end())
        throw AppError(ErrorCode::BASELINE_NOT_FOUND);
    const auto from = options.fromDate.value_or(baseline->input.startDate);
    const auto to = options.toDate.value_or(
        std::max(from, p.statusAt ? p.statusAt->substr(0, 10) : baseline->projectFinish.substr(0, 10)));
    if (from > to)
        throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
    std::optional<std::vector<std::string>> scope;
    if (!options.wbsId.empty()) {
        scope.emplace();
        for (auto i : wbsTaskRows(p, options.wbsId))
            scope->push_back(p.tasks[i].id);
    }
    auto spans = periods(p, from, to, bucket);
    std::vector<EvmBoundary> boundaries;
    for (const auto &s : spans) {
        boundaries.push_back(s.opening);
        boundaries.push_back(s.closing);
    }
    // Guard before allocating curves and report cells, including hierarchy amplification.
    std::set<std::string> selected;
    if (scope)
        selected.insert(scope->begin(), scope->end());
    std::map<std::string, const WbsNode *> nodes;
    for (const auto &n : baseline->input.wbsNodes)
        nodes[n.id] = &n;
    std::set<std::string> groups;
    std::size_t taskCount = 0;
    for (const auto &t : baseline->input.tasks)
        if (!scope || selected.contains(t.id)) {
            ++taskCount;
            for (auto w = t.wbsId; !w.empty(); w = nodes.at(w)->parentId) {
                calculationCheckpoint();
                groups.insert(w);
            }
        }
    if (spans.size() * (1 + taskCount + groups.size()) > 20000)
        throw AppError(ErrorCode::REPORT_CELL_LIMIT);
    const auto curves = nativeEvmCurves(p, options.baselineId, options.formula, boundaries, scope);
    ProjectReport report;
    report.projectName = p.name;
    report.documentId = p.documentId;
    report.revision = p.revision;
    report.currency = p.currency;
    report.statusAt = p.statusAt;
    report.options = options;
    report.options.fromDate = from;
    report.options.toDate = to;
    report.scopeName =
        options.wbsId.empty() ? "기준선 전체 코호트" : "현재 WBS 작업과 기준선의 교집합 · " + options.wbsId;
    const auto note = "native 기준선/보고 원장 · 기간=마감-개시 · 누락은 미정 · 음수는 정정 · CPI/SPI는 합계 비율 · "
                      "EAC/ETC는 마감 누계 · 날짜배분은 현재 상태일 기준, 누계에 중복 합산하지 않음";
    ReportTable summary{"periodEvm", "native 기간 EVM", note, ReportFamily::Costs, columns, {}};
    ReportTable tasks{"periodEvmTasks", "기간 EVM 작업", note, ReportFamily::Costs, columns, {}};
    ReportTable wbs{"periodEvmWbs", "기간 EVM 기준선 WBS", note, ReportFamily::Costs, columns, {}};
    std::map<std::string, std::map<std::string, Money>> dated;
    if (p.statusAt)
        for (const auto &[key, e] : periodActualsAsOf(p, *p.statusAt)) {
            (void)key;
            if (e->actualMinor) {
                auto [it, inserted] = dated[e->taskId].try_emplace(e->date, 0);
                (void)inserted;
                it->second = sum(it->second, e->actualMinor);
            }
        }
    // Sparse date prefix index: late reports are intentionally restated by actual date only here.
    std::map<std::string, std::vector<std::pair<std::string, Money>>> datedPrefix;
    for (const auto &[id, amounts] : dated) {
        Money total = 0;
        for (const auto &[d, n] : amounts) {
            total = sum(total, n);
            datedPrefix[id].push_back({d, total});
        }
    }
    auto datedBefore = [&](const std::string &id, const std::string &at, bool inclusive) -> Money {
        const auto &rows = datedPrefix[id];
        auto end = inclusive ? std::upper_bound(rows.begin(), rows.end(), at,
                                                [](const auto &d, const auto &v) { return d < v.first; })
                             : std::lower_bound(rows.begin(), rows.end(), at,
                                                [](const auto &v, const auto &d) { return v.first < d; });
        return end == rows.begin() ? Money{0} : std::prev(end)->second;
    };
    for (std::size_t i = 0; i < spans.size(); ++i) {
        calculationCheckpoint();
        const auto &span = spans[i];
        Total total;
        std::map<std::string, Total> wbsTotals;
        for (const auto &c : curves) {
            calculationCheckpoint();
            const auto &a = c.points[2 * i], &b = c.points[2 * i + 1];
            const auto allocated = delta(datedBefore(c.taskId, span.to, true), datedBefore(c.taskId, span.from, false));
            Total task;
            task.add(a, b, allocated);
            tasks.rows.push_back(cells(span, c.taskId, c.name, task, options.formula, a, b));
            total.add(a, b, allocated);
            for (auto w = c.wbsId; !w.empty(); w = nodes.at(w)->parentId) {
                calculationCheckpoint();
                wbsTotals[w].add(a, b, allocated);
            }
        }
        summary.rows.push_back(cells(span, p.documentId, report.scopeName, total, options.formula));
        for (const auto &n : baseline->input.wbsNodes)
            if (wbsTotals.contains(n.id))
                wbs.rows.push_back(cells(span, n.id, n.name, wbsTotals.at(n.id), options.formula));
        const auto &end = summary.rows.back();
        auto amount = [&](std::size_t n) -> Money {
            const auto *v = std::get_if<std::int64_t>(&end[n].value);
            return v ? Money{*v} : Money{};
        };
        report.costTrend.push_back({span.to, {}, {}, amount(7), amount(10), amount(13), {}});
    }
    ReportTable context{
        "periodEvmContext",  "기간 EVM 계산 근거",        "읽기 전용 파생 보고 · 원본/원장을 수정하지 않음",
        ReportFamily::Costs, {{"항목", 28}, {"값", 100}}, {}};
    auto fact = [&](std::string k, std::string v) { context.rows.push_back({text(std::move(k)), text(std::move(v))}); };
    fact("정책", "native-period-evm-v1");
    fact("기간 단위", bucket);
    fact("기준선 ID", baseline->id);
    fact("기준선 버전/원본", std::to_string(baseline->version) + " / " + baseline->sourceDocumentId + " / rev " +
                                 std::to_string(baseline->inputRevision));
    fact("승인", baseline->approvedBy + " / " + baseline->createdAt);
    fact("범위", report.scopeName + " / 기준선 작업 " + std::to_string(curves.size()));
    fact("현재 문서", p.documentId + " / rev " + std::to_string(p.revision));
    fact("조회 상태일", p.statusAt.value_or("미설정 · EV/AC 미정"));
    fact("통화/추정식", p.currency + " / " + options.formula);
    fact("날짜 범위", from + " ~ " + to + " (종료일 포함)");
    fact("생성", options.generatedAt + " / " + options.reportId);
    fact("경계", "개시일00:00 미만 / 종료 다음날00:00 미만 / 현재 기간은 상태시각 이하 / 미래 EV·AC 미정");
    fact("개시/정정", "미보고는 미정, 명시0은 알려진 값 / 같은 상태시각의 마지막 정정 우선 / 음수 증감 유지");
    fact(
        "날짜 대사",
        "현재 상태일에 알려진 날짜 배분. 배분0은 발생원가0의 증거가 아님. 차이는 보고 지연/미배분/이전 날짜 정정 포함");
    fact("외부 집계",
         p.externalEvmAdoptions.empty()
             ? "XER EV/PV는 합산하지 않음 / 기준선 밖 신규 작업 제외 / 현재 계획 단가로 AC를 재평가하지 않음"
             : "기준선/작업의 활성 XER EV/PV 채택은 native 측정값 대체, 철회는 복원 / 확인한 개시·마감만 알려진 값 / "
               "BAC 초과 시 EAC·ETC 미정 / AC 불변");
    report.tables = {std::move(summary), std::move(tasks), std::move(wbs), std::move(context)};
    report.kpis = {{"periodEvmTasks",
                    "기준선 선택 작업",
                    "개",
                    report.scopeName,
                    {std::int64_t(curves.size()), CellFormat::Integer}},
                   {"periodEvmPeriods", "선택 기간", "개", bucket, {std::int64_t(spans.size()), CellFormat::Integer}}};
    appendExternalEvmTables(report, p, scope);
    appendFinancialPeriodHistory(report, p);
    return report;
}
} // namespace jobs
