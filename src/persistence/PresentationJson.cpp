#include "persistence/PresentationJson.h"
#include "domain/Diagnostics.h"
#include <QJsonArray>
#include <cmath>
namespace jobs::json {
namespace {
[[noreturn]] void bad() {
    throw AppError(Diagnostic{"PRESENTATION_INVALID", {}, {}});
}
void keys(const QJsonObject &o, const QStringList &allowed) {
    for (const auto &k : o.keys())
        if (!allowed.contains(k))
            bad();
}
int integer(const QJsonObject &o, const QString &key) {
    const auto v = o.value(key);
    const auto n = v.toDouble();
    if (!v.isDouble() || !std::isfinite(n) || n != std::floor(n) || n < -1000000 || n > 1000000)
        bad();
    return int(n);
}
bool boolean(const QJsonObject &o, const QString &key) {
    if (!o.value(key).isBool())
        bad();
    return o.value(key).toBool();
}
} // namespace
void encodePresentation(QJsonObject &o, const Project &p) {
    if (p.printPreferences) {
        const auto &s = *p.printPreferences;
        QJsonObject v;
        v["paper"] = s.paper;
        v["orientation"] = s.orientation;
        v["margin"] = s.margin;
        v["fontPoints"] = s.fontPoints;
        v["tablePercent"] = s.tablePercent;
        v["nameWidth"] = s.nameWidth;
        v["daysPerPage"] = s.daysPerPage;
        v["networkScale"] = s.networkScale;
        v["selectedOnly"] = s.selectedOnly;
        v["fitTime"] = s.fitTime;
        v["baseline"] = s.baseline;
        v["automaticDates"] = s.automaticDates;
        v["relations"] = s.relations;
        v["wbs"] = s.wbs;
        v["networkFit"] = s.networkFit;
        v["from"] = QString::fromStdString(s.from);
        v["to"] = QString::fromStdString(s.to);
        o["printPreferences"] = v;
    }
    if (!p.networkPositions.empty()) {
        QJsonArray a;
        for (const auto &v : p.networkPositions)
            a.append(QJsonObject{{"taskId", QString::fromStdString(v.taskId)}, {"x", v.x}, {"y", v.y}});
        o["networkPositions"] = a;
    }
}
void decodePresentation(Project &p, const QJsonObject &o) {
    if (o.contains("printPreferences")) {
        if (!o["printPreferences"].isObject())
            bad();
        const auto v = o["printPreferences"].toObject();
        PrintPreferences s;
        keys(v, {"paper", "orientation", "margin", "fontPoints", "tablePercent", "nameWidth", "daysPerPage",
                 "selectedOnly", "fitTime", "baseline", "automaticDates", "relations", "wbs", "networkFit",
                 "networkScale", "from", "to"});
        s.paper = integer(v, "paper");
        s.orientation = integer(v, "orientation");
        s.margin = integer(v, "margin");
        s.fontPoints = integer(v, "fontPoints");
        s.tablePercent = integer(v, "tablePercent");
        s.nameWidth = integer(v, "nameWidth");
        s.daysPerPage = integer(v, "daysPerPage");
        s.networkScale = integer(v, "networkScale");
        s.selectedOnly = boolean(v, "selectedOnly");
        s.fitTime = boolean(v, "fitTime");
        s.baseline = boolean(v, "baseline");
        s.automaticDates = boolean(v, "automaticDates");
        s.relations = boolean(v, "relations");
        s.wbs = boolean(v, "wbs");
        s.networkFit = boolean(v, "networkFit");
        if (!v["from"].isString() || !v["to"].isString())
            bad();
        s.from = v["from"].toString().toStdString();
        s.to = v["to"].toString().toStdString();
        p.printPreferences = s;
    }
    if (o.contains("networkPositions")) {
        if (!o["networkPositions"].isArray())
            bad();
        const auto a = o["networkPositions"].toArray();
        if (a.size() > 10000)
            bad();
        for (const auto &x : a) {
            if (!x.isObject())
                bad();
            const auto v = x.toObject();
            keys(v, {"taskId", "x", "y"});
            if (!v["taskId"].isString())
                bad();
            p.networkPositions.push_back({v["taskId"].toString().toStdString(), integer(v, "x"), integer(v, "y")});
        }
    }
}
} // namespace jobs::json
