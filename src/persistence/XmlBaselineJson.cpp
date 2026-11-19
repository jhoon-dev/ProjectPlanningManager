#include "persistence/XmlBaselineJson.h"
#include "domain/Project.h"
#include <QJsonArray>
#include <cmath>
namespace ppm::json {
namespace {
[[noreturn]] void bad() {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML 기준선 저장 형식을 확인하세요."});
}
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
QJsonValue val(const std::optional<std::string> &s) {
    return s ? QJsonValue(q(*s)) : QJsonValue(QJsonValue::Null);
}
template <class T> QJsonValue num(const std::optional<T> &n) {
    return n ? QJsonValue(double(*n)) : QJsonValue(QJsonValue::Null);
}
QJsonObject object(const QJsonValue &v, const QStringList &keys) {
    if (!v.isObject())
        bad();
    auto o = v.toObject();
    if (o.size() != keys.size())
        bad();
    for (auto k : o.keys())
        if (!keys.contains(k))
            bad();
    return o;
}
std::string text(const QJsonValue &v) {
    if (!v.isString() || v.toString().size() > 4096)
        bad();
    return v.toString().toStdString();
}
std::optional<std::string> optText(const QJsonValue &v) {
    return v.isNull() ? std::nullopt : std::optional{text(v)};
}
std::int64_t integer(const QJsonValue &v) {
    const auto n = v.toDouble();
    if (!v.isDouble() || !std::isfinite(n) || n < 0 || n > 9007199254740991.0 || std::floor(n) != n)
        bad();
    return std::int64_t(n);
}
std::optional<std::int64_t> optInt(const QJsonValue &v) {
    return v.isNull() ? std::nullopt : std::optional{integer(v)};
}
QJsonArray array(const QJsonValue &v) {
    if (!v.isArray() || v.toArray().size() > 100000)
        bad();
    return v.toArray();
}
} // namespace
QJsonObject encodeXmlBaselines(const XmlBaselineSet &s) {
    QJsonArray rows, days, periods, holidays;
    for (auto day : s.weekdays)
        days.append(day);
    for (auto [a, b] : s.periods)
        periods.append(QJsonArray{a, b});
    for (const auto &h : s.holidays)
        holidays.append(q(h));
    for (const auto &r : s.rows) {
        QJsonObject flags, money;
        QJsonArray times;
        for (const auto &[k, v] : r.flags)
            flags[q(k)] = q(v);
        for (const auto &[k, v] : r.money)
            money[q(k)] = double(v);
        for (const auto &t : r.timephased)
            times.append(QJsonObject{{"type", t.type},
                                     {"uid", q(t.uid)},
                                     {"start", val(t.start)},
                                     {"finish", val(t.finish)},
                                     {"unit", num(t.unit)},
                                     {"value", val(t.value)}});
        rows.append(QJsonObject{{"number", r.number},
                                {"kind", q(r.kind)},
                                {"id", q(r.id)},
                                {"resourceId", q(r.resourceId)},
                                {"xmlUid", q(r.xmlUid)},
                                {"start", val(r.start)},
                                {"finish", val(r.finish)},
                                {"duration", num(r.duration)},
                                {"work", num(r.work)},
                                {"cost", num(r.cost)},
                                {"flags", flags},
                                {"money", money},
                                {"timephased", times}});
        if (r.timephasedOutside) {
            auto row = rows.last().toObject();
            row["timephasedOutside"] = true;
            rows[rows.size() - 1] = row;
        }
    }
    QJsonObject output{{"sourceName", q(s.sourceName)},
                       {"sourceSha256", q(s.sourceSha256)},
                       {"reporter", q(s.reporter)},
                       {"reason", q(s.reason)},
                       {"recordedAt", q(s.recordedAt)},
                       {"weekdays", days},
                       {"periods", periods},
                       {"holidays", holidays},
                       {"rows", rows}};
    if (!s.origins.empty()) {
        QJsonArray origins;
        for (const auto &[number, origin] : s.origins) {
            XmlBaselineSet value;
            static_cast<XmlBaselineOrigin &>(value) = origin;
            auto encoded = encodeXmlBaselines(value);
            encoded.remove("rows");
            origins.append(QJsonObject{{"number", number}, {"origin", encoded}});
        }
        output["origins"] = origins;
    }
    return output;
}
XmlBaselineSet decodeXmlBaselines(const QJsonObject &input, bool allowOrigins, bool allowOutsideTimes) {
    QStringList keys{"sourceName", "sourceSha256", "reporter", "reason", "recordedAt",
                     "weekdays",   "periods",      "holidays", "rows"};
    if (allowOrigins && input.contains("origins"))
        keys.append("origins");
    const auto o = object(input, keys);
    XmlBaselineSet s;
    s.sourceName = text(o["sourceName"]);
    s.sourceSha256 = text(o["sourceSha256"]);
    s.reporter = text(o["reporter"]);
    s.reason = text(o["reason"]);
    s.recordedAt = text(o["recordedAt"]);
    for (auto v : array(o["weekdays"])) {
        auto n = integer(v);
        if (n > 7)
            bad();
        s.weekdays.push_back(int(n));
    }
    for (auto v : array(o["periods"])) {
        auto a = array(v);
        if (a.size() != 2 || integer(a[0]) > 1440 || integer(a[1]) > 1440)
            bad();
        s.periods.emplace_back(int(integer(a[0])), int(integer(a[1])));
    }
    for (auto v : array(o["holidays"]))
        s.holidays.push_back(text(v));
    for (auto value : array(o["rows"])) {
        QStringList keys{"number",   "kind", "id",   "resourceId", "xmlUid", "start",     "finish",
                         "duration", "work", "cost", "flags",      "money",  "timephased"};
        if (allowOutsideTimes && value.isObject() && value.toObject().contains("timephasedOutside"))
            keys.append("timephasedOutside");
        const auto r = object(value, keys);
        XmlBaselineRow row;
        if (r.contains("timephasedOutside")) {
            if (!r["timephasedOutside"].isBool())
                bad();
            row.timephasedOutside = r["timephasedOutside"].toBool();
        }
        auto n = integer(r["number"]);
        if (n > 10)
            bad();
        row.number = int(n);
        row.kind = text(r["kind"]);
        row.id = text(r["id"]);
        row.resourceId = text(r["resourceId"]);
        row.xmlUid = text(r["xmlUid"]);
        row.start = optText(r["start"]);
        row.finish = optText(r["finish"]);
        row.duration = optInt(r["duration"]);
        row.work = optInt(r["work"]);
        row.cost = optInt(r["cost"]);
        if (!r["flags"].isObject() || r["flags"].toObject().size() > 3 || !r["money"].isObject() ||
            r["money"].toObject().size() > 3)
            bad();
        const auto flags = r["flags"].toObject(), money = r["money"].toObject();
        for (auto it = flags.begin(); it != flags.end(); ++it)
            row.flags[it.key().toStdString()] = text(it.value());
        for (auto it = money.begin(); it != money.end(); ++it)
            row.money[it.key().toStdString()] = integer(it.value());
        for (auto v : array(r["timephased"])) {
            const auto t = object(v, {"type", "uid", "start", "finish", "unit", "value"});
            XmlBaselineTime time;
            auto type = integer(t["type"]);
            if (type > 75)
                bad();
            time.type = int(type);
            time.uid = text(t["uid"]);
            time.start = optText(t["start"]);
            time.finish = optText(t["finish"]);
            time.value = optText(t["value"]);
            if (!t["unit"].isNull()) {
                auto unit = integer(t["unit"]);
                if (unit > 8)
                    bad();
                time.unit = int(unit);
            }
            row.timephased.push_back(std::move(time));
        }
        s.rows.push_back(std::move(row));
    }
    if (o.contains("origins")) {
        auto values = array(o["origins"]);
        if (values.isEmpty() || values.size() > 11)
            bad();
        for (auto v : values) {
            auto item = object(v, {"number", "origin"});
            auto n = integer(item["number"]);
            if (n > 10)
                bad();
            auto value = object(item["origin"], {"sourceName", "sourceSha256", "reporter", "reason", "recordedAt",
                                                 "weekdays", "periods", "holidays"});
            value["rows"] = QJsonArray{};
            if (!s.origins.emplace(int(n), decodeXmlBaselines(value, false)).second)
                bad();
        }
    }
    return s;
}
QJsonArray encodeXmlBaselineMerges(const std::vector<XmlBaselineMerge> &events) {
    QJsonArray result;
    for (const auto &e : events) {
        QJsonArray links;
        for (const auto &l : e.links)
            links.append(QJsonObject{
                {"kind", q(l.kind)}, {"sourceId", q(l.sourceId)}, {"targetId", q(l.targetId)}, {"exclude", l.exclude}});
        result.append(
            QJsonObject{{"policy", "explicit-number-replace-v1"},
                        {"id", q(e.id)},
                        {"sourceDocumentId", q(e.sourceDocumentId)},
                        {"recordedRevision", qint64(e.recordedRevision)},
                        {"incoming", encodeXmlBaselines(e.incoming)},
                        {"links", links},
                        {"currency", q(e.currency)},
                        {"replaceExisting", e.replaceExisting},
                        {"confirmed", e.confirmed},
                        {"before", e.before ? QJsonValue(encodeXmlBaselines(*e.before)) : QJsonValue(QJsonValue::Null)},
                        {"after", encodeXmlBaselines(e.after)}});
    }
    return result;
}
std::vector<XmlBaselineMerge> decodeXmlBaselineMerges(const QJsonValue &input) {
    auto values = array(input);
    if (values.isEmpty() || values.size() > 128)
        bad();
    std::vector<XmlBaselineMerge> result;
    std::size_t rows = 0;
    for (auto v : values) {
        auto o = object(v, {"policy", "id", "sourceDocumentId", "recordedRevision", "incoming", "links", "currency",
                            "replaceExisting", "confirmed", "before", "after"});
        if (text(o["policy"]) != "explicit-number-replace-v1" || !o["incoming"].isObject() || !o["after"].isObject() ||
            !o["confirmed"].isBool() || !o["replaceExisting"].isBool())
            bad();
        XmlBaselineMerge e;
        e.id = text(o["id"]);
        e.sourceDocumentId = text(o["sourceDocumentId"]);
        e.recordedRevision = std::uint64_t(integer(o["recordedRevision"]));
        e.currency = text(o["currency"]);
        e.incoming = decodeXmlBaselines(o["incoming"].toObject());
        e.after = decodeXmlBaselines(o["after"].toObject());
        if (!o["before"].isNull()) {
            if (!o["before"].isObject())
                bad();
            e.before = decodeXmlBaselines(o["before"].toObject());
        }
        e.confirmed = o["confirmed"].toBool();
        e.replaceExisting = o["replaceExisting"].toBool();
        auto links = array(o["links"]);
        if (links.size() > 30001)
            bad();
        for (auto link : links) {
            auto l = object(link, {"kind", "sourceId", "targetId", "exclude"});
            if (!l["exclude"].isBool())
                bad();
            e.links.push_back({text(l["kind"]), text(l["sourceId"]), text(l["targetId"]), l["exclude"].toBool()});
        }
        rows += e.incoming.rows.size() + e.after.rows.size() + (e.before ? e.before->rows.size() : 0);
        if (rows > 300000)
            bad();
        result.push_back(std::move(e));
    }
    return result;
}
} // namespace ppm::json
