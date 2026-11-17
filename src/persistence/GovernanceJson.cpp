#include "persistence/GovernanceJson.h"
#include "domain/Project.h"
#include <QJsonArray>
#include <cmath>
namespace jobs {
namespace {
[[noreturn]] void invalid() {
    throw AppError(ErrorCode::WORKSPACE_INVALID, {"조직 운영 JSON"});
}
void keys(const QJsonObject &o, const QStringList &names) {
    for (const auto &k : o.keys())
        if (!names.contains(k))
            invalid();
    for (const auto &k : names)
        if (!o.contains(k))
            invalid();
}
std::string text(const QJsonObject &o, const char *key) {
    const auto v = o.value(QLatin1String(key));
    if (!v.isString() || v.toString().size() > 4096 || v.toString().contains(QChar(0)))
        invalid();
    return v.toString().toUtf8().toStdString();
}
std::int64_t number(const QJsonObject &o, const char *key, std::int64_t low, std::int64_t high) {
    const auto v = o.value(QLatin1String(key));
    const auto n = v.toDouble(-1);
    if (!v.isDouble() || !std::isfinite(n) || n != std::floor(n) || n < low || n > high)
        invalid();
    return static_cast<std::int64_t>(n);
}
} // namespace
QJsonObject encodeGovernance(const Governance &g) {
    QJsonObject result;
    QJsonArray organizations;
    for (const auto &v : g.organizations)
        organizations.append(QJsonObject{{"id", QString::fromStdString(v.id)},
                                         {"name", QString::fromStdString(v.name)},
                                         {"parentId", QString::fromStdString(v.parentId)},
                                         {"kind", QString::fromStdString(v.kind)}});
    result["organizations"] = organizations;
    QJsonArray members;
    for (const auto &v : g.members)
        members.append(QJsonObject{{"id", QString::fromStdString(v.id)},
                                   {"name", QString::fromStdString(v.name)},
                                   {"organizationId", QString::fromStdString(v.organizationId)}});
    result["members"] = members;
    QJsonArray grants;
    for (const auto &v : g.grants)
        grants.append(QJsonObject{{"memberId", QString::fromStdString(v.memberId)},
                                  {"projectId", QString::fromStdString(v.projectId)},
                                  {"role", QString::fromStdString(v.role)}});
    result["grants"] = grants;
    QJsonArray portfolio;
    for (const auto &v : g.portfolio)
        portfolio.append(QJsonObject{{"projectId", QString::fromStdString(v.projectId)},
                                     {"epsId", QString::fromStdString(v.epsId)},
                                     {"ownerId", QString::fromStdString(v.ownerId)},
                                     {"portfolio", QString::fromStdString(v.portfolio)},
                                     {"currency", QString::fromStdString(v.currency)},
                                     {"budgetMinor", qint64(v.budgetMinor)},
                                     {"priority", qint64(v.priority)}});
    result["portfolio"] = portfolio;
    QJsonArray events;
    for (const auto &v : g.events)
        events.append(QJsonObject{{"id", QString::fromStdString(v.id)},
                                  {"projectId", QString::fromStdString(v.projectId)},
                                  {"actorId", QString::fromStdString(v.actorId)},
                                  {"kind", QString::fromStdString(v.kind)},
                                  {"text", QString::fromStdString(v.text)},
                                  {"createdAt", QString::fromStdString(v.createdAt)},
                                  {"targetId", QString::fromStdString(v.targetId)},
                                  {"projectRevision", qint64(v.projectRevision)}});
    result["events"] = events;
    QJsonArray risks;
    for (const auto &v : g.risks)
        risks.append(QJsonObject{{"id", QString::fromStdString(v.id)},
                                 {"projectId", QString::fromStdString(v.projectId)},
                                 {"taskId", QString::fromStdString(v.taskId)},
                                 {"name", QString::fromStdString(v.name)},
                                 {"ownerId", QString::fromStdString(v.ownerId)},
                                 {"state", QString::fromStdString(v.state)},
                                 {"group", QString::fromStdString(v.group)},
                                 {"optimistic", qint64(v.optimistic)},
                                 {"likely", qint64(v.likely)},
                                 {"pessimistic", qint64(v.pessimistic)},
                                 {"probability", qint64(v.probability)}});
    result["risks"] = risks;
    QJsonArray audit;
    for (const auto &v : g.audit)
        audit.append(QJsonObject{{"id", QString::fromStdString(v.id)},
                                 {"actorId", QString::fromStdString(v.actorId)},
                                 {"action", QString::fromStdString(v.action)},
                                 {"createdAt", QString::fromStdString(v.createdAt)},
                                 {"detail", QString::fromStdString(v.detail)}});
    result["audit"] = audit;
    return result;
}
Governance decodeGovernance(const QJsonObject &o) {
    keys(o, {"organizations", "members", "grants", "portfolio", "events", "risks", "audit"});
    Governance result;
    if (!o["organizations"].isArray() || o["organizations"].toArray().size() > 10000)
        invalid();
    for (const auto &item : o["organizations"].toArray()) {
        if (!item.isObject())
            invalid();
        const auto v = item.toObject();
        keys(v, {"id", "name", "parentId", "kind"});
        OrganizationNode entry;
        entry.id = text(v, "id");
        entry.name = text(v, "name");
        entry.parentId = text(v, "parentId");
        entry.kind = text(v, "kind");
        result.organizations.push_back(std::move(entry));
    }
    if (!o["members"].isArray() || o["members"].toArray().size() > 10000)
        invalid();
    for (const auto &item : o["members"].toArray()) {
        if (!item.isObject())
            invalid();
        const auto v = item.toObject();
        keys(v, {"id", "name", "organizationId"});
        WorkspaceMember entry;
        entry.id = text(v, "id");
        entry.name = text(v, "name");
        entry.organizationId = text(v, "organizationId");
        result.members.push_back(std::move(entry));
    }
    if (!o["grants"].isArray() || o["grants"].toArray().size() > 10000)
        invalid();
    for (const auto &item : o["grants"].toArray()) {
        if (!item.isObject())
            invalid();
        const auto v = item.toObject();
        keys(v, {"memberId", "projectId", "role"});
        ProjectGrant entry;
        entry.memberId = text(v, "memberId");
        entry.projectId = text(v, "projectId");
        entry.role = text(v, "role");
        result.grants.push_back(std::move(entry));
    }
    if (!o["portfolio"].isArray() || o["portfolio"].toArray().size() > 10000)
        invalid();
    for (const auto &item : o["portfolio"].toArray()) {
        if (!item.isObject())
            invalid();
        const auto v = item.toObject();
        keys(v, {"projectId", "epsId", "ownerId", "portfolio", "currency", "budgetMinor", "priority"});
        PortfolioEntry entry;
        entry.projectId = text(v, "projectId");
        entry.epsId = text(v, "epsId");
        entry.ownerId = text(v, "ownerId");
        entry.portfolio = text(v, "portfolio");
        entry.currency = text(v, "currency");
        entry.budgetMinor = static_cast<std::int64_t>(number(v, "budgetMinor", 0LL, 9000000000000LL));
        entry.priority = static_cast<int>(number(v, "priority", 0LL, 100LL));
        result.portfolio.push_back(std::move(entry));
    }
    if (!o["events"].isArray() || o["events"].toArray().size() > 10000)
        invalid();
    for (const auto &item : o["events"].toArray()) {
        if (!item.isObject())
            invalid();
        const auto v = item.toObject();
        keys(v, {"id", "projectId", "actorId", "kind", "text", "createdAt", "targetId", "projectRevision"});
        CollaborationEvent entry;
        entry.id = text(v, "id");
        entry.projectId = text(v, "projectId");
        entry.actorId = text(v, "actorId");
        entry.kind = text(v, "kind");
        entry.text = text(v, "text");
        entry.createdAt = text(v, "createdAt");
        entry.targetId = text(v, "targetId");
        entry.projectRevision = static_cast<std::uint64_t>(number(v, "projectRevision", 0LL, 9007199254740991LL));
        result.events.push_back(std::move(entry));
    }
    if (!o["risks"].isArray() || o["risks"].toArray().size() > 10000)
        invalid();
    for (const auto &item : o["risks"].toArray()) {
        if (!item.isObject())
            invalid();
        const auto v = item.toObject();
        keys(v, {"id", "projectId", "taskId", "name", "ownerId", "state", "group", "optimistic", "likely",
                 "pessimistic", "probability"});
        ScheduleRisk entry;
        entry.id = text(v, "id");
        entry.projectId = text(v, "projectId");
        entry.taskId = text(v, "taskId");
        entry.name = text(v, "name");
        entry.ownerId = text(v, "ownerId");
        entry.state = text(v, "state");
        entry.group = text(v, "group");
        entry.optimistic = static_cast<std::int64_t>(number(v, "optimistic", 0LL, 5000000LL));
        entry.likely = static_cast<std::int64_t>(number(v, "likely", 0LL, 5000000LL));
        entry.pessimistic = static_cast<std::int64_t>(number(v, "pessimistic", 0LL, 5000000LL));
        entry.probability = static_cast<int>(number(v, "probability", 0LL, 10000LL));
        result.risks.push_back(std::move(entry));
    }
    if (!o["audit"].isArray() || o["audit"].toArray().size() > 10000)
        invalid();
    for (const auto &item : o["audit"].toArray()) {
        if (!item.isObject())
            invalid();
        const auto v = item.toObject();
        keys(v, {"id", "actorId", "action", "createdAt", "detail"});
        result.audit.push_back(
            {text(v, "id"), text(v, "actorId"), text(v, "action"), text(v, "createdAt"), text(v, "detail")});
    }
    return result;
}
} // namespace jobs
