#include "persistence/ResourcePlanJson.h"
#include "domain/Project.h"
#include "domain/CalculationControl.h"
#include <QJsonArray>
#include <cmath>
namespace ppm {
namespace {
[[noreturn]] void invalid() {
    throw AppError(Diagnostic{"RESOURCE_PLAN_INVALID", {"JSON 형식·필드·정수 범위"}, {}});
}
QString q(const std::string &s) {
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
std::string text(const QJsonObject &o, const char *key) {
    if (!o[key].isString())
        invalid();
    return o[key].toString().toStdString();
}
std::int64_t number(const QJsonValue &v, std::int64_t max) {
    const auto n = v.toDouble(-1);
    if (!v.isDouble() || !std::isfinite(n) || n < 0 || n > double(max) || n != std::floor(n))
        invalid();
    return std::int64_t(n);
}
QJsonValue optional(std::optional<std::int64_t> v) {
    return v ? QJsonValue(qint64(*v)) : QJsonValue(QJsonValue::Null);
}
std::optional<std::int64_t> optional(const QJsonValue &v, std::int64_t max) {
    return v.isNull() ? std::nullopt : std::optional<std::int64_t>{number(v, max)};
}
QJsonArray array(const QJsonValue &v, int max) {
    if (!v.isArray() || v.toArray().size() > max)
        invalid();
    return v.toArray();
}
} // namespace
QJsonObject encodeResourcePlan(const ResourcePlan &p) {
    QJsonArray materials, uses, expenses, roles, demands;
    for (const auto &m : p.materials) {
        QJsonObject row{{"id", q(m.id)},
                        {"name", q(m.name)},
                        {"unit", q(m.unit)},
                        {"unitPriceMinor", optional(m.unitPriceMinor)},
                        {"stockMilli", optional(m.stockMilli)}};
        if (m.stockAt)
            row["stockAt"] = q(*m.stockAt);
        materials.append(row);
    }
    for (const auto &u : p.uses)
        uses.append(QJsonObject{{"id", q(u.id)},
                                {"taskId", q(u.taskId)},
                                {"materialId", q(u.materialId)},
                                {"quantityMilli", qint64(u.quantityMilli)},
                                {"accrual", q(u.accrual)}});
    for (const auto &e : p.expenses)
        expenses.append(QJsonObject{{"id", q(e.id)},
                                    {"taskId", q(e.taskId)},
                                    {"name", q(e.name)},
                                    {"account", q(e.account)},
                                    {"amountMinor", optional(e.amountMinor)},
                                    {"accrual", q(e.accrual)}});
    for (const auto &r : p.roles)
        roles.append(QJsonObject{{"id", q(r.id)}, {"name", q(r.name)}, {"type", q(r.type)}});
    for (const auto &d : p.demands)
        demands.append(QJsonObject{{"id", q(d.id)},
                                   {"taskId", q(d.taskId)},
                                   {"roleId", q(d.roleId)},
                                   {"resourceId", q(d.resourceId)},
                                   {"units", d.units}});
    QJsonObject result{
        {"materials", materials}, {"uses", uses}, {"expenses", expenses}, {"roles", roles}, {"demands", demands}};
    if (!p.receipts.empty()) {
        QJsonArray receipts;
        for (const auto &r : p.receipts)
            receipts.append(QJsonObject{{"id", q(r.id)},
                                        {"materialId", q(r.materialId)},
                                        {"at", q(r.at)},
                                        {"quantityMilli", qint64(r.quantityMilli)}});
        result["receipts"] = receipts;
    }
    if (p.autoDelayRemainingForReceipts)
        result["autoDelayRemainingForReceipts"] = true;
    if (p.autoDelayForReceipts)
        result["autoDelayForReceipts"] = true;
    return result;
}
ResourcePlan decodeResourcePlan(const QJsonValue &v, int schema) {
    const bool receipts = schema >= 81 && v.isObject() && v.toObject().contains("receipts");
    auto fields = QStringList{"materials", "uses", "expenses", "roles", "demands"};
    if (receipts)
        fields.append("receipts");
    const bool automatic = schema >= 82 && v.isObject() && v.toObject().contains("autoDelayForReceipts");
    if (automatic)
        fields.append("autoDelayForReceipts");
    const bool remaining = schema >= 83 && v.isObject() && v.toObject().contains("autoDelayRemainingForReceipts");
    if (remaining)
        fields.append("autoDelayRemainingForReceipts");
    const auto root = object(v, fields);
    ResourcePlan p;
    if (remaining) {
        if (!root["autoDelayRemainingForReceipts"].isBool())
            invalid();
        p.autoDelayRemainingForReceipts = root["autoDelayRemainingForReceipts"].toBool();
    }
    if (automatic) {
        if (!root["autoDelayForReceipts"].isBool())
            invalid();
        p.autoDelayForReceipts = root["autoDelayForReceipts"].toBool();
    }
    for (const auto &row : array(root["materials"], 10000)) {
        calculationCheckpoint();
        const bool dated = schema >= 81 && row.isObject() && row.toObject().contains("stockAt");
        const auto o = object(row, dated ? QStringList{"id", "name", "unit", "unitPriceMinor", "stockMilli", "stockAt"}
                                         : QStringList{"id", "name", "unit", "unitPriceMinor", "stockMilli"});
        p.materials.push_back({text(o, "id"), text(o, "name"), text(o, "unit"), optional(o["unitPriceMinor"], maxMoney),
                               optional(o["stockMilli"], 1000000000000LL)});
        if (dated)
            p.materials.back().stockAt = text(o, "stockAt");
    }
    for (const auto &row : array(root["uses"], 100000)) {
        calculationCheckpoint();
        const auto o = object(row, {"id", "taskId", "materialId", "quantityMilli", "accrual"});
        p.uses.push_back({text(o, "id"), text(o, "taskId"), text(o, "materialId"),
                          number(o["quantityMilli"], 1000000000000LL), text(o, "accrual")});
    }
    for (const auto &row : array(root["expenses"], 100000)) {
        calculationCheckpoint();
        const auto o = object(row, {"id", "taskId", "name", "account", "amountMinor", "accrual"});
        p.expenses.push_back({text(o, "id"), text(o, "taskId"), text(o, "name"), text(o, "account"),
                              optional(o["amountMinor"], maxMoney), text(o, "accrual")});
    }
    for (const auto &row : array(root["roles"], 10000)) {
        calculationCheckpoint();
        const auto o = object(row, {"id", "name", "type"});
        p.roles.push_back({text(o, "id"), text(o, "name"), text(o, "type")});
    }
    for (const auto &row : array(root["demands"], 10000)) {
        calculationCheckpoint();
        const auto o = object(row, {"id", "taskId", "roleId", "resourceId", "units"});
        p.demands.push_back({text(o, "id"), text(o, "taskId"), text(o, "roleId"), text(o, "resourceId"),
                             int(number(o["units"], 100000))});
    }
    if (receipts)
        for (const auto &row : array(root["receipts"], 100000)) {
            calculationCheckpoint();
            const auto o = object(row, {"id", "materialId", "at", "quantityMilli"});
            p.receipts.push_back(
                {text(o, "id"), text(o, "materialId"), text(o, "at"), number(o["quantityMilli"], 1000000000000LL)});
        }
    return p;
}
} // namespace ppm
