#include "persistence/XerFinancialJson.h"
#include "persistence/ProjectCurrencyJson.h"
#include "persistence/ProjectJson.h"
#include "domain/CalculationControl.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <cmath>
#include <map>
namespace ppm {
namespace {
QString q(const std::string &v) {
    return QString::fromStdString(v);
}
[[noreturn]] void invalid() {
    throw AppError(ErrorCode::FILE_PRIMAVERAXER_080, {"financials/native fields"});
}
QJsonObject object(const QJsonValue &v, const QStringList &fields) {
    if (!v.isObject())
        invalid();
    auto o = v.toObject();
    if (o.size() != fields.size())
        invalid();
    for (const auto &key : o.keys())
        if (!fields.contains(key))
            invalid();
    return o;
}
std::string string(const QJsonObject &o, const char *key) {
    if (!o[key].isString())
        invalid();
    return o[key].toString().toStdString();
}
std::optional<std::int64_t> number(const QJsonValue &v) {
    if (v.isNull())
        return {};
    auto n = v.toDouble(-1);
    if (!v.isDouble() || !std::isfinite(n) || n < 0 || std::floor(n) != n || n > double(maxMoney))
        invalid();
    return std::int64_t(n);
}
QJsonValue value(std::optional<std::int64_t> n) {
    return n ? QJsonValue(qint64(*n)) : QJsonValue(QJsonValue::Null);
}
QJsonArray array(const QJsonObject &o, const char *key, std::size_t limit) {
    if (!o[key].isArray() || std::size_t(o[key].toArray().size()) > limit)
        invalid();
    return o[key].toArray();
}
QJsonObject encodeConversion(const XerSourceConversion &c) {
    QJsonArray resources;
    for (const auto &r : c.resources)
        resources.append(QJsonObject{{"fromType", q(r.fromType)},
                                     {"toType", q(r.toType)},
                                     {"workNumerator", qint64(r.workNumerator)},
                                     {"workDenominator", qint64(r.workDenominator)}});
    return {{"policy", "source-major-half-up-exact-work-v1"},
            {"targetCurrency", q(c.targetCurrency)},
            {"effectiveDate", q(c.effectiveDate)},
            {"reference", q(c.reference)},
            {"numerator", qint64(c.numerator)},
            {"denominator", qint64(c.denominator)},
            {"resources", resources}};
}
XerSourceConversion decodeConversion(const QJsonValue &v) {
    const auto o =
        object(v, {"policy", "targetCurrency", "effectiveDate", "reference", "numerator", "denominator", "resources"});
    if (string(o, "policy") != "source-major-half-up-exact-work-v1")
        invalid();
    auto positive = [](const QJsonValue &v) {
        const auto n = number(v);
        if (!n || *n < 1 || *n > 1000000000)
            invalid();
        return *n;
    };
    XerSourceConversion c;
    c.targetCurrency = string(o, "targetCurrency");
    c.effectiveDate = string(o, "effectiveDate");
    c.reference = string(o, "reference");
    c.numerator = positive(o["numerator"]);
    c.denominator = positive(o["denominator"]);
    for (const auto &v : array(o, "resources", 2)) {
        const auto r = object(v, {"fromType", "toType", "workNumerator", "workDenominator"});
        c.resources.push_back(
            {string(r, "fromType"), string(r, "toType"), positive(r["workNumerator"]), positive(r["workDenominator"])});
    }
    return c;
}
const QStringList meta{"sourceName",       "sourceSha256", "projectId", "statusAt",    "currency", "importedAt",
                       "projectionSha256", "tasks",        "resources", "assignments", "periods"};
const QStringList taskFields{"id", "name", "actualMinor"}, resourceFields{"id", "name", "type"};
const QStringList assignmentFields{"taskId",       "resourceId",   "regularWork",
                                   "overtimeWork", "regularMinor", "overtimeMinor"};
const QStringList periodFields{"taskId",        "resourceId",   "start",         "finish",     "work",
                               "actualMinor",   "laborWork",    "equipmentWork", "laborMinor", "equipmentMinor",
                               "materialMinor", "expenseMinor", "bcwp",          "bcws"};
struct Metric {
    const char *name;
    std::optional<std::int64_t> XerFinancialPeriod::*member;
};
const Metric metrics[] = {{"work", &XerFinancialPeriod::work},
                          {"actualMinor", &XerFinancialPeriod::actualMinor},
                          {"laborWork", &XerFinancialPeriod::laborWork},
                          {"equipmentWork", &XerFinancialPeriod::equipmentWork},
                          {"laborMinor", &XerFinancialPeriod::laborMinor},
                          {"equipmentMinor", &XerFinancialPeriod::equipmentMinor},
                          {"materialMinor", &XerFinancialPeriod::materialMinor},
                          {"expenseMinor", &XerFinancialPeriod::expenseMinor},
                          {"bcwp", &XerFinancialPeriod::bcwp},
                          {"bcws", &XerFinancialPeriod::bcws}};
} // namespace
QJsonObject encodeXerFinancials(const XerFinancialSnapshot &s) {
    QJsonArray tasks, resources, assignments, periods;
    for (const auto &t : s.tasks) {
        calculationCheckpoint();
        tasks.append(QJsonObject{{"id", q(t.id)}, {"name", q(t.name)}, {"actualMinor", value(t.actualMinor)}});
    }
    for (const auto &r : s.resources)
        resources.append(QJsonObject{{"id", q(r.id)}, {"name", q(r.name)}, {"type", q(r.type)}});
    for (const auto &a : s.assignments) {
        calculationCheckpoint();
        assignments.append(QJsonObject{{"taskId", q(a.taskId)},
                                       {"resourceId", q(a.resourceId)},
                                       {"regularWork", value(a.regularWork)},
                                       {"overtimeWork", value(a.overtimeWork)},
                                       {"regularMinor", value(a.regularMinor)},
                                       {"overtimeMinor", value(a.overtimeMinor)}});
    }
    for (const auto &e : s.periods) {
        calculationCheckpoint();
        QJsonObject o{
            {"taskId", q(e.taskId)}, {"resourceId", q(e.resourceId)}, {"start", q(e.start)}, {"finish", q(e.finish)}};
        for (const auto &m : metrics)
            o[m.name] = value(e.*(m.member));
        periods.append(o);
    }
    return {{"sourceName", q(s.sourceName)},
            {"sourceSha256", q(s.sourceSha256)},
            {"projectId", q(s.projectId)},
            {"statusAt", q(s.statusAt)},
            {"currency", q(s.currency)},
            {"importedAt", q(s.importedAt)},
            {"projectionSha256", q(s.projectionSha256)},
            {"tasks", tasks},
            {"resources", resources},
            {"assignments", assignments},
            {"periods", periods}};
}
XerFinancialSnapshot decodeXerFinancials(const QJsonValue &v) {
    const auto o = object(v, meta);
    XerFinancialSnapshot s;
    s.sourceName = string(o, "sourceName");
    s.sourceSha256 = string(o, "sourceSha256");
    s.projectId = string(o, "projectId");
    s.statusAt = string(o, "statusAt");
    s.currency = string(o, "currency");
    s.importedAt = string(o, "importedAt");
    s.projectionSha256 = string(o, "projectionSha256");
    for (const auto &v : array(o, "tasks", 10000)) {
        auto r = object(v, taskFields);
        s.tasks.push_back({string(r, "id"), string(r, "name"), number(r["actualMinor"])});
    }
    for (const auto &v : array(o, "resources", 10000)) {
        auto r = object(v, resourceFields);
        s.resources.push_back({string(r, "id"), string(r, "name"), string(r, "type")});
    }
    for (const auto &v : array(o, "assignments", 100000)) {
        calculationCheckpoint();
        auto r = object(v, assignmentFields);
        s.assignments.push_back({string(r, "taskId"), string(r, "resourceId"), number(r["regularWork"]),
                                 number(r["overtimeWork"]), number(r["regularMinor"]), number(r["overtimeMinor"])});
    }
    for (const auto &v : array(o, "periods", 100000)) {
        calculationCheckpoint();
        auto r = object(v, periodFields);
        XerFinancialPeriod e;
        e.taskId = string(r, "taskId");
        e.resourceId = string(r, "resourceId");
        e.start = string(r, "start");
        e.finish = string(r, "finish");
        for (const auto &m : metrics)
            e.*(m.member) = number(r[m.name]);
        s.periods.push_back(e);
    }
    validateXerFinancials(s);
    return s;
}
QJsonArray encodeXerFinancialRevisions(const std::vector<XerFinancialRevision> &revisions) {
    QJsonArray out;
    for (const auto &r : revisions) {
        calculationCheckpoint();
        QJsonArray allocations, owned;
        for (const auto &a : r.allocations) {
            QJsonArray days;
            for (const auto &d : a.days)
                days.append(QJsonObject{{"date", q(d.date)}, {"weight", qint64(d.weight)}});
            allocations.append(QJsonObject{{"periodIndex", qint64(a.periodIndex)}, {"days", days}});
        }
        for (const auto &id : r.ownedEntryIds)
            owned.append(q(id));
        auto encoded = QJsonObject{{"id", q(r.id)},
                                   {"reporter", q(r.reporter)},
                                   {"reason", q(r.reason)},
                                   {"recordedAt", q(r.recordedAt)},
                                   {"sourceDocumentId", q(r.sourceDocumentId)},
                                   {"recordedRevision", qint64(r.recordedRevision)},
                                   {"financials", encodeXerFinancials(r.financials)},
                                   {"allocations", allocations},
                                   {"ownedEntryIds", owned}};
        if (r.merge) {
            QJsonArray replaced, overrides;
            for (auto i : r.merge->replacedPeriods)
                replaced.append(qint64(i));
            for (const auto &a : r.merge->assignmentOverrides)
                overrides.append(QJsonObject{{"taskId", q(a.taskId)},
                                             {"resourceId", q(a.resourceId)},
                                             {"regularWork", value(a.regularWork)},
                                             {"overtimeWork", value(a.overtimeWork)},
                                             {"regularMinor", value(a.regularMinor)},
                                             {"overtimeMinor", value(a.overtimeMinor)}});
            encoded["merge"] = QJsonObject{{"policy", "native-ledger-union-v1"},
                                           {"baseProjectionSha256", q(r.merge->baseProjectionSha256)},
                                           {"replacedPeriods", replaced},
                                           {"assignmentOverrides", overrides}};
        }
        if (r.relink) {
            auto links = [](const auto &list) {
                QJsonArray out;
                for (const auto &l : list)
                    out.append(
                        QJsonObject{{"kind", q(l.kind)}, {"sourceId", q(l.sourceId)}, {"targetId", q(l.targetId)}});
                return out;
            };
            encoded["relink"] =
                QJsonObject{{"policy", "explicit-source-ids-v1"},
                            {"incoming", encodeXerFinancials(r.relink->incoming)},
                            {"projectName", q(r.relink->projectName)},
                            {"sourceStatusAt", r.relink->sourceStatusAt ? QJsonValue(q(*r.relink->sourceStatusAt))
                                                                        : QJsonValue(QJsonValue::Null)},
                            {"incomingLinks", links(r.relink->incomingLinks)},
                            {"previousLinks", links(r.relink->previousLinks)},
                            {"confirmed", r.relink->confirmed},
                            {"replaceMissingData", r.relink->replaceMissingData}};
            auto audit = encoded["relink"].toObject();
            if (r.relink->incomingConversion)
                audit["incomingConversion"] = encodeConversion(*r.relink->incomingConversion);
            if (r.relink->previousConversion)
                audit["previousConversion"] = encodeConversion(*r.relink->previousConversion);
            if (!r.relink->incomingCurrencySteps.empty()) {
                QJsonArray steps;
                for (const auto &rate : r.relink->incomingCurrencySteps)
                    steps.append(encodeCurrencyRate(rate));
                audit["incomingCurrencySteps"] = steps;
            }
            encoded["relink"] = audit;
        }
        out.append(encoded);
    }
    return out;
}
std::vector<XerFinancialRevision> decodeXerFinancialRevisions(const QJsonValue &v, bool allowMerge, bool allowRelink,
                                                              bool allowConversion, bool allowCurrencySteps) {
    if (!v.isArray() || v.toArray().isEmpty() || v.toArray().size() > 128)
        invalid();
    std::vector<XerFinancialRevision> out;
    std::size_t rows = 0;
    for (const auto &value : v.toArray()) {
        calculationCheckpoint();
        QStringList fields{"id",         "reporter",         "reason",
                           "recordedAt", "sourceDocumentId", "recordedRevision",
                           "financials", "allocations",      "ownedEntryIds"};
        if (allowMerge && value.isObject() && value.toObject().contains("merge"))
            fields.append("merge");
        if (allowRelink && value.isObject() && value.toObject().contains("relink"))
            fields.append("relink");
        auto o = object(value, fields);
        XerFinancialRevision r;
        r.id = string(o, "id");
        r.reporter = string(o, "reporter");
        r.reason = string(o, "reason");
        r.recordedAt = string(o, "recordedAt");
        r.sourceDocumentId = string(o, "sourceDocumentId");
        const auto revision = o["recordedRevision"].toDouble(-1);
        if (!o["recordedRevision"].isDouble() || !std::isfinite(revision) || revision < 1 ||
            revision > 9007199254740991.0 || std::floor(revision) != revision)
            invalid();
        r.recordedRevision = std::uint64_t(revision);
        if (o.contains("merge")) {
            auto m = object(o["merge"], {"policy", "baseProjectionSha256", "replacedPeriods", "assignmentOverrides"});
            if (string(m, "policy") != "native-ledger-union-v1")
                invalid();
            XerFinancialMergeAudit audit;
            audit.baseProjectionSha256 = string(m, "baseProjectionSha256");
            for (const auto &v : array(m, "replacedPeriods", 100000)) {
                const auto n = number(v);
                if (!n || *n >= 100000)
                    invalid();
                audit.replacedPeriods.push_back(std::size_t(*n));
            }
            for (const auto &v : array(m, "assignmentOverrides", 100000)) {
                auto a = object(v, assignmentFields);
                audit.assignmentOverrides.push_back({string(a, "taskId"), string(a, "resourceId"),
                                                     number(a["regularWork"]), number(a["overtimeWork"]),
                                                     number(a["regularMinor"]), number(a["overtimeMinor"])});
            }
            r.merge = std::move(audit);
        }
        r.financials = decodeXerFinancials(o["financials"]);
        if (o.contains("relink")) {
            QStringList auditFields{"policy",        "incoming",      "projectName", "sourceStatusAt",
                                    "incomingLinks", "previousLinks", "confirmed",   "replaceMissingData"};
            if (allowCurrencySteps && o["relink"].toObject().contains("incomingCurrencySteps"))
                auditFields.append("incomingCurrencySteps");
            for (const auto *key : {"incomingConversion", "previousConversion"})
                if (allowConversion && o["relink"].toObject().contains(key))
                    auditFields.append(key);
            auto a = object(o["relink"], auditFields);
            if (string(a, "policy") != "explicit-source-ids-v1" || !a["confirmed"].isBool() ||
                !a["replaceMissingData"].isBool())
                invalid();
            XerRelinkAudit audit;
            audit.incoming = decodeXerFinancials(a["incoming"]);
            audit.projectName = string(a, "projectName");
            if (!a["sourceStatusAt"].isNull())
                audit.sourceStatusAt = string(a, "sourceStatusAt");
            audit.confirmed = a["confirmed"].toBool();
            audit.replaceMissingData = a["replaceMissingData"].toBool();
            if (a.contains("incomingCurrencySteps")) {
                auto steps = array(a, "incomingCurrencySteps", 16);
                if (steps.isEmpty())
                    invalid();
                for (const auto &step : steps)
                    audit.incomingCurrencySteps.push_back(decodeCurrencyRate(step));
            }
            if (a.contains("incomingConversion"))
                audit.incomingConversion = decodeConversion(a["incomingConversion"]);
            if (a.contains("previousConversion"))
                audit.previousConversion = decodeConversion(a["previousConversion"]);
            auto links = [&](const char *key) {
                std::vector<XerIdLink> out;
                for (const auto &v : array(a, key, 20000)) {
                    auto l = object(v, {"kind", "sourceId", "targetId"});
                    out.push_back({string(l, "kind"), string(l, "sourceId"), string(l, "targetId")});
                }
                return out;
            };
            audit.incomingLinks = links("incomingLinks");
            audit.previousLinks = links("previousLinks");
            rows += audit.incoming.tasks.size() + audit.incoming.resources.size() + audit.incoming.assignments.size() +
                    audit.incoming.periods.size();
            r.relink = std::move(audit);
        }
        rows += r.financials.tasks.size() + r.financials.resources.size() + r.financials.assignments.size() +
                r.financials.periods.size();
        if (rows > 300000)
            invalid();
        std::size_t count = 0;
        for (const auto &value : array(o, "allocations", 1000)) {
            auto a = object(value, {"periodIndex", "days"});
            const auto index = number(a["periodIndex"]);
            if (!index || *index >= 100000)
                invalid();
            XerFinancialAllocation allocation;
            allocation.periodIndex = std::size_t(*index);
            for (const auto &value : array(a, "days", 1000)) {
                if (++count > 1000)
                    invalid();
                auto d = object(value, {"date", "weight"});
                const auto weight = number(d["weight"]);
                if (!weight || *weight < 1 || *weight > 1000000)
                    invalid();
                allocation.days.push_back({string(d, "date"), *weight});
            }
            r.allocations.push_back(std::move(allocation));
        }
        for (const auto &id : array(o, "ownedEntryIds", 1000)) {
            if (!id.isString())
                invalid();
            r.ownedEntryIds.push_back(id.toString().toStdString());
        }
        out.push_back(std::move(r));
    }
    return out;
}
std::string xerFinancialProjectionHash(const Project &p) {
    auto o = json::encodeProject(p, false);
    for (const auto *key : {"xerFinancials", "revision", "documentId", "derivedFrom", "xerFinancialRevisions"})
        o.remove(key);
    return QCryptographicHash::hash(QJsonDocument(o).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}
bool xerFinancialsCurrent(const Project &p) {
    return xerFinancialsMatch(p, xerFinancialProjectionHash(p));
}
} // namespace ppm
