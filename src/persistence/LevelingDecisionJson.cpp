#include "persistence/LevelingDecisionJson.h"
#include "persistence/LevelingJson.h"
#include "domain/Diagnostics.h"
#include <cmath>
namespace jobs::json {
QJsonArray encodeLevelingDecisions(const std::vector<LevelingDecision> &entries) {
    QJsonArray out;
    const auto q = [](const std::string &v) { return QString::fromUtf8(v); };
    const auto stamp = [&](const auto &v) { return v ? QJsonValue(q(*v)) : QJsonValue(QJsonValue::Null); };
    const auto selection = [](const auto &v) {
        return v ? QJsonValue(encodeLeveling(*v)) : QJsonValue(QJsonValue::Null);
    };
    for (const auto &e : entries)
        out.append(QJsonObject{{"id", q(e.id)},
                               {"previousEntryId", q(e.previousEntryId)},
                               {"recordedAt", q(e.recordedAt)},
                               {"sourceDocumentId", q(e.sourceDocumentId)},
                               {"action", q(e.action)},
                               {"reason", q(e.reason)},
                               {"sourceRevision", qint64(e.sourceRevision)},
                               {"recordedRevision", qint64(e.recordedRevision)},
                               {"beforeStatusAt", stamp(e.beforeStatusAt)},
                               {"afterStatusAt", stamp(e.afterStatusAt)},
                               {"beforePlan", selection(e.beforePlan)},
                               {"beforeRemaining", selection(e.beforeRemaining)},
                               {"afterPlan", selection(e.afterPlan)},
                               {"afterRemaining", selection(e.afterRemaining)}});
    return out;
}
std::vector<LevelingDecision> decodeLevelingDecisions(const QJsonValue &v) {
    const auto fail = [] { throw AppError(ErrorCode::LEVEL_DECISION_INVALID); };
    if (!v.isArray() || v.toArray().size() > 10000)
        fail();
    std::vector<LevelingDecision> out;
    const QStringList fields{"id",         "previousEntryId", "recordedAt",       "sourceDocumentId", "action",
                             "reason",     "sourceRevision",  "recordedRevision", "beforeStatusAt",   "afterStatusAt",
                             "beforePlan", "beforeRemaining", "afterPlan",        "afterRemaining"};
    for (const auto &value : v.toArray()) {
        if (!value.isObject())
            fail();
        const auto o = value.toObject();
        if (o.size() != fields.size())
            fail();
        for (const auto &f : fields)
            if (!o.contains(f))
                fail();
        const auto str = [&](const char *key) {
            if (!o[key].isString())
                fail();
            return o[key].toString().toStdString();
        };
        const auto number = [&](const char *key) {
            const auto n = o[key].toDouble(-1);
            if (!o[key].isDouble() || !std::isfinite(n) || n < 1 || n > 9007199254740991.0 || n != std::floor(n))
                fail();
            return std::uint64_t(n);
        };
        const auto stamp = [&](const char *key) -> std::optional<std::string> {
            return o[key].isNull() ? std::nullopt : std::optional<std::string>(str(key));
        };
        const auto selection = [&](const char *key) -> std::optional<LevelingSelection> {
            return o[key].isNull() ? std::nullopt : std::optional<LevelingSelection>(decodeLeveling(o[key]));
        };
        LevelingDecision e;
        e.id = str("id");
        e.previousEntryId = str("previousEntryId");
        e.recordedAt = str("recordedAt");
        e.sourceDocumentId = str("sourceDocumentId");
        e.action = str("action");
        e.reason = str("reason");
        e.sourceRevision = number("sourceRevision");
        e.recordedRevision = number("recordedRevision");
        e.beforeStatusAt = stamp("beforeStatusAt");
        e.afterStatusAt = stamp("afterStatusAt");
        e.beforePlan = selection("beforePlan");
        e.beforeRemaining = selection("beforeRemaining");
        e.afterPlan = selection("afterPlan");
        e.afterRemaining = selection("afterRemaining");
        out.push_back(std::move(e));
    }
    return out;
}
} // namespace jobs::json
