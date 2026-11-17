#include "persistence/ResourcePlanningJson.h"
#include "domain/Productivity.h"
#include <QJsonArray>
#include <cmath>
namespace jobs {
namespace {
[[noreturn]] void bad() {
    throw AppError(Diagnostic{
        "RESOURCE_PLANNING_INVALID", {"필드·형식·정수·행 한도를 확인하세요."}, {}, "RESOURCE_PLANNING_INVALID"});
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
std::int64_t num(const QJsonValue &v, std::int64_t max, std::int64_t min = 0) {
    const auto n = v.toDouble(double(min - 1));
    if (!v.isDouble() || !std::isfinite(n) || std::floor(n) != n || n < min || n > max)
        bad();
    return std::int64_t(n);
}
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
} // namespace
QJsonObject encodeProductivityRate(const ProductivityRate &r) {
    return {{"unit", q(r.unit)},
            {"effectiveDate", q(r.effectiveDate)},
            {"reference", q(r.reference)},
            {"reporter", q(r.reporter)},
            {"quantityMilliPerHour", qint64(r.quantityMilliPerHour)}};
}
ProductivityRate decodeProductivityRate(const QJsonValue &v) {
    auto o = object(v, {"unit", "effectiveDate", "reference", "reporter", "quantityMilliPerHour"});
    ProductivityRate r{str(o, "unit"), str(o, "effectiveDate"), str(o, "reference"), str(o, "reporter"),
                       num(o["quantityMilliPerHour"], 1000000000, 1)};
    validateProductivityRate(r);
    return r;
}
QJsonObject encodeProductivityEstimate(const ProductivityEstimate &e) {
    return {{"rate", encodeProductivityRate(e.rate)},
            {"quantityMilli", qint64(e.quantityMilli)},
            {"workUnitMinutes", qint64(e.workUnitMinutes)},
            {"reason", q(e.reason)},
            {"recordedAt", q(e.recordedAt)},
            {"confirmed", e.confirmed},
            {"application", q(e.application)},
            {"reporter", q(e.reporter)}};
}
ProductivityEstimate decodeProductivityEstimate(const QJsonValue &v) {
    auto o = object(v, {"rate", "quantityMilli", "workUnitMinutes", "reason", "recordedAt", "confirmed", "application",
                        "reporter"});
    if (!o["confirmed"].isBool())
        bad();
    return {decodeProductivityRate(o["rate"]),
            num(o["quantityMilli"], 1000000000),
            num(o["workUnitMinutes"], maxWork),
            str(o, "reason"),
            str(o, "recordedAt"),
            o["confirmed"].toBool(),
            str(o, "application"),
            str(o, "reporter")};
}
QJsonObject encodeNativeCurve(const NativeCurve &c) {
    QJsonArray weights, periods;
    for (auto w : c.weights)
        weights.append(w);
    for (const auto &p : c.projectedPeriods)
        periods.append(
            QJsonObject{{"fromMinute", qint64(p.fromMinute)}, {"toMinute", qint64(p.toMinute)}, {"units", p.units}});
    return {{"source", q(c.source)},
            {"externalId", q(c.externalId)},
            {"name", q(c.name)},
            {"reference", q(c.reference)},
            {"reporter", q(c.reporter)},
            {"recordedAt", q(c.recordedAt)},
            {"mspCode", c.mspCode},
            {"baseUnits", c.baseUnits},
            {"weights", weights},
            {"durationMinutes", qint64(c.durationMinutes)},
            {"workUnitMinutes", qint64(c.workUnitMinutes)},
            {"projectedPeriods", periods}};
}
NativeCurve decodeNativeCurve(const QJsonValue &v) {
    auto o = object(v, {"source", "externalId", "name", "reference", "reporter", "recordedAt", "mspCode", "baseUnits",
                        "weights", "durationMinutes", "workUnitMinutes", "projectedPeriods"});
    NativeCurve c;
    c.source = str(o, "source");
    c.externalId = str(o, "externalId");
    c.name = str(o, "name");
    c.reference = str(o, "reference");
    c.reporter = str(o, "reporter");
    c.recordedAt = str(o, "recordedAt");
    c.mspCode = int(num(o["mspCode"], 8, -1));
    c.baseUnits = int(num(o["baseUnits"], 100000, 1));
    c.durationMinutes = num(o["durationMinutes"], 5000000, 1);
    c.workUnitMinutes = num(o["workUnitMinutes"], maxWork);
    if (!o["weights"].isArray() || o["weights"].toArray().size() > 21 || !o["projectedPeriods"].isArray() ||
        o["projectedPeriods"].toArray().size() > 1024)
        bad();
    for (const auto &w : o["weights"].toArray())
        c.weights.push_back(int(num(w, 100000000)));
    for (const auto &v : o["projectedPeriods"].toArray()) {
        auto p = object(v, {"fromMinute", "toMinute", "units"});
        c.projectedPeriods.push_back(
            {num(p["fromMinute"], 5000000), num(p["toMinute"], 5000000), int(num(p["units"], 100000))});
    }
    return c;
}
} // namespace jobs
