#include "persistence/FinancialPeriodJson.h"
#include "domain/Project.h"
#include <QJsonObject>
#include <cmath>
namespace ppm::json {
namespace {
[[noreturn]] void invalid() {
    throw AppError(ErrorCode::FINANCIAL_PERIOD_INVALID, {"native 필드/형식/한도"});
}
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
QJsonValue num(std::optional<std::int64_t> n) {
    return n ? QJsonValue(qint64(*n)) : QJsonValue(QJsonValue::Null);
}
QJsonObject object(const QJsonValue &v, const QStringList &keys) {
    if (!v.isObject())
        invalid();
    auto o = v.toObject();
    if (o.size() != keys.size())
        invalid();
    for (const auto &key : o.keys())
        if (!keys.contains(key))
            invalid();
    return o;
}
QJsonArray array(const QJsonValue &v, std::size_t limit) {
    if (!v.isArray() || std::size_t(v.toArray().size()) > limit)
        invalid();
    return v.toArray();
}
std::string text(const QJsonObject &o, const char *key) {
    if (!o[key].isString() || o[key].toString().size() > 4096)
        invalid();
    return o[key].toString().toStdString();
}
bool flag(const QJsonObject &o, const char *key) {
    if (!o[key].isBool())
        invalid();
    return o[key].toBool();
}
std::int64_t integer(const QJsonValue &v, std::int64_t min, std::int64_t max) {
    const double n = v.toDouble();
    if (!v.isDouble() || !std::isfinite(n) || std::floor(n) != n || n < double(min) || n > double(max))
        invalid();
    return std::int64_t(n);
}
std::optional<std::int64_t> money(const QJsonValue &v) {
    return v.isNull() ? std::nullopt : std::optional{integer(v, -maxMoney, maxMoney)};
}
QJsonObject encodeBalance(const FinancialPeriodBalance &b) {
    return {{"bac", num(b.bac)},
            {"pv", num(b.pv)},
            {"ev", num(b.ev)},
            {"ac", num(b.ac)},
            {"estimateSupported", b.estimateSupported},
            {"acReportId", q(b.acReportId)},
            {"evReportId", q(b.evReportId)}};
}
FinancialPeriodBalance decodeBalance(const QJsonValue &v) {
    const auto b = object(v, {"bac", "pv", "ev", "ac", "estimateSupported", "acReportId", "evReportId"});
    return {money(b["bac"]),       money(b["pv"]),       money(b["ev"]), money(b["ac"]), flag(b, "estimateSupported"),
            text(b, "acReportId"), text(b, "evReportId")};
}
} // namespace
QJsonArray encodeFinancialPeriodEvents(const std::vector<FinancialPeriodEvent> &events) {
    QJsonArray out;
    for (const auto &e : events) {
        QJsonArray values, tasks;
        for (const auto &v : e.period.values)
            values.append(QJsonObject{{"taskId", q(v.taskId)}, {"budget", num(v.budget)}, {"earned", num(v.earned)}});
        for (const auto &t : e.tasks)
            tasks.append(QJsonObject{{"taskId", q(t.taskId)},
                                     {"opening", encodeBalance(t.opening)},
                                     {"closing", encodeBalance(t.closing)},
                                     {"dated", num(t.dated)}});
        QJsonObject period{{"id", q(e.period.id)},
                           {"name", q(e.period.name)},
                           {"baselineId", q(e.period.baselineId)},
                           {"fromDate", q(e.period.fromDate)},
                           {"toDate", q(e.period.toDate)},
                           {"values", values}};
        out.append(QJsonObject{{"policy", "custom-financial-period-v1"},
                               {"id", q(e.id)},
                               {"action", q(e.action)},
                               {"period", period},
                               {"formula", q(e.formula)},
                               {"reporter", q(e.reporter)},
                               {"reason", q(e.reason)},
                               {"recordedAt", q(e.recordedAt)},
                               {"confirmed", e.confirmed},
                               {"sourceDocumentId", q(e.sourceDocumentId)},
                               {"currency", q(e.currency)},
                               {"recordedRevision", qint64(e.recordedRevision)},
                               {"knowledgeRevision", qint64(e.knowledgeRevision)},
                               {"statusAt", e.statusAt ? QJsonValue(q(*e.statusAt)) : QJsonValue(QJsonValue::Null)},
                               {"tasks", tasks}});
    }
    return out;
}
std::vector<FinancialPeriodEvent> decodeFinancialPeriodEvents(const QJsonValue &v) {
    std::vector<FinancialPeriodEvent> out;
    std::size_t refs = 0;
    for (const auto &item : array(v, 512)) {
        const auto o = object(item, {"policy", "id", "action", "period", "formula", "reporter", "reason", "recordedAt",
                                     "confirmed", "sourceDocumentId", "currency", "recordedRevision",
                                     "knowledgeRevision", "statusAt", "tasks"});
        if (text(o, "policy") != "custom-financial-period-v1")
            invalid();
        FinancialPeriodEvent e;
        e.id = text(o, "id");
        e.action = text(o, "action");
        e.formula = text(o, "formula");
        e.reporter = text(o, "reporter");
        e.reason = text(o, "reason");
        e.recordedAt = text(o, "recordedAt");
        e.confirmed = flag(o, "confirmed");
        e.sourceDocumentId = text(o, "sourceDocumentId");
        e.currency = text(o, "currency");
        e.recordedRevision = std::uint64_t(integer(o["recordedRevision"], 1, 9007199254740991LL));
        e.knowledgeRevision = std::uint64_t(integer(o["knowledgeRevision"], 1, 9007199254740991LL));
        if (!o["statusAt"].isNull())
            e.statusAt = text(o, "statusAt");
        const auto d = object(o["period"], {"id", "name", "baselineId", "fromDate", "toDate", "values"});
        e.period.id = text(d, "id");
        e.period.name = text(d, "name");
        e.period.baselineId = text(d, "baselineId");
        e.period.fromDate = text(d, "fromDate");
        e.period.toDate = text(d, "toDate");
        const auto values = array(d["values"], 10000), tasks = array(o["tasks"], 10000);
        refs += std::size_t(values.size() + tasks.size());
        if (refs > 100000)
            invalid();
        for (const auto &value : values) {
            const auto a = object(value, {"taskId", "budget", "earned"});
            e.period.values.push_back({text(a, "taskId"), money(a["budget"]), money(a["earned"])});
        }
        for (const auto &task : tasks) {
            const auto t = object(task, {"taskId", "opening", "closing", "dated"});
            e.tasks.push_back(
                {text(t, "taskId"), decodeBalance(t["opening"]), decodeBalance(t["closing"]), money(t["dated"])});
        }
        out.push_back(std::move(e));
    }
    return out;
}
} // namespace ppm::json
