#include "persistence/ActivityJson.h"
#include "domain/Project.h"
#include "domain/CalculationControl.h"
#include <QJsonObject>
#include <cmath>
namespace jobs {
namespace {
[[noreturn]] void invalid() {
    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활동 확장 JSON 형식·필드를 확인하세요."});
}
QString qs(const std::string &s) {
    return QString::fromUtf8(s);
}
QJsonObject object(const QJsonValue &v, const QStringList &keys) {
    if (!v.isObject())
        invalid();
    const auto o = v.toObject();
    if (o.size() != keys.size())
        invalid();
    for (const auto &key : keys)
        if (!o.contains(key))
            invalid();
    return o;
}
QJsonArray array(const QJsonValue &v, int limit) {
    if (!v.isArray() || v.toArray().size() > limit)
        invalid();
    return v.toArray();
}
std::string text(const QJsonObject &o, const char *key) {
    if (!o[key].isString())
        invalid();
    return o[key].toString().toUtf8().toStdString();
}
std::int64_t number(const QJsonObject &o, const char *key, std::int64_t max) {
    const auto v = o[key];
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < 0 || v.toDouble() > double(max) ||
        std::floor(v.toDouble()) != v.toDouble())
        invalid();
    return std::int64_t(v.toDouble());
}
QJsonObject step(const ActivityStep &s) {
    return {{"id", qs(s.id)}, {"name", qs(s.name)}, {"weight", s.weight}};
}
ActivityStep readStep(const QJsonValue &v) {
    const auto o = object(v, {"id", "name", "weight"});
    return {text(o, "id"), text(o, "name"), int(number(o, "weight", 1000000))};
}
} // namespace
QJsonArray encodeActivities(const std::vector<ActivityDefinition> &definitions) {
    QJsonArray rows;
    for (const auto &a : definitions) {
        QJsonArray steps;
        for (const auto &s : a.steps)
            steps.append(step(s));
        rows.append(QJsonObject{{"id", qs(a.id)},
                                {"name", qs(a.name)},
                                {"kind", qs(a.kind)},
                                {"taskId", qs(a.taskId)},
                                {"startTaskId", qs(a.startTaskId)},
                                {"finishTaskId", qs(a.finishTaskId)},
                                {"wbsId", qs(a.wbsId)},
                                {"calendarId", qs(a.calendarId)},
                                {"physicalFromSteps", a.physicalFromSteps},
                                {"steps", steps}});
    }
    return rows;
}
std::vector<ActivityDefinition> decodeActivities(const QJsonValue &v) {
    std::vector<ActivityDefinition> result;
    for (const auto &row : array(v, 10000)) {
        calculationCheckpoint();
        const auto o = object(row, {"id", "name", "kind", "taskId", "startTaskId", "finishTaskId", "wbsId",
                                    "calendarId", "physicalFromSteps", "steps"});
        ActivityDefinition a;
        a.id = text(o, "id");
        a.name = text(o, "name");
        a.kind = text(o, "kind");
        a.taskId = text(o, "taskId");
        a.startTaskId = text(o, "startTaskId");
        a.finishTaskId = text(o, "finishTaskId");
        a.wbsId = text(o, "wbsId");
        a.calendarId = text(o, "calendarId");
        if (!o["physicalFromSteps"].isBool())
            invalid();
        a.physicalFromSteps = o["physicalFromSteps"].toBool();
        for (const auto &s : array(o["steps"], 200))
            a.steps.push_back(readStep(s));
        result.push_back(std::move(a));
    }
    return result;
}
QJsonArray encodeStepReports(const std::vector<StepReport> &reports) {
    QJsonArray rows;
    for (const auto &r : reports) {
        QJsonArray observations;
        for (const auto &s : r.steps)
            observations.append(QJsonObject{{"step", step(s.step)}, {"basisPoints", s.basisPoints}});
        rows.append(QJsonObject{{"id", qs(r.id)},
                                {"taskId", qs(r.taskId)},
                                {"statusAt", qs(r.statusAt)},
                                {"recordedAt", qs(r.recordedAt)},
                                {"reporter", qs(r.reporter)},
                                {"reason", qs(r.reason)},
                                {"sourceDocumentId", qs(r.sourceDocumentId)},
                                {"recordedRevision", double(r.recordedRevision)},
                                {"steps", observations}});
    }
    return rows;
}
std::vector<StepReport> decodeStepReports(const QJsonValue &v) {
    std::vector<StepReport> result;
    for (const auto &row : array(v, 10000)) {
        calculationCheckpoint();
        const auto o = object(row, {"id", "taskId", "statusAt", "recordedAt", "reporter", "reason", "sourceDocumentId",
                                    "recordedRevision", "steps"});
        StepReport r;
        r.id = text(o, "id");
        r.taskId = text(o, "taskId");
        r.statusAt = text(o, "statusAt");
        r.recordedAt = text(o, "recordedAt");
        r.reporter = text(o, "reporter");
        r.reason = text(o, "reason");
        r.sourceDocumentId = text(o, "sourceDocumentId");
        r.recordedRevision = std::uint64_t(number(o, "recordedRevision", 9007199254740991LL));
        for (const auto &s : array(o["steps"], 200)) {
            const auto entry = object(s, {"step", "basisPoints"});
            r.steps.push_back({readStep(entry["step"]), int(number(entry, "basisPoints", 10000))});
        }
        result.push_back(std::move(r));
    }
    return result;
}
} // namespace jobs
