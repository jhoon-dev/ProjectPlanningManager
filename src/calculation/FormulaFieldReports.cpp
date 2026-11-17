#include "calculation/ScheduleFormulaScope.h"
#include "domain/XmlFormula.h"
#include "calculation/FormulaFieldReports.h"
#include "domain/FormulaFields.h"
namespace jobs {
std::vector<ReportTable> formulaFieldTables(const Project &p, const std::set<std::string> &tasks,
                                            const std::optional<std::set<std::string>> &resources,
                                            bool includeProject) {
    if (!hasFormulaFields(p) && !hasXmlFormulas(p))
        return {};
    EvaluationScope scope;
    ScheduleFormulaScope formulaSchedule(p);
    ReportTable definitions;
    definitions.id = "formulaFields";
    definitions.title = "계산식 필드 정의";
    definitions.family = ReportFamily::All;
    definitions.note =
        "현재 사전 정의 · 결과는 소수 6자리 고정 정밀도 · 단위 태그 일치 · 계산 결과를 일정/원가에 합산하지 않음";
    definitions.columns = {{"범위", 16}, {"필드 키", 26}, {"필드", 22}, {"단위", 14}, {"계산식", 60}};
    ReportTable issues;
    issues.id = "formulaIssues";
    issues.title = "계산식 미설정과 오류";
    issues.family = ReportFamily::All;
    issues.note = "결과가 없는 값을 0으로 대체하지 않습니다. 미설정과 계산 오류를 구분합니다.";
    issues.columns = {{"범위", 16}, {"객체 ID", 26}, {"객체", 24}, {"필드 키", 26}, {"상태", 16}, {"내용", 48}};
    auto text = [](const std::string &v) { return ReportCell{v, CellFormat::Text}; };
    auto caption = [](FieldScope s) {
        return s == FieldScope::Task         ? "작업"
               : s == FieldScope::Project    ? "프로젝트"
               : s == FieldScope::Assignment ? "배정"
                                             : "자원";
    };
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, s))
            if (!f.expression.empty() || f.xmlFormula)
                definitions.rows.push_back(
                    {text(caption(s)), text("field:" + f.id), text(f.name), text(f.unit),
                     text(f.xmlFormula
                              ? (f.xmlFormula->enabled ? "[XML 활성] " : "[XML 비활성] ") + f.xmlFormula->source
                              : f.expression)});
    auto append = [&](FieldScope s, const std::string &id, const std::string &name, const Task *task,
                      const Resource *resource, const Assignment *assignment = nullptr) {
        const auto values = formulaValues(p, s, task, resource, assignment);
        for (const auto &[key, v] : *values)
            if (!formulaRawValue(v)) {
                calculationCheckpoint();
                if (issues.rows.size() >= 1000000 / 6)
                    throw AppError(ErrorCode::REPORT_CELL_LIMIT);
                issues.rows.push_back({text(caption(s)), text(id), text(name), text(key),
                                       text(v.missing ? "미설정" : "계산 오류"), text(v.issue)});
            }
    };
    for (const auto &t : p.tasks)
        if (tasks.contains(t.id))
            append(FieldScope::Task, t.id, t.name, &t, nullptr);
    if (includeProject)
        append(FieldScope::Project, p.documentId, p.name, nullptr, nullptr);
    for (const auto &r : p.resources)
        if (!resources || resources->contains(r.id))
            append(FieldScope::Resource, r.id, r.name, nullptr, &r);
    for (const auto &a : p.assignments)
        if (tasks.contains(a.taskId))
            append(FieldScope::Assignment, assignmentAttributeId(a), a.taskId + " / " + a.resourceId, nullptr, nullptr,
                   &a);
    return {std::move(definitions), std::move(issues)};
}
} // namespace jobs
