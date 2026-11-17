#include "reporting/IntegratedExport.h"
#include "domain/CalculationControl.h"
#include "domain/ResourcePlan.h"
#include "formatting/DiagnosticText.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <sstream>
namespace jobs {
namespace {
ReportCell text(const std::string &s) {
    return {s, CellFormat::Text};
}
ReportCell number(std::int64_t v) {
    return {v, CellFormat::Integer};
}
std::string refs(const std::vector<ProjectTaskRef> &v) {
    std::string out;
    for (const auto &r : v) {
        if (!out.empty())
            out += "; ";
        out += r.projectId + " / " + r.taskId;
    }
    return out;
}
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
QJsonArray references(const std::vector<ProjectTaskRef> &v) {
    QJsonArray out;
    for (const auto &r : v)
        out.append(QJsonObject{{"projectId", q(r.projectId)}, {"taskId", q(r.taskId)}});
    return out;
}
} // namespace
ProjectReport integratedWorkspaceReport(const Workspace &w, const IntegratedResult &result, std::uint64_t generation,
                                        const std::string &generatedAt) {
    if (result.status != CalculationStatus::Valid)
        throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
    ProjectReport out;
    out.projectName = "통합 일정";
    out.documentId = w.id;
    out.revision = generation;
    out.currency = "KRW";
    out.scopeName = "작업공간 전체 · 통화 합산 없음";
    out.scheduleStatus = result.status;
    out.options.reportId = "integrated-" + w.id;
    out.options.generatedAt = generatedAt;
    ReportTable tasks{"integratedTasks",
                      "프로젝트 간 링크·통합 일정",
                      "원본 revision의 읽기 전용 계산 · 원본 날짜를 채택하지 않음 · " + result.policy,
                      ReportFamily::Schedule,
                      {{"프로젝트 ID", 24},
                       {"프로젝트", 24},
                       {"작업 ID", 24},
                       {"작업", 28},
                       {"revision", 14},
                       {"로컬 시작", 24},
                       {"로컬 완료", 24},
                       {"통합 시작", 24},
                       {"통합 완료", 24},
                       {"잔여 시작", 24},
                       {"총 여유(분)", 18},
                       {"자유 여유(분)", 18},
                       {"임계", 14},
                       {"선행 작업 (프로젝트 / 작업)", 40}},
                      {}};
    for (const auto &t : result.tasks) {
        calculationCheckpoint();
        tasks.rows.push_back(
            {text(t.ref.projectId), text(t.projectName), text(t.ref.taskId), text(t.name),
             number(std::int64_t(t.revision)), t.originalStart ? text(*t.originalStart) : ReportCell{},
             t.originalFinish ? text(*t.originalFinish) : ReportCell{}, text(t.schedule.start), text(t.schedule.finish),
             t.schedule.remainingStart ? text(*t.schedule.remainingStart) : ReportCell{},
             t.floatAvailable ? number(t.schedule.totalFloatMinutes) : ReportCell{},
             t.floatAvailable ? number(t.schedule.freeFloatMinutes) : ReportCell{},
             t.floatAvailable ? text(t.schedule.critical ? "임계" : "비임계") : ReportCell{}, text(refs(t.driving))});
    }
    out.tables.push_back(std::move(tasks));
    ReportTable load{"integratedSharedLoad",
                     "통합 날짜의 공유 자원 부하",
                     "통합 결과의 실제 겹침. 가용량은 수요 구간의 설정값이며 여러 값이면 시간별로 표시합니다. 공유 "
                     "근무 달력도 과부하 계산에 적용합니다.",
                     ReportFamily::Resources,
                     {{"공유 자원 ID", 24},
                      {"날짜", 16},
                      {"최대 수요(%)", 18},
                      {"가용량(%)", 18},
                      {"과부하(분)", 18},
                      {"미정 가용(분)", 18},
                      {"작업 (프로젝트 / 작업)", 40}},
                     {}};
    for (const auto &d : result.shared.days) {
        calculationCheckpoint();
        load.rows.push_back({text(d.sharedId), text(d.date), number(d.peakUnits),
                             d.variableCapacity ? text("시간별")
                             : d.capacity       ? number(*d.capacity)
                                                : ReportCell{},
                             number(d.overloadMinutes), number(d.unknownCapacityMinutes), text(refs(d.tasks))});
    }
    out.tables.push_back(std::move(load));
    if (!result.materials.empty()) {
        ReportTable materials{"integratedMaterialStocks",
                              "통합 예측의 실제 자재 재고",
                              "프로젝트별 확인된 상태일 재고 · 명시 잔여 소모 · 실제 도착분을 제외한 미래 입고 · "
                              "자재는 프로젝트별로 유지",
                              ReportFamily::Resources,
                              {{"프로젝트 ID", 22},
                               {"자재 ID", 18},
                               {"자재", 20},
                               {"단위", 12},
                               {"상태일", 26},
                               {"revision", 14},
                               {"개시", 16},
                               {"실제 입고", 16},
                               {"실제 소모", 16},
                               {"확인 재고", 16},
                               {"잔여 소모", 16},
                               {"미입고 잔량", 18}},
                              {}};
        for (const auto &m : result.materials) {
            calculationCheckpoint();
            const auto quantity = [](std::int64_t n) { return text(formatMaterialQuantity(n)); };
            materials.rows.push_back({text(m.projectId), text(m.materialId), text(m.name), text(m.stock.unit),
                                      text(m.stock.at), number(std::int64_t(m.revision)),
                                      m.stock.opening ? quantity(*m.stock.opening) : ReportCell{},
                                      quantity(m.stock.received), quantity(m.stock.consumed),
                                      m.stock.remaining ? quantity(*m.stock.remaining) : ReportCell{},
                                      quantity(m.remainingUse), quantity(m.incoming)});
        }
        out.tables.push_back(std::move(materials));
    }
    ReportTable meta{"integratedMetadata",
                     "계산 출처·한계",
                     "기준 시각은 프로젝트의 ISO 로컬 시각, 원본 프로젝트 파일은 변경하지 않음",
                     ReportFamily::All,
                     {{"항목", 28}, {"값", 80}},
                     {}};
    meta.rows.push_back({text("작업공간 generation"), number(std::int64_t(generation))});
    meta.rows.push_back({text("계산 정책"), text(result.policy)});
    if (!result.floatPolicy.empty())
        meta.rows.push_back({text("여유 계산 정책"), text(result.floatPolicy)});
    meta.rows.push_back({text("모드"), text(result.forecast ? "현재 예측" : "계획")});
    meta.rows.push_back({text("프로젝트 간 링크 수"), number(std::int64_t(w.links.size()))});
    for (const auto &d : result.diagnostics)
        meta.rows.push_back({text("진단"), text(formatDiagnostic(d))});
    out.tables.push_back(std::move(meta));
    std::size_t cells = 0;
    for (const auto &t : out.tables)
        cells += t.columns.size() * t.rows.size();
    if (cells > 1000000)
        throw AppError(ErrorCode::REPORT_CELL_LIMIT);
    return out;
}
QByteArray integratedWorkspaceJson(const Workspace &w, const IntegratedResult &result, std::uint64_t generation) {
    if (result.status != CalculationStatus::Valid)
        throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
    QJsonArray tasks, days, links;
    for (const auto &t : result.tasks) {
        calculationCheckpoint();
        tasks.append(QJsonObject{
            {"projectId", q(t.ref.projectId)},
            {"projectName", q(t.projectName)},
            {"taskId", q(t.ref.taskId)},
            {"name", q(t.name)},
            {"revision", qint64(t.revision)},
            {"originalStart", t.originalStart ? QJsonValue(q(*t.originalStart)) : QJsonValue{}},
            {"originalFinish", t.originalFinish ? QJsonValue(q(*t.originalFinish)) : QJsonValue{}},
            {"start", q(t.schedule.start)},
            {"finish", q(t.schedule.finish)},
            {"remainingStart", t.schedule.remainingStart ? QJsonValue(q(*t.schedule.remainingStart)) : QJsonValue{}},
            {"completedActual", t.schedule.completedActual},
            {"totalFloatMinutes", !t.floatAvailable ? QJsonValue{} : QJsonValue(qint64(t.schedule.totalFloatMinutes))},
            {"freeFloatMinutes", !t.floatAvailable ? QJsonValue{} : QJsonValue(qint64(t.schedule.freeFloatMinutes))},
            {"critical", !t.floatAvailable ? QJsonValue{} : QJsonValue(t.schedule.critical)},
            {"driving", references(t.driving)}});
    }
    for (const auto &d : result.shared.days)
        days.append(QJsonObject{{"sharedId", q(d.sharedId)},
                                {"date", q(d.date)},
                                {"peakUnits", qint64(d.peakUnits)},
                                {"capacity", d.capacity ? QJsonValue(*d.capacity) : QJsonValue{}},
                                {"variableCapacity", d.variableCapacity},
                                {"overloadMinutes", qint64(d.overloadMinutes)},
                                {"unknownCapacityMinutes", qint64(d.unknownCapacityMinutes)},
                                {"tasks", references(d.tasks)}});
    for (const auto &l : w.links)
        links.append(QJsonObject{{"id", q(l.id)},
                                 {"predecessorProjectId", q(l.predecessorProjectId)},
                                 {"predecessorTaskId", q(l.predecessorTaskId)},
                                 {"successorProjectId", q(l.successorProjectId)},
                                 {"successorTaskId", q(l.successorTaskId)},
                                 {"type", l.type == DependencyType::FS   ? "FS"
                                          : l.type == DependencyType::SS ? "SS"
                                          : l.type == DependencyType::FF ? "FF"
                                                                         : "SF"},
                                 {"lagMinutes", qint64(l.lagMinutes)}});
    QJsonObject output{{"contract", "jobs-integrated-workspace-v1"},
                       {"workspaceId", q(w.id)},
                       {"generation", qint64(generation)},
                       {"policy", q(result.policy)},
                       {"forecast", result.forecast},
                       {"leveled", result.leveled},
                       {"finish", result.finish ? QJsonValue(q(*result.finish)) : QJsonValue{}},
                       {"links", links},
                       {"tasks", tasks},
                       {"sharedLoad", days}};
    if (!result.floatPolicy.empty())
        output["floatPolicy"] = q(result.floatPolicy);
    if (!result.materials.empty()) {
        QJsonArray materials;
        // Aggregated milli quantities can exceed JavaScript's exact integer range.
        for (const auto &m : result.materials) {
            calculationCheckpoint();
            materials.append(QJsonObject{
                {"projectId", q(m.projectId)},
                {"materialId", q(m.materialId)},
                {"name", q(m.name)},
                {"unit", q(m.stock.unit)},
                {"statusAt", q(m.stock.at)},
                {"revision", qint64(m.revision)},
                {"ledgerRows", qint64(m.ledgerRows)},
                {"confirmed", m.stock.confirmed},
                {"openingMilli", m.stock.opening ? QJsonValue(QString::number(*m.stock.opening)) : QJsonValue{}},
                {"receivedMilli", QString::number(m.stock.received)},
                {"consumedMilli", QString::number(m.stock.consumed)},
                {"onHandMilli", m.stock.remaining ? QJsonValue(QString::number(*m.stock.remaining)) : QJsonValue{}},
                {"remainingUseMilli", QString::number(m.remainingUse)},
                {"incomingMilli", QString::number(m.incoming)}});
        }
        output["materialStocks"] = materials;
    }
    return QJsonDocument(output).toJson();
}
} // namespace jobs
