#include "persistence/LedgerIdentityJson.h"
#include "persistence/ProjectJson.h"
#include "domain/NativeSchema.h"
#include "domain/LedgerIdentity.h"
#include "domain/CalculationControl.h"
#include <QJsonObject>
#include <cmath>
namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
[[noreturn]] void bad() {
    throw AppError(ErrorCode::LEDGER_ID_INVALID, {"ID 이관 이력의 필드·스냅샷·형식·한도를 확인하세요."});
}
QJsonObject object(const QJsonValue &v, const QStringList &keys) {
    if (!v.isObject())
        bad();
    auto o = v.toObject();
    if (o.size() != keys.size())
        bad();
    for (const auto &k : o.keys())
        if (!keys.contains(k))
            bad();
    return o;
}
std::string string(const QJsonObject &o, const char *k) {
    if (!o[k].isString())
        bad();
    return o[k].toString().toStdString();
}
QJsonArray array(const QJsonValue &v, int max) {
    if (!v.isArray() || v.toArray().size() > max)
        bad();
    return v.toArray();
}
Project snapshot(const QJsonValue &v) {
    if (!v.isObject() || v.toObject().contains("ledgerIdMigrations") || v.toObject().contains("currencyConversions"))
        bad();
    return json::decodeProject(v.toObject(), maximumNativeSchema);
}
} // namespace
QJsonArray encodeLedgerIdMigrations(const std::vector<LedgerIdMigration> &events) {
    QJsonArray out;
    for (const auto &e : events) {
        if (!e.before || !e.after || !e.before->ledgerIdMigrations.empty() || !e.after->ledgerIdMigrations.empty() ||
            !e.before->currencyConversions.empty() || !e.after->currencyConversions.empty())
            bad();
        QJsonArray links;
        for (const auto &l : e.links)
            links.append(QJsonObject{{"kind", q(l.kind)}, {"sourceId", q(l.sourceId)}, {"targetId", q(l.targetId)}});
        out.append(QJsonObject{{"policy", "explicit-ledger-identity-v1"},
                               {"id", q(e.id)},
                               {"sourceDocumentId", q(e.sourceDocumentId)},
                               {"recordedRevision", qint64(e.recordedRevision)},
                               {"reporter", q(e.reporter)},
                               {"reason", q(e.reason)},
                               {"recordedAt", q(e.recordedAt)},
                               {"confirmed", e.confirmed},
                               {"links", links},
                               {"before", json::encodeProject(*e.before)},
                               {"after", json::encodeProject(*e.after)}});
    }
    return out;
}
std::vector<LedgerIdMigration> decodeLedgerIdMigrations(const QJsonValue &v) {
    auto list = array(v, 16);
    if (list.isEmpty())
        bad();
    std::vector<LedgerIdMigration> out;
    std::size_t rows = 0;
    for (const auto &item : list) {
        calculationCheckpoint();
        const auto o = object(item, {"policy", "id", "sourceDocumentId", "recordedRevision", "reporter", "reason",
                                     "recordedAt", "confirmed", "links", "before", "after"});
        if (string(o, "policy") != "explicit-ledger-identity-v1" || !o["confirmed"].isBool())
            bad();
        LedgerIdMigration e;
        e.id = string(o, "id");
        e.sourceDocumentId = string(o, "sourceDocumentId");
        e.reporter = string(o, "reporter");
        e.reason = string(o, "reason");
        e.recordedAt = string(o, "recordedAt");
        e.confirmed = o["confirmed"].toBool();
        auto n = o["recordedRevision"].toDouble(-1);
        if (!o["recordedRevision"].isDouble() || !std::isfinite(n) || n < 2 || n > 9007199254740991.0 ||
            std::floor(n) != n)
            bad();
        e.recordedRevision = std::uint64_t(n);
        for (const auto &v : array(o["links"], 20000)) {
            const auto l = object(v, {"kind", "sourceId", "targetId"});
            e.links.push_back({string(l, "kind"), string(l, "sourceId"), string(l, "targetId")});
        }
        e.before = std::make_shared<const Project>(snapshot(o["before"]));
        e.after = std::make_shared<const Project>(snapshot(o["after"]));
        rows += ledgerIdentityRows(*e.before) + ledgerIdentityRows(*e.after);
        if (rows > 300000)
            bad();
        out.push_back(std::move(e));
    }
    return out;
}
} // namespace ppm
