#include "persistence/ProjectCurrencyJson.h"
#include "persistence/ProjectJson.h"
#include "domain/NativeSchema.h"
#include "domain/ProjectCurrency.h"
#include "domain/LedgerIdentity.h"
#include "domain/CalculationControl.h"
#include <cmath>
namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
[[noreturn]] void bad() {
    throw AppError(Diagnostic{"CURRENCY_CONVERSION_INVALID",
                              {"통화 전환의 필드·형식·정수·중첩 이력·스냅샷 한도를 확인하세요."},
                              {},
                              "CURRENCY_CONVERSION_INVALID"});
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
std::string str(const QJsonObject &o, const char *key) {
    if (!o[key].isString())
        bad();
    return o[key].toString().toStdString();
}
std::int64_t integer(const QJsonValue &v, std::int64_t maximum) {
    auto n = v.toDouble(-1);
    if (!v.isDouble() || !std::isfinite(n) || n < 1 || n > maximum || std::floor(n) != n)
        bad();
    return std::int64_t(n);
}
Project snapshot(const QJsonValue &v) {
    if (!v.isObject() || v.toObject().contains("currencyConversions") || v.toObject().contains("ledgerIdMigrations"))
        bad();
    return json::decodeProject(v.toObject(), maximumNativeSchema);
}
} // namespace
QJsonObject encodeCurrencyRate(const CurrencyRate &r) {
    return {{"fromCurrency", q(r.fromCurrency)},   {"targetCurrency", q(r.targetCurrency)},
            {"effectiveDate", q(r.effectiveDate)}, {"reference", q(r.reference)},
            {"rounding", q(r.rounding)},           {"numerator", qint64(r.numerator)},
            {"denominator", qint64(r.denominator)}};
}
CurrencyRate decodeCurrencyRate(const QJsonValue &v) {
    auto o = object(
        v, {"fromCurrency", "targetCurrency", "effectiveDate", "reference", "rounding", "numerator", "denominator"});
    CurrencyRate r;
    r.fromCurrency = str(o, "fromCurrency");
    r.targetCurrency = str(o, "targetCurrency");
    r.effectiveDate = str(o, "effectiveDate");
    r.reference = str(o, "reference");
    r.rounding = str(o, "rounding");
    r.numerator = integer(o["numerator"], 1000000000);
    r.denominator = integer(o["denominator"], 1000000000);
    validateCurrencyRate(r);
    return r;
}
QJsonArray encodeCurrencyConversions(const std::vector<CurrencyConversion> &events) {
    QJsonArray out;
    for (const auto &e : events) {
        if (!e.before || !e.after || !e.before->currencyConversions.empty() || !e.before->ledgerIdMigrations.empty() ||
            !e.after->currencyConversions.empty() || !e.after->ledgerIdMigrations.empty())
            bad();
        out.append(QJsonObject{{"policy", "project-currency-v1"},
                               {"id", q(e.id)},
                               {"sourceDocumentId", q(e.sourceDocumentId)},
                               {"recordedRevision", qint64(e.recordedRevision)},
                               {"rate", encodeCurrencyRate(e.rate)},
                               {"reporter", q(e.reporter)},
                               {"reason", q(e.reason)},
                               {"recordedAt", q(e.recordedAt)},
                               {"confirmed", e.confirmed},
                               {"before", json::encodeProject(*e.before)},
                               {"after", json::encodeProject(*e.after)}});
    }
    return out;
}
std::vector<CurrencyConversion> decodeCurrencyConversions(const QJsonValue &v) {
    if (!v.isArray() || v.toArray().isEmpty() || v.toArray().size() > 16)
        bad();
    std::vector<CurrencyConversion> out;
    std::size_t rows = 0;
    for (const auto &value : v.toArray()) {
        calculationCheckpoint();
        auto o = object(value, {"policy", "id", "sourceDocumentId", "recordedRevision", "rate", "reporter", "reason",
                                "recordedAt", "confirmed", "before", "after"});
        if (str(o, "policy") != "project-currency-v1" || !o["confirmed"].isBool())
            bad();
        CurrencyConversion e;
        e.id = str(o, "id");
        e.sourceDocumentId = str(o, "sourceDocumentId");
        e.recordedRevision = std::uint64_t(integer(o["recordedRevision"], 9007199254740991LL));
        e.rate = decodeCurrencyRate(o["rate"]);
        e.reporter = str(o, "reporter");
        e.reason = str(o, "reason");
        e.recordedAt = str(o, "recordedAt");
        e.confirmed = o["confirmed"].toBool();
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
