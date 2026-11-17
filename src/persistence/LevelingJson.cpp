#include "persistence/LevelingJson.h"
#include "domain/Diagnostics.h"
#include <cmath>
namespace jobs::json {
QJsonObject encodeLeveling(const LevelingSelection &s) {
    const auto q = [](const auto &v) { return QString::fromUtf8(v); };
    return {{"priority", q(s.priority)},
            {"policy", q(s.policy)},
            {"horizonWorkDays", s.horizonWorkDays},
            {"adoptedAt", q(s.adoptedAt)},
            {"reason", q(s.reason)},
            {"sourceDocumentId", q(s.sourceDocumentId)},
            {"sourceRevision", qint64(s.sourceRevision)}};
}
LevelingSelection decodeLeveling(const QJsonValue &v) {
    const auto fail = [] { throw AppError(ErrorCode::LEVEL_OPTIONS_INVALID); };
    if (!v.isObject())
        fail();
    const auto o = v.toObject();
    const QStringList fields{"priority", "policy",           "horizonWorkDays", "adoptedAt",
                             "reason",   "sourceDocumentId", "sourceRevision"};
    if (o.size() != fields.size())
        fail();
    for (const auto &f : fields)
        if (!o.contains(f))
            fail();
    const auto str = [&](const QString &key) {
        if (!o[key].isString())
            fail();
        return o[key].toString().toStdString();
    };
    const auto number = [&](const QString &key, double limit) {
        const auto n = o[key].toDouble(-1);
        if (!o[key].isDouble() || !std::isfinite(n) || n < 0 || n > limit || n != std::floor(n))
            fail();
        return std::uint64_t(n);
    };
    LevelingSelection s;
    s.priority = str("priority");
    s.policy = str("policy");
    s.adoptedAt = str("adoptedAt");
    s.reason = str("reason");
    s.sourceDocumentId = str("sourceDocumentId");
    s.horizonWorkDays = int(number("horizonWorkDays", 5200));
    s.sourceRevision = number("sourceRevision", 9007199254740991.0);
    return s;
}
} // namespace jobs::json
