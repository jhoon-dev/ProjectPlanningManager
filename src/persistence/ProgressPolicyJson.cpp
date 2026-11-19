#include "persistence/ProgressPolicyJson.h"
#include "formatting/PlanTokens.h"
#include "domain/Diagnostics.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
namespace ppm {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s);
}
QJsonValue text(const std::optional<std::string> &s) {
    return s ? QJsonValue(qs(*s)) : QJsonValue(QJsonValue::Null);
}
QJsonValue number(const std::optional<std::int64_t> &n) {
    return n ? QJsonValue(qint64(*n)) : QJsonValue(QJsonValue::Null);
}
QJsonArray strings(const std::vector<std::string> &v) {
    QJsonArray out;
    for (const auto &s : v)
        out.append(qs(s));
    return out;
}
QString state(CalculationStatus s) {
    return s == CalculationStatus::Valid       ? "valid"
           : s == CalculationStatus::Conflict  ? "conflict"
           : s == CalculationStatus::Cancelled ? "cancelled"
           : s == CalculationStatus::Stale     ? "stale"
                                               : "incomplete";
}
QJsonValue task(const ScheduleResult &result, const std::optional<TaskSchedule> &row) {
    if (!row)
        return QJsonValue(QJsonValue::Null);
    const bool known = hasScheduleFloat(result, *row);
    return QJsonObject{
        {"start", qs(row->start)},
        {"finish", qs(row->finish)},
        {"remainingStart", text(row->remainingStart)},
        {"completedActual", row->completedActual},
        {"totalFloatMinutes", known ? QJsonValue(qint64(row->totalFloatMinutes)) : QJsonValue(QJsonValue::Null)},
        {"freeFloatMinutes", known ? QJsonValue(qint64(row->freeFloatMinutes)) : QJsonValue(QJsonValue::Null)},
        {"critical", known ? QJsonValue(row->critical) : QJsonValue(QJsonValue::Null)},
        {"drivingPredecessors", strings(row->drivingPredecessors)}};
}
QJsonObject diagnosticJson(const Diagnostic &d) {
    calculationCheckpoint();
    QJsonArray causes;
    for (const auto &cause : d.causes)
        causes.append(diagnosticJson(cause));
    return {{"code", qs(d.code)},
            {"arguments", strings(d.arguments)},
            {"taskIds", strings(d.taskIds)},
            {"resourceIds", strings(d.resourceIds)},
            {"calendarIds", strings(d.calendarIds)},
            {"projectId", qs(d.projectId)},
            {"path", qs(d.path)},
            {"causes", causes}};
}
QJsonObject receipt(const ScheduleResult &r) {
    QJsonArray diagnostics;
    for (const auto &d : r.diagnostics) {
        calculationCheckpoint();
        diagnostics.append(diagnosticJson(d));
    }
    return {{"status", state(r.status)},
            {"finish", text(r.projectFinish)},
            {"engine", qs(r.engineVersion)},
            {"policy", qs(r.calculationPolicy)},
            {"diagnostics", diagnostics}};
}
} // namespace
QByteArray progressPolicyJson(const ProgressPolicyComparison &r, const QString &createdAt) {
    QJsonArray rows, ignored, historical, plan;
    for (const auto &t : r.tasks) {
        calculationCheckpoint();
        rows.append(QJsonObject{{"taskId", qs(t.id)},
                                {"name", qs(t.name)},
                                {"actualStart", text(t.actualStart)},
                                {"actualFinish", text(t.actualFinish)},
                                {"comparable", t.comparable},
                                {"changed", t.changed},
                                {"finishDeltaMinutes", number(t.finishDeltaMinutes)},
                                {"resumeDeltaMinutes", number(t.resumeDeltaMinutes)},
                                {"retained", task(r.retained, t.retained)},
                                {"overridden", task(r.overridden, t.overridden)},
                                {"actualDates", task(r.actualDates, t.actualDates)}});
    }
    for (const auto &d : r.ignored) {
        calculationCheckpoint();
        ignored.append(QJsonObject{{"predecessorId", qs(d.predecessorId)},
                                   {"taskId", qs(d.taskId)},
                                   {"type", qs(enumToken(d.type))},
                                   {"lagMinutes", qint64(d.lagMinutes)}});
    }
    for (const auto &d : r.actualDatesIgnored)
        historical.append(QJsonObject{{"predecessorId", qs(d.predecessorId)},
                                      {"taskId", qs(d.taskId)},
                                      {"type", qs(enumToken(d.type))},
                                      {"lagMinutes", qint64(d.lagMinutes)}});
    for (const auto &t : r.originalPlan.tasks)
        plan.append(QJsonObject{{"taskId", qs(t.taskId)}, {"start", qs(t.start)}, {"finish", qs(t.finish)}});
    QJsonObject json{{"contract", "ppm-progress-policy-comparison-v1"},
                     {"documentId", qs(r.documentId)},
                     {"name", qs(r.name)},
                     {"revision", QString::number(r.revision)},
                     {"statusAt", qs(r.statusAt)},
                     {"createdAt", createdAt},
                     {"computed", r.computed},
                     {"blockingReason", qs(r.blockingReason)},
                     {"changedTasks", qint64(r.changedTasks)},
                     {"uncomparedTasks", qint64(r.uncomparedTasks)},
                     {"deltaUnit", "project-calendar-work-minutes; B-minus-A"},
                     {"adopted", false},
                     {"P6RoundTrip", "not-run"},
                     {"originalPlan", plan},
                     {"retained", receipt(r.retained)},
                     {"overridden", receipt(r.overridden)},
                     {"actualDates", receipt(r.actualDates)},
                     {"tasks", rows},
                     {"ignoredRelationships", ignored},
                     {"actualDatesHistoricalRelationships", historical}};
    return QJsonDocument(json).toJson();
}
void saveProgressPolicyJson(const ProgressPolicyComparison &r, const QString &path) {
    const auto bytes = progressPolicyJson(r, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    calculationCheckpoint();
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("비교 JSON 파일을 기록할 수 없습니다.");
    calculationCheckpoint();
    if (!file.commit())
        throw std::runtime_error("비교 JSON 파일을 게시할 수 없습니다.");
}
} // namespace ppm
