#include "domain/CalculationControl.h"
#include "persistence/MaterialActualJson.h"
#include "domain/Project.h"
#include <QJsonObject>
#include <cmath>
namespace jobs {
namespace {
[[noreturn]] void invalid() {
    throw AppError(Diagnostic{"MATERIAL_ACTUAL_INVALID", {"원장 JSON 형식"}, {}, "MATERIAL_ACTUAL_INVALID"});
}
std::int64_t number(const QJsonValue &v, std::int64_t max) {
    const auto n = v.toDouble(-1);
    if (!v.isDouble() || !std::isfinite(n) || n < 0 || n > double(max) || n != std::floor(n))
        invalid();
    return std::int64_t(n);
}
} // namespace
QJsonArray encodeMaterialActuals(const std::vector<MaterialActualEntry> &entries) {
    QJsonArray out;
    for (const auto &e : entries) {
        QJsonObject o;
#define TEXT(field) o[#field] = QString::fromStdString(e.field)
        TEXT(id);
        TEXT(transactionId);
        TEXT(materialId);
        TEXT(kind);
        TEXT(at);
        TEXT(taskId);
        TEXT(useId);
        TEXT(receiptId);
        TEXT(statusAt);
        TEXT(recordedAt);
        TEXT(reporter);
        TEXT(reason);
        TEXT(previousEntryId);
        TEXT(sourceDocumentId);
        TEXT(unit);
        TEXT(openingAt);
#undef TEXT
        o["quantityMilli"] = qint64(e.quantityMilli);
        o["openingMilli"] = qint64(e.openingMilli);
        o["recordedRevision"] = qint64(e.recordedRevision);
        out.append(o);
    }
    return out;
}
std::vector<MaterialActualEntry> decodeMaterialActuals(const QJsonValue &value) {
    if (!value.isArray() || value.toArray().size() > 100000)
        invalid();
    std::vector<MaterialActualEntry> out;
    for (const auto &v : value.toArray()) {
        calculationCheckpoint();
        if (!v.isObject())
            invalid();
        const auto o = v.toObject();
        if (o.size() != 19)
            invalid();
        MaterialActualEntry e;
#define TEXT(field)                                                                                                    \
    if (!o[#field].isString())                                                                                         \
        invalid();                                                                                                     \
    e.field = o[#field].toString().toStdString()
        TEXT(id);
        TEXT(transactionId);
        TEXT(materialId);
        TEXT(kind);
        TEXT(at);
        TEXT(taskId);
        TEXT(useId);
        TEXT(receiptId);
        TEXT(statusAt);
        TEXT(recordedAt);
        TEXT(reporter);
        TEXT(reason);
        TEXT(previousEntryId);
        TEXT(sourceDocumentId);
        TEXT(unit);
        TEXT(openingAt);
#undef TEXT
        e.quantityMilli = number(o["quantityMilli"], 1000000000000LL);
        e.openingMilli = number(o["openingMilli"], 1000000000000LL);
        e.recordedRevision = std::uint64_t(number(o["recordedRevision"], 9007199254740991LL));
        out.push_back(std::move(e));
    }
    return out;
}
} // namespace jobs
