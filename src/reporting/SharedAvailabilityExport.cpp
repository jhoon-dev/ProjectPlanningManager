#include "reporting/SharedAvailabilityExport.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
QJsonValue value(std::optional<std::int64_t> n) {
    return n ? QJsonValue(double(*n)) : QJsonValue(QJsonValue::Null);
}
ReportCell text(const std::string &s) {
    return {s, CellFormat::Text};
}
ReportCell units(std::optional<std::int64_t> n) {
    return n ? ReportCell{*n, CellFormat::Units} : ReportCell{};
}
std::map<std::string, std::string> names(const Workspace &w) {
    std::map<std::string, std::string> out;
    for (const auto &r : w.resources)
        out[r.id] = r.name;
    return out;
}
void validate(const Workspace &w, const SharedAvailabilityResult &r) {
    validateWorkspace(w);
    if (!r.configuration || *r.configuration != w || w.id != r.workspaceId || !validStamp(r.at) ||
        r.points.size() != w.resources.size())
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    const auto labels = names(w);
    std::set<std::string> seen;
    for (const auto &point : r.points)
        if (!labels.contains(point.value.resourceId) || point.value.at != r.at ||
            !seen.insert(point.value.resourceId).second)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
}
} // namespace
QByteArray sharedAvailabilityJson(const Workspace &w, const SharedAvailabilityResult &r, std::uint64_t generation) {
    validate(w, r);
    QJsonArray sources, points;
    auto labels = names(w);
    for (const auto &source : r.sources)
        sources.append(QJsonObject{
            {"projectId", q(source.projectId)},
            {"name", q(source.name)},
            {"available", source.available},
            {"revision", source.available ? QJsonValue(double(source.revision)) : QJsonValue(QJsonValue::Null)},
            {"status", int(source.status)},
            {"forecast", source.available ? QJsonValue(source.forecast) : QJsonValue(QJsonValue::Null)},
            {"statusAt", source.statusAt ? QJsonValue(q(*source.statusAt)) : QJsonValue(QJsonValue::Null)},
            {"issue", q(source.issue)}});
    for (const auto &point : r.points) {
        calculationCheckpoint();
        QJsonArray contributions;
        for (const auto &c : point.contributions)
            contributions.append(
                QJsonObject{{"projectId", q(c.projectId)},
                            {"revision", c.revision ? QJsonValue(double(*c.revision)) : QJsonValue(QJsonValue::Null)},
                            {"demand", value(c.demand)},
                            {"knownDemand", double(c.knownDemand)},
                            {"issue", q(c.issue)}});
        const auto &v = point.value;
        points.append(QJsonObject{{"sharedId", q(v.resourceId)},
                                  {"name", q(labels.at(v.resourceId))},
                                  {"capacity", value(v.capacity)},
                                  {"demand", value(v.demand)},
                                  {"knownDemand", double(point.knownDemand)},
                                  {"remaining", value(v.remaining)},
                                  {"freeUnits", value(v.freeUnits)},
                                  {"shortage", value(v.shortage)},
                                  {"working", v.working},
                                  {"issue", q(v.issue)},
                                  {"contributions", contributions}});
    }
    return QJsonDocument(QJsonObject{{"contract", "ppm-shared-remaining-availability-v1"},
                                     {"workspaceId", q(w.id)},
                                     {"generation", double(generation)},
                                     {"at", q(r.at)},
                                     {"forecast", r.forecast},
                                     {"status", int(r.status)},
                                     {"unit", "100=one-resource"},
                                     {"occupancyCount", double(r.occupancyCount)},
                                     {"unmappedResources", double(r.unmappedResources)},
                                     {"sources", sources},
                                     {"points", points}})
        .toJson();
}
ProjectReport sharedAvailabilityReport(const Workspace &w, const SharedAvailabilityResult &r, std::uint64_t generation,
                                       const std::string &generatedAt) {
    validate(w, r);
    if (!validUtcStamp(generatedAt))
        throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
    ProjectReport out;
    out.documentId = w.id;
    out.projectName = "공유 자원 잔여 가용량";
    out.revision = generation;
    out.currency = "KRW";
    out.scopeName = "작업공간 공유 연결 · 독립 프로젝트 일정의 동시 수요";
    out.scheduleStatus = r.status;
    out.options.reportId = "shared-remaining-" + w.id;
    out.options.generatedAt = generatedAt;
    ReportTable points{"sharedRemainingAvailability",
                       "시각별 공유 잔여 가용량",
                       "공유 풀의 근무 달력/가용량을 한 번만 적용합니다. 누락/상태일 이전 수요가 있으면 전체 수요와 "
                       "잔여는 미정이며 확인된 수요는 하한입니다.",
                       ReportFamily::Resources,
                       {{"공유 ID", 20},
                        {"공유 자원", 23},
                        {"조회 시각", 23},
                        {"유효 가용률", 14},
                        {"전체 수요", 14},
                        {"확인된 수요", 15},
                        {"잔여", 14},
                        {"배정 가능", 14},
                        {"부족", 14},
                        {"상태", 30}},
                       {}};
    auto labels = names(w);
    ReportTable detail{
        "sharedRemainingContributions",
        "프로젝트별 수요 기여",
        "프로젝트 ID/revision으로 구분합니다. 로컬 자원 이름을 합산 키로 사용하지 않습니다.",
        ReportFamily::Resources,
        {{"공유 ID", 20}, {"프로젝트 ID", 22}, {"revision", 12}, {"전체 기여", 14}, {"확인된 기여", 15}, {"상태", 35}},
        {}};
    for (const auto &point : r.points) {
        calculationCheckpoint();
        const auto &v = point.value;
        points.rows.push_back({text(v.resourceId), text(labels.at(v.resourceId)), text(r.at), units(v.capacity),
                               units(v.demand), units(point.knownDemand), units(v.remaining), units(v.freeUnits),
                               units(v.shortage), text(v.issue)});
        for (const auto &c : point.contributions)
            detail.rows.push_back(
                {text(v.resourceId), text(c.projectId),
                 c.revision ? ReportCell{std::int64_t(*c.revision), CellFormat::Integer} : ReportCell{},
                 units(c.demand), units(c.knownDemand), text(c.issue)});
    }
    ReportTable sources{
        "sharedRemainingSources",
        "입력 프로젝트와 계산 기준",
        "입력 snapshot의 revision과 각 파일의 실제 계획/예측 기준입니다.",
        ReportFamily::Resources,
        {{"프로젝트 ID", 22}, {"프로젝트", 24}, {"revision", 12}, {"수요 기준", 15}, {"상태일", 24}, {"상태", 35}},
        {}};
    for (const auto &source : r.sources)
        sources.rows.push_back(
            {text(source.projectId), text(source.name),
             source.available ? ReportCell{std::int64_t(source.revision), CellFormat::Integer} : ReportCell{},
             text(!source.available ? "미정"
                  : source.forecast ? "잔여 예측"
                                    : "계획"),
             text(source.statusAt.value_or("")), text(source.issue)});
    out.tables = {std::move(points), std::move(detail), std::move(sources)};
    return out;
}
} // namespace ppm
