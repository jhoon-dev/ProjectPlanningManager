#include "persistence/ProgressPolicySelectionJson.h"
#include "domain/Diagnostics.h"
#include <cmath>
namespace ppm::json {
QJsonObject encodeProgressPolicySelection(const ProgressPolicySelection &s) {
    const auto q = [](const auto &v) { return QString::fromUtf8(v); };
    return {{"policy", q(s.policy)},
            {"adoptedBy", q(s.adoptedBy)},
            {"adoptedAt", q(s.adoptedAt)},
            {"reason", q(s.reason)},
            {"sourceDocumentId", q(s.sourceDocumentId)},
            {"sourceRevision", qint64(s.sourceRevision)}};
}
ProgressPolicySelection decodeProgressPolicySelection(const QJsonValue &v) {
    const auto fail = [] { throw AppError(ErrorCode::PROGRESS_POLICY_INVALID); };
    if (!v.isObject())
        fail();
    const auto o = v.toObject();
    const QStringList fields{"policy", "adoptedBy", "adoptedAt", "reason", "sourceDocumentId", "sourceRevision"};
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
    const auto n = o["sourceRevision"].toDouble(-1);
    if (!o["sourceRevision"].isDouble() || !std::isfinite(n) || n < 1 || n > 9007199254740991.0 || n != std::floor(n))
        fail();
    ProgressPolicySelection s{str("policy"), str("adoptedBy"),        str("adoptedAt"),
                              str("reason"), str("sourceDocumentId"), std::uint64_t(n)};
    validateProgressPolicySelection(s);
    return s;
}
} // namespace ppm::json
