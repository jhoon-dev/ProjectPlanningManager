#include "calculation/PeriodActualReports.h"
#include "domain/PeriodActuals.h"
#include "domain/ActualProvenance.h"
#include <unordered_map>
namespace ppm {
namespace {
ReportCell text(const std::string &s) {
    return {s, CellFormat::Text};
}
ReportCell number(std::optional<std::int64_t> n, CellFormat f) {
    return {n ? decltype(ReportCell::value){*n} : decltype(ReportCell::value){}, f};
}
} // namespace
std::vector<ReportTable> periodActualTables(const Project &p, const std::set<std::string> &ids,
                                            const std::optional<std::string> &from,
                                            const std::optional<std::string> &to) {
    if (p.periodActuals.empty())
        return {};
    std::unordered_map<std::string, std::string> taskNames, resourceNames;
    for (const auto &t : p.tasks)
        taskNames[t.id] = t.name;
    for (const auto &r : p.resources)
        resourceNames[r.id] = r.name;
    std::size_t cells = 0;
    const auto append = [&](ReportTable &t, std::vector<ReportCell> row) {
        calculationCheckpoint();
        cells += row.size();
        if (cells > 1000000)
            throw AppError(ErrorCode::REPORT_CELL_LIMIT);
        t.rows.push_back(std::move(row));
    };
    ReportTable detail;
    detail.id = "periodActuals";
    detail.title = "기간별 보고 실제";
    detail.note = "조회 상태일의 최신 보고 · 미보고와 0 구분 · 공수는 관측 정수 %·분 · 원가는 누계에 포함된 배분액 · "
                  "합계는 보고분만";
    detail.columns = {{"날짜", 14},        {"작업 ID", 20}, {"작업", 24},        {"자원 ID", 20},
                      {"자원/직접비", 24}, {"단위", 12},    {"보고 공수", 16},   {"배분 실제원가", 18},
                      {"보고 ID", 24},     {"상태일", 24},  {"누계 보고 ID", 24}};
    if (p.statusAt)
        for (const auto &[key, e] : periodActualsAsOf(p, *p.statusAt)) {
            (void)key;
            if (!ids.contains(e->taskId) || (from && e->date < *from) || (to && e->date > *to))
                continue;
            append(detail,
                   {text(e->date), text(e->taskId), text(taskNames.at(e->taskId)), text(e->resourceId),
                    text(e->resourceId.empty() ? "작업 직접비" : resourceNames.at(e->resourceId)),
                    text(e->resourceId.empty()        ? "—"
                         : e->resourceType == "labor" ? "인시"
                                                      : "장비시"),
                    number(e->workUnitMinutes, CellFormat::WorkHours), number(e->actualMinor, CellFormat::Money),
                    text(e->id), text(e->statusAt), text(e->costEntryId)});
        }
    ReportTable balance;
    const auto origins = actualDerivations(p);
    ReportTable estimated;
    estimated.id = "periodActualAllocation";
    estimated.title = "비례 배분 실적 근거";
    estimated.note = "현재 선택된 비례 추정 보고입니다. 측정값이 아닙니다. 사유에 원본 해시/구간·기준·날짜별 "
                     "가중분/합계와 정수 배분을 보존합니다. 공수/원가 중 보존된 척도는 추정으로 바꾸지 않습니다.";
    estimated.columns = {{"날짜", 14},           {"작업 ID", 20},        {"자원 ID", 20},       {"공수 근거", 14},
                         {"원가 근거", 14},      {"공수 u", 18},         {"원가 최소단위", 18}, {"보고 ID", 26},
                         {"공수 배분 보고", 26}, {"원가 배분 보고", 26}, {"현재 사유", 100}};
    if (p.statusAt)
        for (const auto &[key, e] : periodActualsAsOf(p, *p.statusAt)) {
            (void)key;
            const auto &d = origins.at(e->id);
            if (!d.any() || !ids.contains(e->taskId) || (from && e->date < *from) || (to && e->date > *to))
                continue;
            append(estimated,
                   {text(e->date), text(e->taskId), text(e->resourceId),
                    text(d.workSourceId.empty() ? "보고/보존" : "비례 추정 기원"),
                    text(d.costSourceId.empty() ? "보고/보존" : "비례 추정 기원"),
                    number(e->workUnitMinutes, CellFormat::Integer), number(e->actualMinor, CellFormat::Integer),
                    text(e->id), text(d.workSourceId), text(d.costSourceId), text(e->reason)});
        }
    if (!estimated.rows.empty()) {
        detail.note = "조회 상태일의 최신 보고 · 미보고와0 구분 · 공수 정수 %·분 · 원가는 누계에 포함된 배분액 · 비례 "
                      "추정 기원 포함: '비례 배분 실적 근거'에서 척도별 배분 보고를 확인하세요.";
        estimated.note += " 같은 수치를 이어받은 정정에도 배분 기원을 유지합니다. 배분 보고 ID의 정정 이력에서 원래 "
                          "가중치/수량을 확인하세요.";
    }
    balance.id = "periodActualBalance";
    balance.title = "누적 원가 배분 대사";
    balance.note =
        "상태일 기준 전기간 대사 · 누적 AC = 전기간 알려진 배분 + 미배분 · 표시 기간 보고액을 다시 더하지 않음";
    balance.columns = {{"작업 ID", 20},     {"작업", 26},   {"상태일", 24},        {"누적 AC", 20},
                       {"전기간 배분", 20}, {"미배분", 20}, {"배분 보고 건수", 18}};
    if (p.statusAt)
        for (const auto &[id, a] : actualCostAllocations(p, *p.statusAt))
            if (ids.contains(id))
                append(balance,
                       {text(id), text(taskNames.at(id)), text(*p.statusAt), number(a.cumulative, CellFormat::Money),
                        number(a.allocated, CellFormat::Money), number(a.unallocated, CellFormat::Money),
                        number(std::int64_t(a.costReports), CellFormat::Integer)});
    ReportTable history;
    history.id = "periodActualHistory";
    history.title = "기간 실적 정정 이력";
    history.note = "선택 작업의 전체 제출 원장 · 날짜 필터/조회 상태일 밖 기록도 상태일과 함께 보존 · 정정은 원본을 "
                   "대체 표시하며 원장을 삭제하지 않음";
    history.columns = {{"보고 ID", 24},   {"이전 보고 ID", 24}, {"작업 ID", 20},       {"자원 ID", 20},
                       {"종류", 12},      {"실제 날짜", 14},    {"보고 상태일", 24},   {"공수", 16},
                       {"배분 원가", 18}, {"통화", 10},         {"작성자", 20},        {"사유", 32},
                       {"기록 UTC", 24},  {"원본 문서", 24},    {"원본 revision", 18}, {"누계 보고 ID", 24}};
    for (const auto &e : p.periodActuals)
        if (ids.contains(e.taskId))
            append(history,
                   {text(e.id), text(e.previousEntryId), text(e.taskId), text(e.resourceId), text(e.resourceType),
                    text(e.date), text(e.statusAt), number(e.workUnitMinutes, CellFormat::WorkHours),
                    number(e.actualMinor, CellFormat::Money), text(e.currency), text(e.reporter), text(e.reason),
                    text(e.recordedAt), text(e.sourceDocumentId),
                    number(std::int64_t(e.recordedRevision), CellFormat::Integer), text(e.costEntryId)});
    std::vector<ReportTable> result{std::move(detail), std::move(balance), std::move(history)};
    if (!estimated.rows.empty())
        result.push_back(std::move(estimated));
    return result;
}
} // namespace ppm
