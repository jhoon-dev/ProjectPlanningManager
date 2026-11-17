#include "persistence/XerScheduleJson.h"
#include "persistence/ProjectJson.h"
#include "domain/NativeSchema.h"
#include "domain/CalculationControl.h"
#include <QJsonObject>
#include <cmath>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
[[noreturn]] void invalid() {
    throw AppError(ErrorCode::FILE_PRIMAVERAXER_080, {"일정 채택 이력의 필드·형식·한도를 확인하세요."});
}
QJsonObject object(const QJsonValue &v, const QStringList &keys) {
    if (!v.isObject())
        invalid();
    const auto o = v.toObject();
    if (o.size() != keys.size())
        invalid();
    for (const auto &k : o.keys())
        if (!keys.contains(k))
            invalid();
    return o;
}
std::string string(const QJsonObject &o, const char *key) {
    if (!o[key].isString())
        invalid();
    return o[key].toString().toStdString();
}
bool boolean(const QJsonObject &o, const char *key) {
    if (!o[key].isBool())
        invalid();
    return o[key].toBool();
}
QJsonArray array(const QJsonValue &v, int maximum) {
    if (!v.isArray() || v.toArray().size() > maximum)
        invalid();
    return v.toArray();
}
QJsonObject plan(const QJsonValue &v) {
    if (!v.isObject())
        invalid();
    return v.toObject();
}
} // namespace
QJsonArray encodeXerScheduleAdoptions(const std::vector<XerScheduleAdoption> &events) {
    QJsonArray result;
    for (const auto &e : events) {
        QJsonArray links;
        for (const auto &l : e.links)
            links.append(QJsonObject{{"sourceId", q(l.sourceId)}, {"targetId", q(l.targetId)}, {"create", l.create}});
        const auto &s = e.source;
        QJsonObject source{{"input", json::encodePlanInput(s.input)},
                           {"sourceName", q(s.sourceName)},
                           {"sourceSha256", q(s.sourceSha256)},
                           {"projectId", q(s.projectId)},
                           {"importedAt", q(s.importedAt)},
                           {"reportJson", q(s.reportJson)},
                           {"statusAt", s.statusAt ? QJsonValue(q(*s.statusAt)) : QJsonValue(QJsonValue::Null)}};
        result.append(QJsonObject{{"policy", "explicit-plan-wbs-calendars-v1"},
                                  {"id", q(e.id)},
                                  {"sourceDocumentId", q(e.sourceDocumentId)},
                                  {"recordedRevision", qint64(e.recordedRevision)},
                                  {"source", source},
                                  {"links", links},
                                  {"schedule", e.schedule},
                                  {"wbs", e.wbs},
                                  {"calendars", e.calendars},
                                  {"confirmed", e.confirmed},
                                  {"reporter", q(e.reporter)},
                                  {"reason", q(e.reason)},
                                  {"recordedAt", q(e.recordedAt)},
                                  {"before", json::encodePlanInput(e.before)},
                                  {"after", json::encodePlanInput(e.after)}});
    }
    return result;
}
std::vector<XerScheduleAdoption> decodeXerScheduleAdoptions(const QJsonValue &v) {
    auto list = array(v, 128);
    if (list.isEmpty())
        invalid();
    std::vector<XerScheduleAdoption> result;
    std::size_t rows = 0;
    for (const auto &item : list) {
        calculationCheckpoint();
        const auto o =
            object(item, {"policy", "id", "sourceDocumentId", "recordedRevision", "source", "links", "schedule", "wbs",
                          "calendars", "confirmed", "reporter", "reason", "recordedAt", "before", "after"});
        if (string(o, "policy") != "explicit-plan-wbs-calendars-v1")
            invalid();
        XerScheduleAdoption e;
        e.id = string(o, "id");
        e.sourceDocumentId = string(o, "sourceDocumentId");
        const auto n = o["recordedRevision"].toDouble(-1);
        if (!o["recordedRevision"].isDouble() || !std::isfinite(n) || n < 1 || n > 9007199254740991.0 ||
            std::floor(n) != n)
            invalid();
        e.recordedRevision = std::uint64_t(n);
        e.schedule = boolean(o, "schedule");
        e.wbs = boolean(o, "wbs");
        e.calendars = boolean(o, "calendars");
        e.confirmed = boolean(o, "confirmed");
        e.reporter = string(o, "reporter");
        e.reason = string(o, "reason");
        e.recordedAt = string(o, "recordedAt");
        const auto s = object(
            o["source"], {"input", "sourceName", "sourceSha256", "projectId", "importedAt", "reportJson", "statusAt"});
        e.source.sourceName = string(s, "sourceName");
        e.source.sourceSha256 = string(s, "sourceSha256");
        e.source.projectId = string(s, "projectId");
        e.source.importedAt = string(s, "importedAt");
        e.source.reportJson = string(s, "reportJson");
        if (!s["statusAt"].isNull())
            e.source.statusAt = string(s, "statusAt");
        e.source.input = json::decodePlanInput(plan(s["input"]), maximumNativeSchema);
        e.before = json::decodePlanInput(plan(o["before"]), maximumNativeSchema);
        e.after = json::decodePlanInput(plan(o["after"]), maximumNativeSchema);
        for (const auto &l : array(o["links"], 10000)) {
            const auto link = object(l, {"sourceId", "targetId", "create"});
            e.links.push_back({string(link, "sourceId"), string(link, "targetId"), boolean(link, "create")});
        }
        for (const auto *input : {&e.before, &e.after, &e.source.input})
            rows += input->tasks.size() + input->resources.size() + input->assignments.size() + input->wbsNodes.size() +
                    input->calendars.size();
        if (rows > 300000)
            invalid();
        result.push_back(std::move(e));
    }
    return result;
}
} // namespace jobs
