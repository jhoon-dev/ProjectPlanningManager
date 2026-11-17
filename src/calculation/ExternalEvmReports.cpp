#include "calculation/ExternalEvmReports.h"
#include "domain/ExternalEvm.h"
#include <algorithm>
#include <set>
namespace jobs {
namespace {
ReportCell text(const std::string &s) {
    return {s, CellFormat::Text};
}
ReportCell money(std::optional<std::int64_t> n) {
    return {n ? decltype(ReportCell::value){*n} : decltype(ReportCell::value){}, CellFormat::Money};
}
} // namespace
void appendExternalEvmTables(ProjectReport &report, const Project &p,
                             const std::optional<std::vector<std::string>> &scope) {
    std::set<std::string> selected;
    if (scope)
        selected.insert(scope->begin(), scope->end());
    auto includes = [&](const std::string &id) { return !scope || selected.contains(id); };
    std::map<std::string, const ExternalEvmAdoption *> active;
    ReportTable history{"externalEvmHistory",
                        "외부 XER EV PV 채택 이력",
                        "기준선별 불변 이력 · 활성 채택만 native 값을 대체 · 철회는 native 측정법 복원",
                        ReportFamily::Costs,
                        {{"채택 ID", 28},
                         {"기준선 ID", 28},
                         {"상태/지표", 22},
                         {"선택 작업 수", 16},
                         {"기간 수", 14},
                         {"원본 파일/프로젝트", 34},
                         {"파일 SHA256", 38},
                         {"집계 revision/투영 SHA256", 38},
                         {"원본/native 상태일", 28},
                         {"작성자/사유", 38},
                         {"기록 UTC", 26},
                         {"원본 문서/revision", 32}},
                        {}};
    for (const auto &a : p.externalEvmAdoptions) {
        calculationCheckpoint();
        if (a.baselineId != report.options.baselineId)
            continue;
        const auto count = std::count_if(a.taskIds.begin(), a.taskIds.end(), includes);
        if (!count)
            continue;
        const auto *source = externalEvmSource(p, a.financialRevisionId);
        for (const auto &id : a.taskIds)
            if (includes(id))
                active[id] = &a;
        history.rows.push_back(
            {text(a.id),
             text(a.baselineId),
             text(a.withdrawn ? "철회" : std::string("채택 ") + (a.useEv ? "EV " : "") + (a.usePv ? "PV" : "")),
             {std::int64_t(count), CellFormat::Integer},
             {std::int64_t(a.rows.size()), CellFormat::Integer},
             text(source->sourceName + " / " + source->projectId),
             text(a.sourceSha256),
             text(a.financialRevisionId + "\n" + a.sourceProjectionSha256),
             text(a.sourceStatusAt + "\n" + a.nativeStatusAt.value_or("미설정")),
             text(a.reporter + "\n" + a.reason),
             text(a.recordedAt),
             text(a.sourceDocumentId + " / " + std::to_string(a.recordedRevision))});
    }
    if (history.rows.empty())
        return;
    ReportTable periods{"externalEvmPeriods",
                        "채택 XER EV PV 기간",
                        "활성 채택 원본 기간 · 마감=명시 개시+기간 금액 · 내부/공백/범위 밖 미정 · AC/물리 진척 불변",
                        ReportFamily::Costs,
                        {{"채택 ID", 28},
                         {"작업 ID", 26},
                         {"기간 참조", 14},
                         {"시작", 24},
                         {"마감", 24},
                         {"개시 EV", 18},
                         {"기간 EV", 18},
                         {"마감 EV", 18},
                         {"개시 PV", 18},
                         {"기간 PV", 18},
                         {"마감 PV", 18}},
                        {}};
    auto end = [](auto a, auto b) -> Money { return a && b ? Money{*a + *b} : Money{}; };
    for (const auto &a : p.externalEvmAdoptions) {
        if (a.baselineId != report.options.baselineId || a.withdrawn)
            continue;
        const auto *source = externalEvmSource(p, a.financialRevisionId);
        for (const auto &r : a.rows) {
            calculationCheckpoint();
            const auto &row = source->periods[r.periodIndex];
            if (!includes(row.taskId) || active.at(row.taskId) != &a ||
                (report.options.fromDate && row.finish.substr(0, 10) < *report.options.fromDate) ||
                (report.options.toDate && row.start.substr(0, 10) > *report.options.toDate))
                continue;
            const auto [from, to] = externalEvmBounds(*source, row);
            periods.rows.push_back({text(a.id),
                                    text(row.taskId),
                                    {std::int64_t(r.periodIndex), CellFormat::Integer},
                                    text(from + " 미만"),
                                    text(to + " 이하"),
                                    money(a.useEv ? r.openingEv : Money{}),
                                    money(a.useEv ? row.bcwp : Money{}),
                                    money(a.useEv ? end(r.openingEv, row.bcwp) : Money{}),
                                    money(a.usePv ? r.openingPv : Money{}),
                                    money(a.usePv ? row.bcws : Money{}),
                                    money(a.usePv ? end(r.openingPv, row.bcws) : Money{})});
        }
    }
    report.tables.push_back(std::move(history));
    report.tables.push_back(std::move(periods));
    std::size_t cells = 0;
    for (const auto &t : report.tables) {
        cells += t.columns.size() * t.rows.size();
        if (cells > 1000000)
            throw AppError(ErrorCode::REPORT_CELL_LIMIT);
    }
}
} // namespace jobs
