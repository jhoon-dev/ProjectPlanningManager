#include "calculation/ScheduleFormulaScope.h"
#include "calculation/ResourceViewReport.h"
#include "calculation/FormulaFieldReports.h"
#include "domain/FormulaFields.h"
namespace ppm {
ProjectReport resourceViewReport(const Project &p, const ResourceViewSettings &settings, const ReportOptions &options) {
    EvaluationScope evaluation;
    ScheduleFormulaScope formulaSchedule(p, nullptr, ScheduleFormulaMode::Active);
    const auto rows = projectResources(p, settings);
    ProjectReport report;
    report.projectName = p.name;
    report.documentId = p.documentId;
    report.currency = p.currency;
    report.revision = p.revision;
    report.scopeName = "자원 그룹·필터 보기";
    report.options = options;
    report.statusAt = p.statusAt;
    ReportTable t;
    t.id = "resourceView";
    t.title = "자원 그룹과 필터";
    t.family = ReportFamily::Resources;
    t.note = "필터 결과의 고유 자원별 한 행 · 미설정은 미정 표시 · 숫자 0/빈 텍스트/미설정 구분 · 집계 없음";
    std::vector<std::string> keys{"id", "name", "type", "calendar", "capacity", "assigned"};
    const auto custom = attributeKeys(p, FieldScope::Resource);
    keys.insert(keys.end(), custom.begin(), custom.end());
    if (rows.size() * (keys.size() + settings.groupBy.size()) > 1000000)
        throw AppError(ErrorCode::REPORT_CELL_LIMIT);
    for (const auto &key : settings.groupBy)
        t.columns.push_back({"그룹 · " + resourceViewName(p, key), 22});
    for (const auto &key : keys)
        t.columns.push_back({resourceViewName(p, key), 22});
    std::set<std::string> included, assigned;
    for (const auto &a : p.assignments)
        assigned.insert(a.resourceId);
    for (const auto &row : rows) {
        calculationCheckpoint();
        std::vector<ReportCell> cells;
        for (const auto &g : row.groups)
            cells.push_back({g.label, CellFormat::Text});
        const auto &r = p.resources[row.resourceIndex];
        included.insert(r.id);
        const auto computed = formulaValues(p, FieldScope::Resource, nullptr, &r);
        for (const auto &key : keys) {
            const auto *f = customField(p, key, FieldScope::Resource);
            auto value = f && calculatedField(*f) ? formulaRawValue(computed->at(key))
                         : key == "assigned"      ? std::optional<std::string>{assigned.contains(r.id) ? "yes" : "no"}
                                                  : resourceViewValue(p, r, key);
            if (!value)
                cells.push_back({});
            else if (key == "capacity" || (f && f->type == CustomFieldType::Number))
                cells.push_back({parseFieldNumber(*value), CellFormat::FieldNumber});
            else
                cells.push_back({resourceViewText(p, key, *value), CellFormat::Text});
        }
        t.rows.push_back(std::move(cells));
    }
    report.tables.push_back(std::move(t));
    const auto groups = resourceRollUpGroups(rows);
    const RollUpValues rollups(p, FieldScope::Resource, groups);
    if (!rollups.fields().empty()) {
        ReportTable summary;
        summary.id = "resourceRollUp";
        summary.title = "자원 사용자 필드 요약";
        summary.family = ReportFamily::Resources;
        summary.note = "현재 필터의 고유 자원과 분류 계층으로 계산합니다. 공수·원가에 합산하지 않습니다.";
        summary.columns = {{"그룹 ID", 28}, {"필드", 24}, {"계산 방식", 24}, {"값", 24}, {"단위", 12}, {"상태", 32}};
        for (const auto &g : groups)
            for (const auto *f : rollups.fields()) {
                const auto key = "field:" + f->id;
                const auto &cell = rollups.cell(g.id, key);
                ReportCell value;
                if (cell.value)
                    value = f->type == CustomFieldType::Number
                                ? ReportCell{parseFieldNumber(*cell.value), CellFormat::FieldNumber}
                            : f->type == CustomFieldType::Cost
                                ? ReportCell{parseFieldCost(*cell.value, p.currency), CellFormat::Money}
                                : ReportCell{attributeText(p, key, *cell.value, FieldScope::Resource)};
                const auto m = f->xmlRollUp->rollupType;
                summary.rows.push_back(
                    {{g.id.empty() ? "프로젝트 전체" : g.id},
                     {f->name},
                     {rollUpMethodName(m)},
                     value,
                     {m == 2 || m == 6 || m == 7 ? "개" : attributeUnit(p, key, FieldScope::Resource)},
                     {cell.issue}});
            }
        report.tables.push_back(std::move(summary));
    }
    ReportTable definition;
    definition.id = "resourceViewSettings";
    definition.title = "자원 보기 조건";
    definition.family = ReportFamily::Resources;
    definition.columns = {{"항목", 22}, {"키", 26}, {"연산", 20}, {"값", 32}};
    auto add = [&](std::string a, std::string b, std::string c, std::string d) {
        definition.rows.push_back({{a}, {b}, {c}, {d}});
    };
    add("검색", "name/id", "contains (대소문자 구분)", settings.query);
    add("정렬", settings.sortKey, settings.descending ? "내림차순" : "오름차순", "미설정은 마지막 · 동률은 ID");
    for (const auto &g : settings.groupBy)
        add("그룹", g, "오름차순", resourceViewName(p, g));
    for (const auto &f : settings.filters)
        add("필터 (AND)", f.key, f.operation, f.value);
    report.tables.push_back(std::move(definition));
    const auto formulas = formulaFieldTables(p, {}, included, true);
    report.tables.insert(report.tables.end(), formulas.begin(), formulas.end());
    std::size_t cells = 0;
    for (const auto &table : report.tables)
        cells += table.rows.size() * table.columns.size();
    if (cells > 1000000)
        throw AppError(ErrorCode::REPORT_CELL_LIMIT);
    return report;
}
} // namespace ppm
