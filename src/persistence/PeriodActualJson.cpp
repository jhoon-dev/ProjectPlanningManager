#include "persistence/PeriodActualJson.h"
#include "domain/Project.h"
#include "domain/CalculationControl.h"
#include <QJsonObject>
#include <cmath>
namespace ppm::json {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s);
}
std::string string(const QJsonObject &o, const QString &key) {
    if (!o[key].isString())
        throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
    return o[key].toString().toStdString();
}
std::int64_t integer(const QJsonValue &v, std::int64_t limit) {
    const auto n = v.toDouble(-1);
    if (!v.isDouble() || !std::isfinite(n) || n < 0 || n > double(limit) || n != std::floor(n))
        throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
    return std::int64_t(n);
}
QJsonValue number(const std::optional<std::int64_t> &n) {
    return n ? QJsonValue(qint64(*n)) : QJsonValue(QJsonValue::Null);
}
} // namespace
QJsonArray encodePeriodActuals(const std::vector<PeriodActualEntry> &entries) {
    QJsonArray out;
    for (const auto &e : entries) {
        calculationCheckpoint();
        out.append(QJsonObject{{"id", qs(e.id)},
                               {"taskId", qs(e.taskId)},
                               {"resourceId", qs(e.resourceId)},
                               {"resourceType", qs(e.resourceType)},
                               {"date", qs(e.date)},
                               {"statusAt", qs(e.statusAt)},
                               {"recordedAt", qs(e.recordedAt)},
                               {"reporter", qs(e.reporter)},
                               {"reason", qs(e.reason)},
                               {"previousEntryId", qs(e.previousEntryId)},
                               {"sourceDocumentId", qs(e.sourceDocumentId)},
                               {"currency", qs(e.currency)},
                               {"costEntryId", qs(e.costEntryId)},
                               {"recordedRevision", qint64(e.recordedRevision)},
                               {"workUnitMinutes", number(e.workUnitMinutes)},
                               {"actualMinor", number(e.actualMinor)}});
    }
    return out;
}
std::vector<PeriodActualEntry> decodePeriodActuals(const QJsonValue &value) {
    if (!value.isArray() || value.toArray().size() > 50000)
        throw AppError(ErrorCode::PERIOD_ACTUAL_LIMIT);
    const QStringList fields{"id",          "taskId",           "resourceId",       "resourceType",
                             "date",        "statusAt",         "recordedAt",       "reporter",
                             "reason",      "previousEntryId",  "sourceDocumentId", "currency",
                             "costEntryId", "recordedRevision", "workUnitMinutes",  "actualMinor"};
    std::vector<PeriodActualEntry> out;
    for (const auto &v : value.toArray()) {
        calculationCheckpoint();
        if (!v.isObject())
            throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
        const auto o = v.toObject();
        if (o.size() != fields.size())
            throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
        for (const auto &f : fields)
            if (!o.contains(f))
                throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
        PeriodActualEntry e;
        e.id = string(o, "id");
        e.taskId = string(o, "taskId");
        e.resourceId = string(o, "resourceId");
        e.resourceType = string(o, "resourceType");
        e.date = string(o, "date");
        e.statusAt = string(o, "statusAt");
        e.recordedAt = string(o, "recordedAt");
        e.reporter = string(o, "reporter");
        e.reason = string(o, "reason");
        e.previousEntryId = string(o, "previousEntryId");
        e.sourceDocumentId = string(o, "sourceDocumentId");
        e.currency = string(o, "currency");
        e.costEntryId = string(o, "costEntryId");
        e.recordedRevision = std::uint64_t(integer(o["recordedRevision"], 9007199254740991LL));
        if (!o["workUnitMinutes"].isNull())
            e.workUnitMinutes = integer(o["workUnitMinutes"], maxWork);
        if (!o["actualMinor"].isNull())
            e.actualMinor = integer(o["actualMinor"], maxMoney);
        out.push_back(std::move(e));
    }
    return out;
}
} // namespace ppm::json
