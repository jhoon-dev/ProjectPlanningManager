#include "calculation/FinancialPeriodReports.h"
#include "calculation/PeriodEvm.h"
#include "domain/FinancialPeriods.h"
#include "domain/PeriodActuals.h"
#include <algorithm>
#include <map>
#include <set>
namespace jobs {
namespace {
using M = std::optional<std::int64_t>;
M add(M a, M b) {
    if (!a || !b || (*b > 0 && *a > maxMoney - *b) || (*b < 0 && *a < -maxMoney - *b))
        return {};
    return *a + *b;
}
M delta(M a, M b) {
    return a && b ? add(a, -*b) : M{};
}
ReportCell text(const std::string &s) {
    return {s, CellFormat::Text};
}
ReportCell money(M n) {
    return {n ? decltype(ReportCell::value){*n} : decltype(ReportCell::value){}, CellFormat::Money};
}
ReportCell ratio(M a, M b) {
    return a && b && *a >= 0 && *b > 0 ? ReportCell{double(*a) / double(*b), CellFormat::Ratio} : ReportCell{};
}
FinancialPeriodBalance balance(const PeriodEvmPoint &v) {
    return {v.value.bac, v.value.pv, v.value.ev, v.value.ac, v.value.estimateSupported, v.acReportId, v.evReportId};
}
struct Total {
    M pv = 0, ev = 0, ac = 0, nativePv = 0, nativeEv = 0, dated = 0;
    Evm closing;
    std::size_t manualPv = 0, manualEv = 0, tasks = 0;
    Total() {
        closing.bac = closing.pv = closing.ev = closing.ac = 0;
    }
    void merge(const Total &b) {
        pv = add(pv, b.pv);
        ev = add(ev, b.ev);
        ac = add(ac, b.ac);
        nativePv = add(nativePv, b.nativePv);
        nativeEv = add(nativeEv, b.nativeEv);
        dated = add(dated, b.dated);
        closing.bac = add(closing.bac, b.closing.bac);
        closing.pv = add(closing.pv, b.closing.pv);
        closing.ev = add(closing.ev, b.closing.ev);
        closing.ac = add(closing.ac, b.closing.ac);
        closing.estimateSupported = closing.estimateSupported && b.closing.estimateSupported;
        manualPv += b.manualPv;
        manualEv += b.manualEv;
        tasks += b.tasks;
    }
};
Total total(const FinancialPeriodTask &t, const FinancialPeriodValue *v) {
    Total out;
    out.nativePv = delta(t.closing.pv, t.opening.pv);
    out.nativeEv = delta(t.closing.ev, t.opening.ev);
    out.manualPv = v && v->budget.has_value();
    out.manualEv = v && v->earned.has_value();
    out.tasks = 1;
    out.pv = out.manualPv ? v->budget : out.nativePv;
    out.ev = out.manualEv ? v->earned : out.nativeEv;
    out.ac = delta(t.closing.ac, t.opening.ac);
    out.dated = t.dated;
    out.closing.bac = t.closing.bac;
    out.closing.pv = t.closing.pv;
    out.closing.ev = t.closing.ev;
    out.closing.ac = t.closing.ac;
    out.closing.estimateSupported = t.closing.estimateSupported;
    return out;
}
std::vector<ReportCell> cells(const std::string &id, const std::string &name, const Total &t,
                              const std::string &formula, const FinancialPeriodTask *row = nullptr) {
    const auto e = nativeEvmIndicators(t.closing, formula);
    return {text(id),
            text(name),
            money(t.pv),
            money(t.ev),
            money(t.ac),
            money(delta(t.ev, t.ac)),
            money(delta(t.ev, t.pv)),
            ratio(t.ev, t.ac),
            ratio(t.ev, t.pv),
            money(t.nativePv),
            money(t.nativeEv),
            money(t.dated),
            money(delta(t.ac, t.dated)),
            money(e.bac),
            money(e.pv),
            money(e.ev),
            money(e.ac),
            money(e.eac),
            money(e.etc),
            text(std::to_string(t.manualPv) + "/" + std::to_string(t.tasks)),
            text(std::to_string(t.manualEv) + "/" + std::to_string(t.tasks)),
            text(row ? row->opening.acReportId : ""),
            text(row ? row->closing.acReportId : ""),
            text(row ? row->opening.evReportId : ""),
            text(row ? row->closing.evReportId : "")};
}
const std::vector<ReportColumn> columns = {{"범위 ID", 24},           {"범위", 26},
                                           {"기간 PV", 18},           {"기간 EV", 18},
                                           {"기간 AC", 18},           {"기간 CV", 18},
                                           {"기간 SV", 18},           {"기간 CPI", 14},
                                           {"기간 SPI", 14},          {"native 기간 PV", 18},
                                           {"native 기간 EV", 18},    {"날짜배분 AC", 18},
                                           {"보고증감-날짜배분", 24}, {"BAC", 18},
                                           {"native 마감 PV", 18},    {"native 마감 EV", 18},
                                           {"native 마감 AC", 18},    {"native 마감 EAC", 18},
                                           {"native 마감 ETC", 18},   {"지정 PV 작업", 16},
                                           {"지정 EV 작업", 16},      {"개시 AC 보고 ID", 28},
                                           {"마감 AC 보고 ID", 28},   {"개시 EV 보고 ID", 28},
                                           {"마감 EV 보고 ID", 28}};
} // namespace
std::vector<FinancialPeriodTask> financialPeriodSnapshot(const Project &p, const FinancialPeriodDefinition &d,
                                                         const std::string &formula) {
    const auto begin = d.fromDate + " 00:00", end = d.toDate + " 23:59";
    const bool openingObserved = p.statusAt && begin <= *p.statusAt;
    const bool closingObserved = p.statusAt && begin <= *p.statusAt;
    const auto closeAt = closingObserved ? std::min(end, *p.statusAt) : end;
    auto curves =
        nativeEvmCurves(p, d.baselineId, formula, {{begin, false, openingObserved}, {closeAt, true, closingObserved}});
    std::map<std::string, M> dated;
    if (p.statusAt)
        for (const auto &[key, e] : periodActualsAsOf(p, *p.statusAt)) {
            (void)key;
            if (e->actualMinor && d.fromDate <= e->date && e->date <= d.toDate) {
                auto [i, inserted] = dated.try_emplace(e->taskId, 0);
                (void)inserted;
                i->second = add(i->second, e->actualMinor);
            }
        }
    std::vector<FinancialPeriodTask> out;
    for (const auto &c : curves) {
        calculationCheckpoint();
        auto a = balance(c.points[0]), b = balance(c.points[1]);
        if (!openingObserved)
            a.ev = a.ac = {};
        if (!closingObserved)
            b.ev = b.ac = {};
        out.push_back({c.taskId, std::move(a), std::move(b),
                       closingObserved ? (dated.contains(c.taskId) ? dated.at(c.taskId) : M{0}) : M{}});
    }
    return out;
}
void appendFinancialPeriodHistory(ProjectReport &r, const Project &p) {
    if (!p.currencyConversions.empty()) {
        ReportTable table{"projectCurrencyHistory",
                          "프로젝트 전체 통화 전환 이력",
                          "금액은 현재 통화의 환산 투영입니다. 사용자 숫자/계산식 상수와 외부 원본 입력은 자동 "
                          "환산하지 않습니다. 기준일은 환율 근거이며 과거 원장을 포함합니다. 원래 "
                          "제출/승인 금액과 전후 전체 원본은 PPM 이력에서 조회합니다.",
                          ReportFamily::All,
                          {{"전환 ID", 28},
                           {"통화", 20},
                           {"환율 분자/분모", 24},
                           {"반올림", 28},
                           {"기준일 / 근거", 40},
                           {"작성자 / 사유", 40},
                           {"기록 UTC", 26},
                           {"원본 문서 / revision", 40}},
                          {}};
        for (const auto &e : p.currencyConversions) {
            calculationCheckpoint();
            table.rows.push_back({text(e.id), text(e.rate.fromCurrency + " → " + e.rate.targetCurrency),
                                  text(std::to_string(e.rate.numerator) + " / " + std::to_string(e.rate.denominator)),
                                  text(e.rate.rounding), text(e.rate.effectiveDate + " / " + e.rate.reference),
                                  text(e.reporter + " / " + e.reason), text(e.recordedAt),
                                  text(e.sourceDocumentId + " / " + std::to_string(e.recordedRevision))});
        }
        r.tables.push_back(std::move(table));
    }
    if (p.financialPeriodEvents.empty())
        return;
    ReportTable h{"financialPeriodHistory",
                  "사용자 기간·마감 이력",
                  "불변 기록 · 마감 당시 지식 revision · 재개방도 기존 마감을 삭제하지 않음",
                  ReportFamily::Costs,
                  {{"이력 ID", 28},
                   {"기간 ID / 이름", 36},
                   {"동작", 12},
                   {"기준선 ID", 28},
                   {"시작일", 14},
                   {"종료일", 14},
                   {"지식 revision", 18},
                   {"상태일", 22},
                   {"추정식", 24},
                   {"작성자 / 사유", 50},
                   {"기록 UTC", 26},
                   {"원본 문서 / revision", 48}},
                  {}};
    for (const auto &e : p.financialPeriodEvents) {
        calculationCheckpoint();
        h.rows.push_back({text(e.id),
                          text(e.period.id + " / " + e.period.name),
                          text(e.action),
                          text(e.period.baselineId),
                          text(e.period.fromDate),
                          text(e.period.toDate),
                          {std::int64_t(e.knowledgeRevision), CellFormat::Integer},
                          text(e.statusAt.value_or("")),
                          text(e.formula),
                          text(e.reporter + " / " + e.reason),
                          text(e.recordedAt),
                          text(e.sourceDocumentId + " / " + std::to_string(e.recordedRevision))});
    }
    r.tables.push_back(std::move(h));
}
ProjectReport buildFinancialPeriodReport(const Project &p, ReportOptions options, const std::string &id,
                                         const std::string &closeId) {
    EvaluationScope evaluation;
    validate(p);
    if (options.reportId.empty() || !validUtcStamp(options.generatedAt))
        throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
    const FinancialPeriodEvent *e = &financialPeriod(p, id);
    if (!closeId.empty()) {
        const auto it =
            std::find_if(p.financialPeriodEvents.begin(), p.financialPeriodEvents.end(),
                         [&](const auto &v) { return v.id == closeId && v.period.id == id && v.action == "close"; });
        if (it == p.financialPeriodEvents.end())
            throw AppError(ErrorCode::FINANCIAL_PERIOD_INVALID, {"마감 이력이 없습니다."});
        e = &*it;
    }
    const auto &d = e->period;
    const auto &b =
        *std::find_if(p.baselines.begin(), p.baselines.end(), [&](const auto &v) { return v.id == d.baselineId; });
    const bool closed = e->action == "close";
    const auto formula = closed ? e->formula : options.formula;
    const auto rows = closed ? e->tasks : financialPeriodSnapshot(p, d, formula);
    options.baselineId = d.baselineId;
    options.fromDate = d.fromDate;
    options.toDate = d.toDate;
    options.formula = formula;
    ProjectReport r;
    r.projectName = p.name;
    r.documentId = p.documentId;
    r.revision = p.revision;
    r.currency = e->currency;
    r.scopeName = d.name;
    r.statusAt = closed ? e->statusAt : p.statusAt;
    r.options = options;
    const std::string note = "사용자 지정 기간액 우선 / 빈칸은 native 누계 증감 / native 마감 누계·EAC·ETC는 별도 / "
                             "미보고는 미정 / 마감은 당시 값 고정";
    ReportTable summary{"financialPeriod", "사용자 기간 EVM", note, ReportFamily::Costs, columns, {}};
    ReportTable tasks{"financialPeriodTasks", "사용자 기간 작업", note, ReportFamily::Costs, columns, {}};
    ReportTable wbs{"financialPeriodWbs", "사용자 기간 기준선 WBS", note, ReportFamily::Costs, columns, {}};
    std::map<std::string, const WbsNode *> nodes;
    for (const auto &n : b.input.wbsNodes)
        nodes[n.id] = &n;
    if (!options.wbsId.empty() && !nodes.contains(options.wbsId))
        throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
    std::map<std::string, const Task *> taskIndex;
    for (const auto &t : b.input.tasks)
        taskIndex[t.id] = &t;
    std::map<std::string, const FinancialPeriodValue *> values;
    for (const auto &v : d.values)
        values[v.taskId] = &v;
    std::map<std::string, Total> groups;
    Total all;
    std::size_t cellsCount = 0;
    for (const auto &row : rows) {
        calculationCheckpoint();
        const auto &t = *taskIndex.at(row.taskId);
        std::vector<std::string> ancestors;
        for (auto w = t.wbsId; !w.empty(); w = nodes.at(w)->parentId) {
            calculationCheckpoint();
            ancestors.push_back(w);
        }
        if (!options.wbsId.empty() && std::find(ancestors.begin(), ancestors.end(), options.wbsId) == ancestors.end())
            continue;
        auto explicitValue = values.contains(t.id) ? *values.at(t.id) : FinancialPeriodValue{t.id, {}, {}};
        if (!closed && (!p.statusAt || *p.statusAt < d.toDate + " 23:59"))
            explicitValue.earned.reset();
        const auto v = total(row, &explicitValue);
        all.merge(v);
        for (const auto &w : ancestors)
            groups[w].merge(v);
        if ((cellsCount += columns.size()) > 900000)
            throw AppError(ErrorCode::REPORT_CELL_LIMIT);
        tasks.rows.push_back(cells(t.id, t.name, v, formula, &row));
    }
    if (cellsCount + (groups.size() + 1) * columns.size() > 1000000)
        throw AppError(ErrorCode::REPORT_CELL_LIMIT);
    summary.rows.push_back(cells(p.documentId, d.name, all, formula));
    for (const auto &n : b.input.wbsNodes)
        if (groups.contains(n.id))
            wbs.rows.push_back(cells(n.id, n.name, groups.at(n.id), formula));
    ReportTable context{"financialPeriodContext",
                        "사용자 기간 계산 근거",
                        "기간 금액은 해당 기간 성과이며 native 누계/기준선 BAC를 다시 쓰지 않음",
                        ReportFamily::Costs,
                        {{"항목", 28}, {"값", 100}},
                        {}};
    auto fact = [&](const std::string &k, const std::string &v) { context.rows.push_back({text(k), text(v)}); };
    fact("정책", "custom-financial-period-v1");
    fact("기간", d.id + " / " + d.name + " / " + d.fromDate + " ~ " + d.toDate);
    fact("상태", closed ? "마감 · 당시 지식 고정" : "열림 · 현재 지식으로 재계산");
    fact("마감 이력", closed ? e->id : "");
    fact("지식 revision", std::to_string(closed ? e->knowledgeRevision : p.revision));
    fact("원본 문서", closed ? e->sourceDocumentId : p.documentId);
    fact("현재 문서", p.documentId + " / rev " + std::to_string(p.revision));
    fact("기준선", b.id + " / " + b.name);
    fact("생성", options.generatedAt + " / " + options.reportId);
    fact("상태일", r.statusAt.value_or("미설정"));
    fact("통화 / 추정식", r.currency + " / " + formula);
    fact("지정/미정",
         "기간 PV·EV 지정은 기간 CV/SV/CPI/SPI에 사용. 빈칸은 native 기간값, 명시0은0, 미보고 누계는 미정.");
    fact("마감 경계",
         "시작일00:00 미만 / 종료일23:59 이하 / 열린 현재 기간은 상태시각 이하 / 재개방 후 재마감은 새 이력");
    r.tables = {std::move(summary), std::move(tasks), std::move(wbs), std::move(context)};
    r.costTrend.push_back({d.toDate, {}, {}, all.closing.pv, all.closing.ev, all.closing.ac, {}});
    appendFinancialPeriodHistory(r, p);
    return r;
}
} // namespace jobs
