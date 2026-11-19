#include "persistence/ExternalEvmJson.h"
#include "domain/Project.h"
#include <QJsonObject>
#include <cmath>
namespace ppm {
namespace {
[[noreturn]] void invalid() {
    throw AppError(ErrorCode::EXTERNAL_EVM_INVALID, {"native 채택 필드/형식/한도"});
}
QJsonObject object(const QJsonValue &value, const QStringList &fields) {
    if (!value.isObject())
        invalid();
    auto out = value.toObject();
    if (out.size() != fields.size())
        invalid();
    for (const auto &key : out.keys())
        if (!fields.contains(key))
            invalid();
    return out;
}
std::string text(const QJsonObject &o, const char *key) {
    if (!o[key].isString())
        invalid();
    return o[key].toString().toStdString();
}
bool flag(const QJsonObject &o, const char *key) {
    if (!o[key].isBool())
        invalid();
    return o[key].toBool();
}
std::int64_t integer(const QJsonValue &v, std::int64_t max) {
    const auto n = v.toDouble(-1);
    if (!v.isDouble() || !std::isfinite(n) || n < 0 || n > double(max) || std::floor(n) != n)
        invalid();
    return std::int64_t(n);
}
QJsonArray array(const QJsonValue &v, std::size_t max) {
    if (!v.isArray() || std::size_t(v.toArray().size()) > max)
        invalid();
    return v.toArray();
}
QJsonValue amount(std::optional<std::int64_t> n) {
    return n ? QJsonValue(qint64(*n)) : QJsonValue(QJsonValue::Null);
}
std::optional<std::int64_t> amount(const QJsonValue &v) {
    return v.isNull() ? std::nullopt : std::optional{integer(v, maxMoney)};
}
} // namespace
QJsonArray encodeExternalEvmAdoptions(const std::vector<ExternalEvmAdoption> &items) {
    QJsonArray out;
    for (const auto &a : items) {
        QJsonArray tasks, rows;
        for (const auto &id : a.taskIds)
            tasks.append(QString::fromStdString(id));
        for (const auto &r : a.rows)
            rows.append(QJsonObject{{"periodIndex", qint64(r.periodIndex)},
                                    {"openingEv", amount(r.openingEv)},
                                    {"openingPv", amount(r.openingPv)}});
        out.append(
            QJsonObject{{"policy", "xer-native-evm-v1"},
                        {"id", QString::fromStdString(a.id)},
                        {"baselineId", QString::fromStdString(a.baselineId)},
                        {"financialRevisionId", QString::fromStdString(a.financialRevisionId)},
                        {"sourceSha256", QString::fromStdString(a.sourceSha256)},
                        {"sourceProjectionSha256", QString::fromStdString(a.sourceProjectionSha256)},
                        {"taskIds", tasks},
                        {"rows", rows},
                        {"useEv", a.useEv},
                        {"usePv", a.usePv},
                        {"withdrawn", a.withdrawn},
                        {"confirmed", a.confirmed},
                        {"reporter", QString::fromStdString(a.reporter)},
                        {"reason", QString::fromStdString(a.reason)},
                        {"recordedAt", QString::fromStdString(a.recordedAt)},
                        {"currency", QString::fromStdString(a.currency)},
                        {"sourceStatusAt", QString::fromStdString(a.sourceStatusAt)},
                        {"nativeStatusAt", a.nativeStatusAt ? QJsonValue(QString::fromStdString(*a.nativeStatusAt))
                                                            : QJsonValue(QJsonValue::Null)},
                        {"sourceDocumentId", QString::fromStdString(a.sourceDocumentId)},
                        {"recordedRevision", qint64(a.recordedRevision)}});
    }
    return out;
}
std::vector<ExternalEvmAdoption> decodeExternalEvmAdoptions(const QJsonValue &v) {
    std::vector<ExternalEvmAdoption> out;
    std::size_t rowCount = 0;
    for (const auto &entry : array(v, 128)) {
        auto o = object(entry, {"policy",
                                "id",
                                "baselineId",
                                "financialRevisionId",
                                "sourceSha256",
                                "sourceProjectionSha256",
                                "taskIds",
                                "rows",
                                "useEv",
                                "usePv",
                                "withdrawn",
                                "confirmed",
                                "reporter",
                                "reason",
                                "recordedAt",
                                "currency",
                                "sourceStatusAt",
                                "nativeStatusAt",
                                "sourceDocumentId",
                                "recordedRevision"});
        if (text(o, "policy") != "xer-native-evm-v1")
            invalid();
        ExternalEvmAdoption a;
        a.id = text(o, "id");
        a.baselineId = text(o, "baselineId");
        a.financialRevisionId = text(o, "financialRevisionId");
        a.sourceSha256 = text(o, "sourceSha256");
        a.sourceProjectionSha256 = text(o, "sourceProjectionSha256");
        a.useEv = flag(o, "useEv");
        a.usePv = flag(o, "usePv");
        a.withdrawn = flag(o, "withdrawn");
        a.confirmed = flag(o, "confirmed");
        a.reporter = text(o, "reporter");
        a.reason = text(o, "reason");
        a.recordedAt = text(o, "recordedAt");
        a.currency = text(o, "currency");
        a.sourceStatusAt = text(o, "sourceStatusAt");
        a.sourceDocumentId = text(o, "sourceDocumentId");
        if (!o["nativeStatusAt"].isNull())
            a.nativeStatusAt = text(o, "nativeStatusAt");
        a.recordedRevision = std::uint64_t(integer(o["recordedRevision"], 9007199254740991LL));
        for (const auto &id : array(o["taskIds"], 10000)) {
            if (!id.isString())
                invalid();
            a.taskIds.push_back(id.toString().toStdString());
        }
        const auto rows = array(o["rows"], 100000);
        rowCount += std::size_t(rows.size()) + a.taskIds.size();
        if (rowCount > 100000)
            invalid();
        for (const auto &r : rows) {
            auto row = object(r, {"periodIndex", "openingEv", "openingPv"});
            a.rows.push_back(
                {std::size_t(integer(row["periodIndex"], 99999)), amount(row["openingEv"]), amount(row["openingPv"])});
        }
        out.push_back(std::move(a));
    }
    return out;
}
} // namespace ppm
