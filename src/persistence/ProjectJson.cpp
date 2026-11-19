#include "persistence/ResourcePlanJson.h"
#include "persistence/MaterialActualJson.h"
#include "persistence/ResourcePlanningJson.h"
#include "persistence/RemainingWorkJson.h"
#include "domain/ResourcePlan.h"
#include "domain/NativeSchema.h"
#include "persistence/ActivityJson.h"
#include "formatting/QtDiagnosticText.h"
#include "formatting/PlanTokens.h"
#include "persistence/ProjectJson.h"
#include "domain/BrandIdentifiers.h"
#include "persistence/XmlBaselineJson.h"
#include "persistence/XerFinancialJson.h"
#include "persistence/ExternalEvmJson.h"
#include "persistence/FinancialPeriodJson.h"
#include "persistence/XerScheduleJson.h"
#include "persistence/LedgerIdentityJson.h"
#include "domain/LedgerIdentity.h"
#include "domain/ProjectCurrency.h"
#include "persistence/ProjectCurrencyJson.h"
#include "persistence/PresentationJson.h"
#include "persistence/ClassificationJson.h"
#include "persistence/PeriodActualJson.h"
#include "persistence/LevelingJson.h"
#include "persistence/LevelingDecisionJson.h"
#include "persistence/ProgressPolicySelectionJson.h"
#include "domain/Classification.h"
#include "calculation/Tracking.h"
#include <QJsonArray>
#include <cmath>
#include <stdexcept>
namespace ppm::json {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
std::string str(const QString &s) {
    return s.toUtf8().toStdString();
}

void keys(const QJsonObject &o, const QStringList &allowed) {
    for (const auto &key : o.keys())
        if (!allowed.contains(key))
            throw AppError(ErrorCode::FILE_UNKNOWN_FIELD, {diagnosticArgument(key)});
}
std::string string(const QJsonObject &o, const QString &key) {
    if (!o.value(key).isString())
        throw AppError(ErrorCode::FILE_PROJECTJSON_002, {diagnosticArgument(key)});
    return str(o.value(key).toString());
}
std::optional<std::string> optionalString(const QJsonObject &o, const QString &key) {
    if (!o.contains(key))
        throw AppError(ErrorCode::FILE_PROJECTJSON_003, {diagnosticArgument(key)});
    if (o.value(key).isNull())
        return std::nullopt;
    return string(o, key);
}
std::int64_t integer(const QJsonObject &o, const QString &key, double maximum = 9007199254740991.0) {
    auto value = o.value(key);
    double n = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(n) || n < 0 || n > maximum || std::floor(n) != n)
        throw AppError(ErrorCode::FILE_PROJECTJSON_004, {diagnosticArgument(key)});
    return static_cast<std::int64_t>(n);
}
bool boolean(const QJsonObject &o, const QString &key) {
    if (!o.value(key).isBool())
        throw AppError(ErrorCode::FILE_PROJECTJSON_005, {diagnosticArgument(key)});
    return o.value(key).toBool();
}
QJsonArray array(const QJsonObject &o, const QString &key) {
    if (!o.value(key).isArray())
        throw AppError(ErrorCode::FILE_PROJECTJSON_006, {diagnosticArgument(key)});
    return o.value(key).toArray();
}
QJsonObject object(const QJsonValue &value) {
    if (!value.isObject())
        throw AppError(ErrorCode::FILE_PROJECTJSON_007, {});
    return value.toObject();
}
QJsonArray strings(const std::vector<std::string> &values) {
    QJsonArray a;
    for (const auto &s : values)
        a.append(qs(s));
    return a;
}
std::vector<std::string> readStrings(const QJsonObject &o, const QString &key) {
    std::vector<std::string> result;
    for (const auto &v : array(o, key)) {
        if (!v.isString())
            throw AppError(ErrorCode::FILE_PROJECTJSON_008, {diagnosticArgument(key)});
        result.push_back(str(v.toString()));
    }
    return result;
}
QJsonValue nullable(const std::optional<std::string> &value) {
    return value ? QJsonValue(qs(*value)) : QJsonValue(QJsonValue::Null);
}
QJsonValue money(const std::optional<std::int64_t> &v) {
    return v ? QJsonValue(double(*v)) : QJsonValue(QJsonValue::Null);
}
std::optional<std::int64_t> readMoney(const QJsonObject &o, const QString &key, double limit = double(maxMoney)) {
    if (!o.contains(key))
        throw AppError(ErrorCode::FILE_PROJECTJSON_009, {diagnosticArgument(key)});
    return o[key].isNull() ? std::optional<std::int64_t>{} : integer(o, key, limit);
}
QStringList planKeys(int schema) {
    QStringList result{"name", "startDate", "calendar", "tasks"};
    if (schema >= 2)
        result.append({"holidays", "calculationProfile"});
    if (schema >= 3)
        result.append("wbsNodes");
    if (schema >= 5)
        result.append({"resources", "assignments"});
    if (schema >= 6)
        result.append({"currency", "costProfile", "taskCosts"});
    if (schema >= 7)
        result.append("calendars");
    if (schema >= 10)
        result.append({"customFields", "codeTypes"});
    if (schema >= 12)
        result.append("planLeveling");
    if (schema >= 16)
        result.append({"projectFields", "resourceFields", "projectAttributes"});
    if (schema >= 19)
        result.append("activities");
    if (schema >= 20)
        result.append("resourcePlan");
    if (schema >= 37)
        result.append("assignmentFields");
    if (schema >= 41)
        result.append("xmlWbs");
    if (schema >= 43)
        result.append("taskSummaryAttributes");
    if (schema >= 46)
        result.append("xmlFormulaCalendarId");
    if (schema >= 75)
        result.append("xmlBaselines");
    return result;
}
QJsonObject encodePlan(const PlanInput &p) {
    QJsonArray tasks, nodes;
    for (const auto &t : p.tasks)
        tasks.append(QJsonObject{{"id", qs(t.id)},
                                 {"name", qs(t.name)},
                                 {"wbsId", qs(t.wbsId)},
                                 {"durationMinutes", t.durationMinutes ? QJsonValue(double(*t.durationMinutes))
                                                                       : QJsonValue(QJsonValue::Null)},
                                 {"predecessors", strings(t.predecessors)}});
    if (isPdm(p))
        for (int i = 0; i < tasks.size(); ++i) {
            auto t = tasks[i].toObject();
            const auto &task = p.tasks[std::size_t(i)];
            QJsonArray edges;
            for (const auto &d : task.dependencyRules)
                edges.append(QJsonObject{{"predecessorId", qs(d.predecessorId)},
                                         {"type", qs(enumToken(d.type))},
                                         {"lagMinutes", double(d.lagMinutes)}});
            t["dependencyRules"] = edges;
            t["calendarId"] = qs(task.calendarId);
            tasks[i] = t;
            if (hasConstraints(p)) {
                t["constraintType"] = qs(enumToken(task.constraintType));
                t["constraintAt"] = nullable(task.constraintAt);
                t["deadlineAt"] = nullable(task.deadlineAt);
            }
            if (isEffort(p)) {
                QJsonArray gaps;
                for (const auto &b : task.breaks)
                    gaps.append(QJsonObject{{"start", qs(b.start)}, {"finish", qs(b.finish)}});
                t["taskType"] = qs(enumToken(task.taskType));
                t["workDriven"] = task.workDriven;
                t["workUnitMinutes"] = money(task.workUnitMinutes);
                t["breaks"] = gaps;
            }
            tasks[i] = t;
        }
    for (int i = 0; i < tasks.size(); ++i)
        if (!p.tasks[std::size_t(i)].attributes.empty()) {
            auto task = tasks[i].toObject();
            task["attributes"] = encodeAttributes(p.tasks[std::size_t(i)].attributes);
            tasks[i] = task;
        }
    for (const auto &n : p.wbsNodes) {
        QJsonObject node{{"id", qs(n.id)}, {"name", qs(n.name)}, {"parentId", qs(n.parentId)}};
        if (!n.attributes.empty())
            node["attributes"] = encodeAttributes(n.attributes);
        nodes.append(node);
    }
    QJsonArray resources, assignments;
    for (const auto &r : p.resources) {
        QJsonArray exceptions;
        for (const auto &d : r.exceptions)
            exceptions.append(QJsonObject{{"date", qs(d.date)},
                                          {"units", d.units ? QJsonValue(*d.units) : QJsonValue(QJsonValue::Null)}});
        QJsonArray rates;
        for (const auto &rate : r.rates)
            rates.append(
                QJsonObject{{"effectiveDate", qs(rate.effectiveDate)}, {"hourlyMinor", money(rate.hourlyMinor)}});
        resources.append(QJsonObject{{"id", qs(r.id)},
                                     {"name", qs(r.name)},
                                     {"type", qs(r.type)},
                                     {"maxUnits", r.maxUnits ? QJsonValue(*r.maxUnits) : QJsonValue(QJsonValue::Null)},
                                     {"exceptions", exceptions},
                                     {"rates", rates}});
        if (r.availableFrom || r.availableTo || !r.availability.empty()) {
            auto value = resources.last().toObject();
            value["availableFrom"] = r.availableFrom ? QJsonValue(qs(*r.availableFrom)) : QJsonValue(QJsonValue::Null);
            value["availableTo"] = r.availableTo ? QJsonValue(qs(*r.availableTo)) : QJsonValue(QJsonValue::Null);
            QJsonArray periods;
            for (const auto &v : r.availability)
                periods.append(QJsonObject{{"from", v.from ? QJsonValue(qs(*v.from)) : QJsonValue(QJsonValue::Null)},
                                           {"to", v.to ? QJsonValue(qs(*v.to)) : QJsonValue(QJsonValue::Null)},
                                           {"units", v.units ? QJsonValue(*v.units) : QJsonValue(QJsonValue::Null)}});
            value["availability"] = periods;
            resources[resources.size() - 1] = value;
        }
        if (isEffort(p)) {
            auto resource = resources.last().toObject();
            resource["calendarId"] = qs(r.calendarId);
            resources[resources.size() - 1] = resource;
        }
    }
    for (qsizetype i = 0; i < resources.size(); ++i)
        if (!p.resources[std::size_t(i)].attributes.empty()) {
            auto resource = resources[i].toObject();
            resource["attributes"] = encodeAttributes(p.resources[std::size_t(i)].attributes);
            resources[i] = resource;
        }
    for (qsizetype i = 0; i < resources.size(); ++i)
        if (p.resources[std::size_t(i)].productivity) {
            auto resource = resources[i].toObject();
            resource["productivity"] = encodeProductivityRate(*p.resources[std::size_t(i)].productivity);
            resources[i] = resource;
        }
    for (const auto &a : p.assignments) {
        QJsonObject value{{"taskId", qs(a.taskId)}, {"resourceId", qs(a.resourceId)}, {"units", a.units}};
        if (!a.periods.empty()) {
            QJsonArray periods;
            for (const auto &v : a.periods)
                periods.append(QJsonObject{
                    {"fromMinute", double(v.fromMinute)}, {"toMinute", double(v.toMinute)}, {"units", v.units}});
            value["periods"] = periods;
        }
        if (a.nativeCurve)
            value["nativeCurve"] = encodeNativeCurve(*a.nativeCurve);
        if (a.productivityEstimate)
            value["productivityEstimate"] = encodeProductivityEstimate(*a.productivityEstimate);
        if (!a.attributes.empty())
            value["attributes"] = encodeAttributes(a.attributes);
        if (!a.rollDownMissing.empty())
            value["rollDownMissing"] = strings(a.rollDownMissing);
        assignments.append(value);
    }
    QJsonArray costs;
    for (const auto &c : p.taskCosts)
        costs.append(QJsonObject{{"taskId", qs(c.taskId)},
                                 {"fixedMinor", money(c.fixedMinor)},
                                 {"accrual", qs(c.accrual)},
                                 {"credit", qs(c.credit)}});
    QJsonObject result{{"name", qs(p.name)},
                       {"startDate", qs(p.startDate)},
                       {"calendar", qs(p.calendar)},
                       {"tasks", tasks},
                       {"holidays", strings(p.holidays)},
                       {"calculationProfile", qs(p.calculationProfile)},
                       {"wbsNodes", nodes},
                       {"resources", resources},
                       {"assignments", assignments},
                       {"currency", qs(p.currency)},
                       {"costProfile", qs(p.costProfile)},
                       {"taskCosts", costs}};
    if (isPdm(p)) {
        QJsonArray calendars;
        for (const auto &c : p.calendars) {
            QJsonArray weekdays, periods;
            for (int day : c.weekdays)
                weekdays.append(day);
            for (const auto &s : c.periods)
                periods.append(QJsonObject{{"begin", s.begin}, {"end", s.end}});
            calendars.append(QJsonObject{{"id", qs(c.id)},
                                         {"name", qs(c.name)},
                                         {"weekdays", weekdays},
                                         {"periods", periods},
                                         {"holidays", strings(c.holidays)}});
        }
        result["calendars"] = calendars;
    }
    if (hasClassification(p)) {
        const auto classification = encodeClassification(p);
        for (auto it = classification.begin(); it != classification.end(); ++it)
            result[it.key()] = it.value();
    }
    if (!p.projectFields.empty())
        result["projectFields"] = encodeCustomFields(p.projectFields);
    if (!p.resourceFields.empty())
        result["resourceFields"] = encodeCustomFields(p.resourceFields);
    if (!p.assignmentFields.empty())
        result["assignmentFields"] = encodeCustomFields(p.assignmentFields);
    if (!p.projectAttributes.empty())
        result["projectAttributes"] = encodeAttributes(p.projectAttributes);
    if (hasResourcePlan(p))
        result["resourcePlan"] = encodeResourcePlan(p.resourcePlan);
    if (!p.activities.empty())
        result["activities"] = encodeActivities(p.activities);
    if (!p.taskSummaryAttributes.empty())
        result["taskSummaryAttributes"] = encodeAttributes(p.taskSummaryAttributes);
    if (p.xmlWbs) {
        QJsonArray values;
        for (const auto &v : p.xmlWbs->values)
            values.append(QJsonObject{{"kind", qs(v.kind)}, {"id", qs(v.id)}, {"code", qs(v.code)}});
        result["xmlWbs"] = QJsonObject{{"prefix", qs(p.xmlWbs->prefix)},
                                       {"generate", p.xmlWbs->generate},
                                       {"verifyUnique", p.xmlWbs->verifyUnique},
                                       {"masks", encodeXmlCodeMasks(p.xmlWbs->masks)},
                                       {"values", values}};
    }
    if (p.planLeveling)
        result["planLeveling"] = encodeLeveling(*p.planLeveling);
    if (!p.xmlFormulaCalendarId.empty())
        result["xmlFormulaCalendarId"] = qs(p.xmlFormulaCalendarId);
    if (p.xmlBaselines)
        result["xmlBaselines"] = encodeXmlBaselines(*p.xmlBaselines);
    return result;
}
PlanInput decodePlan(const QJsonObject &o, int schema) {
    keys(o, planKeys(schema));
    PlanInput p;
    if (schema >= 75 && o.contains("xmlBaselines"))
        p.xmlBaselines = decodeXmlBaselines(object(o["xmlBaselines"]), schema >= 76, schema >= 77);
    if (schema >= 46 && o.contains("xmlFormulaCalendarId"))
        p.xmlFormulaCalendarId = string(o, "xmlFormulaCalendarId");
    if (schema >= 43 && o.contains("taskSummaryAttributes"))
        p.taskSummaryAttributes = decodeAttributes(o["taskSummaryAttributes"]);
    if (schema >= 41 && o.contains("xmlWbs")) {
        const auto w = object(o["xmlWbs"]);
        keys(w, {"prefix", "generate", "verifyUnique", "masks", "values"});
        XmlWbsCodes codes;
        codes.prefix = string(w, "prefix");
        codes.generate = boolean(w, "generate");
        codes.verifyUnique = boolean(w, "verifyUnique");
        codes.masks = decodeXmlCodeMasks(w["masks"]);
        const auto values = array(w, "values");
        if (values.size() > 20000)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"WBS 코드 개수 한도 초과"});
        for (const auto &entry : values) {
            const auto v = object(entry);
            keys(v, {"kind", "id", "code"});
            codes.values.push_back({string(v, "kind"), string(v, "id"), string(v, "code")});
        }
        p.xmlWbs = std::move(codes);
    }
    if (schema >= 37 && o.contains("assignmentFields"))
        p.assignmentFields = decodeCustomFields(o["assignmentFields"], schema);
    if (schema >= 20 && o.contains("resourcePlan"))
        p.resourcePlan = decodeResourcePlan(o["resourcePlan"], schema);
    if (schema >= 19 && o.contains("activities"))
        p.activities = decodeActivities(o["activities"]);
    if (schema >= 12 && o.contains("planLeveling"))
        p.planLeveling = decodeLeveling(o["planLeveling"]);
    if (schema >= 10 && (o.contains("customFields") || o.contains("codeTypes")))
        decodeClassification(p, o, schema);
    if (schema >= 16) {
        if (o.contains("projectFields"))
            p.projectFields = decodeCustomFields(o["projectFields"], schema);
        if (o.contains("resourceFields"))
            p.resourceFields = decodeCustomFields(o["resourceFields"], schema);
        if (o.contains("projectAttributes"))
            p.projectAttributes = decodeAttributes(o["projectAttributes"]);
    }
    p.name = string(o, "name");
    p.startDate = string(o, "startDate");
    p.calendar = string(o, "calendar");
    if (schema >= 2) {
        p.holidays = readStrings(o, "holidays");
        p.calculationProfile = string(o, "calculationProfile");
    }
    const bool pdm = schema >= 7 && isPdm(p);
    const bool constrained = schema >= 8 && hasConstraints(p);
    const bool effort = schema >= 9 && isEffort(p);
    if (isEffort(p) && !effort)
        throw AppError(ErrorCode::FILE_PROJECTJSON_010, {});
    if (isPdm(p) && !pdm)
        throw AppError(ErrorCode::FILE_PROJECTJSON_011, {});
    if (hasConstraints(p) && !constrained)
        throw AppError(ErrorCode::FILE_PROJECTJSON_012, {});
    if (!pdm && o.contains("calendars"))
        throw AppError(ErrorCode::FILE_PROJECTJSON_013, {});
    if (pdm)
        for (const auto &v : array(o, "calendars")) {
            auto c = object(v);
            keys(c, {"id", "name", "weekdays", "periods", "holidays"});
            TaskCalendar cal;
            cal.id = string(c, "id");
            cal.name = string(c, "name");
            cal.weekdays.clear();
            cal.periods.clear();
            for (const auto &day : array(c, "weekdays")) {
                QJsonObject d{{"day", day}};
                cal.weekdays.push_back(int(integer(d, "day", 7)));
            }
            for (const auto &period : array(c, "periods")) {
                auto s = object(period);
                keys(s, {"begin", "end"});
                cal.periods.push_back({int(integer(s, "begin", 1440)), int(integer(s, "end", 1440))});
            }
            cal.holidays = readStrings(c, "holidays");
            p.calendars.push_back(std::move(cal));
        }
    if (schema >= 3)
        for (const auto &v : array(o, "wbsNodes")) {
            auto n = object(v);
            auto fields = QStringList{"id", "name", "parentId"};
            if (schema >= 43)
                fields.append("attributes");
            keys(n, fields);
            WbsNode node{string(n, "id"), string(n, "name"), string(n, "parentId")};
            if (schema >= 43 && n.contains("attributes"))
                node.attributes = decodeAttributes(n["attributes"]);
            p.wbsNodes.push_back(std::move(node));
        }
    if (schema >= 5) {
        for (const auto &v : array(o, "resources")) {
            auto r = object(v);
            QStringList fields{"id", "name", "type", "maxUnits", "exceptions"};
            if (schema >= 79)
                fields.append({"availableFrom", "availableTo", "availability"});
            if (schema >= 6)
                fields.append("rates");
            if (effort)
                fields.append("calendarId");
            if (schema >= 16)
                fields.append("attributes");
            if (schema >= 36)
                fields.append("productivity");
            keys(r, fields);
            Resource resource;
            if (schema >= 79) {
                if (r.contains("availableFrom") && !r["availableFrom"].isNull())
                    resource.availableFrom = string(r, "availableFrom");
                if (r.contains("availableTo") && !r["availableTo"].isNull())
                    resource.availableTo = string(r, "availableTo");
                if (r.contains("availability")) {
                    if (array(r, "availability").size() > 10000)
                        throw AppError(ErrorCode::RESOURCE_EXCEPTION_INVALID);
                    for (const auto &entry : array(r, "availability")) {
                        const auto v = object(entry);
                        keys(v, {"from", "to", "units"});
                        if (!v.contains("from") || !v.contains("to") || !v.contains("units"))
                            throw AppError(ErrorCode::RESOURCE_EXCEPTION_INVALID);
                        resource.availability.push_back(
                            {v["from"].isNull() ? std::optional<std::string>{} : string(v, "from"),
                             v["to"].isNull() ? std::optional<std::string>{} : string(v, "to"),
                             v["units"].isNull() ? std::optional<int>{} : int(integer(v, "units", 100000))});
                    }
                }
            }
            if (schema >= 36 && r.contains("productivity"))
                resource.productivity = decodeProductivityRate(r["productivity"]);
            if (schema >= 16 && r.contains("attributes"))
                resource.attributes = decodeAttributes(r["attributes"]);
            resource.id = string(r, "id");
            resource.name = string(r, "name");
            resource.type = string(r, "type");
            if (effort)
                resource.calendarId = string(r, "calendarId");
            if (!r.contains("maxUnits"))
                throw AppError(ErrorCode::FILE_PROJECTJSON_014, {});
            resource.maxUnits = r["maxUnits"].isNull() ? std::optional<int>{} : int(integer(r, "maxUnits", 100000));
            for (const auto &x : array(r, "exceptions")) {
                auto d = object(x);
                keys(d, {"date", "units"});
                if (!d.contains("units"))
                    throw AppError(ErrorCode::FILE_PROJECTJSON_015, {});
                resource.exceptions.push_back(
                    {string(d, "date"), d["units"].isNull() ? std::optional<int>{} : int(integer(d, "units", 100000))});
            }
            if (schema >= 6)
                for (const auto &x : array(r, "rates")) {
                    auto rate = object(x);
                    keys(rate, {"effectiveDate", "hourlyMinor"});
                    resource.rates.push_back(
                        {string(rate, "effectiveDate"), readMoney(rate, "hourlyMinor", 1000000000)});
                }
            p.resources.push_back(std::move(resource));
        }
        std::size_t periodCount = 0;
        for (const auto &v : array(o, "assignments")) {
            auto a = object(v);
            QStringList fields{"taskId", "resourceId", "units"};
            if (schema >= 17)
                fields.append("periods");
            if (schema >= 36)
                fields.append({"nativeCurve", "productivityEstimate"});
            if (schema >= 37)
                fields.append("attributes");
            if (schema >= 42)
                fields.append("rollDownMissing");
            keys(a, fields);
            Assignment value{string(a, "taskId"), string(a, "resourceId"), int(integer(a, "units", 100000))};
            if (schema >= 42 && a.contains("rollDownMissing")) {
                if (array(a, "rollDownMissing").size() > 64)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID);
                value.rollDownMissing = readStrings(a, "rollDownMissing");
            }
            if (schema >= 37 && a.contains("attributes"))
                value.attributes = decodeAttributes(a["attributes"]);
            if (schema >= 36 && a.contains("nativeCurve"))
                value.nativeCurve = decodeNativeCurve(a["nativeCurve"]);
            if (schema >= 36 && a.contains("productivityEstimate"))
                value.productivityEstimate = decodeProductivityEstimate(a["productivityEstimate"]);
            if (a.contains("periods")) {
                const auto periods = array(a, "periods");
                periodCount += std::size_t(periods.size());
                if (periods.size() > 1024 || periodCount > 100000)
                    throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID, {"구간 수 초과"});
                for (const auto &period : periods) {
                    const auto v = object(period);
                    keys(v, {"fromMinute", "toMinute", "units"});
                    value.periods.push_back({integer(v, "fromMinute", 5000000), integer(v, "toMinute", 5000000),
                                             int(integer(v, "units", 100000))});
                }
            }
            p.assignments.push_back(std::move(value));
        }
    }
    if (schema >= 6) {
        p.currency = string(o, "currency");
        p.costProfile = string(o, "costProfile");
        for (const auto &v : array(o, "taskCosts")) {
            auto c = object(v);
            keys(c, {"taskId", "fixedMinor", "accrual", "credit"});
            p.taskCosts.push_back(
                {string(c, "taskId"), readMoney(c, "fixedMinor"), string(c, "accrual"), string(c, "credit")});
        }
    }
    for (const auto &v : array(o, "tasks")) {
        auto t = object(v);
        QStringList fields{"id", "name"};
        if (schema >= 2)
            fields.append({"durationMinutes", "predecessors"});
        if (schema >= 3)
            fields.append("wbsId");
        if (pdm)
            fields.append({"dependencyRules", "calendarId"});
        if (constrained)
            fields.append({"constraintType", "constraintAt", "deadlineAt"});
        if (effort)
            fields.append({"taskType", "workDriven", "workUnitMinutes", "breaks"});
        if (schema >= 10)
            fields.append("attributes");
        keys(t, fields);
        Task task{string(t, "id"), string(t, "name"), std::nullopt, {}};
        if (schema >= 2) {
            if (!t.contains("durationMinutes"))
                throw AppError(ErrorCode::FILE_PROJECTJSON_016, {});
            if (!t.value("durationMinutes").isNull())
                task.durationMinutes = integer(t, "durationMinutes", 5000000);
            task.predecessors = readStrings(t, "predecessors");
        }
        if (schema >= 3)
            task.wbsId = string(t, "wbsId");
        if (pdm) {
            task.calendarId = string(t, "calendarId");
            for (const auto &edgeValue : array(t, "dependencyRules")) {
                auto d = object(edgeValue);
                keys(d, {"predecessorId", "type", "lagMinutes"});
                const auto lag = d["lagMinutes"].toDouble(9000000);
                if (!d["lagMinutes"].isDouble() || !std::isfinite(lag) || std::floor(lag) != lag ||
                    std::abs(lag) > 5000000)
                    throw AppError(ErrorCode::FILE_PROJECTJSON_017, {});
                task.dependencyRules.push_back(
                    {string(d, "predecessorId"), dependencyTypeFromToken(string(d, "type")), std::int64_t(lag)});
            }
        }
        if (constrained) {
            task.constraintType = constraintTypeFromToken(string(t, "constraintType"));
            task.constraintAt = optionalString(t, "constraintAt");
            task.deadlineAt = optionalString(t, "deadlineAt");
        }
        if (effort) {
            task.taskType = taskTypeFromToken(string(t, "taskType"));
            task.workDriven = boolean(t, "workDriven");
            task.workUnitMinutes = readMoney(t, "workUnitMinutes", double(maxWork));
            for (const auto &gap : array(t, "breaks")) {
                auto b = object(gap);
                keys(b, {"start", "finish"});
                task.breaks.push_back({string(b, "start"), string(b, "finish")});
            }
        }
        if (schema >= 10 && t.contains("attributes"))
            task.attributes = decodeAttributes(t.value("attributes"));
        p.tasks.push_back(std::move(task));
    }
    if (schema < 24 &&
        (hasWorkAssignmentPeriods(p) || (p.planLeveling && p.planLeveling->policy == "serial-effort-work-periods-v1")))
        throw AppError(ErrorCode::FILE_VERSION_UNSUPPORTED);
    if (schema < 40 && hasDateTimeFields(p))
        throw AppError(ErrorCode::FILE_VERSION_UNSUPPORTED);
    return p;
}
QJsonObject encodeRow(const TaskSchedule &r) {
    return {{"taskId", qs(r.taskId)},
            {"start", qs(r.start)},
            {"finish", qs(r.finish)},
            {"earlyStartMinutes", double(r.earlyStartMinutes)},
            {"earlyFinishMinutes", double(r.earlyFinishMinutes)},
            {"lateStartMinutes", double(r.lateStartMinutes)},
            {"lateFinishMinutes", double(r.lateFinishMinutes)},
            {"totalFloatMinutes", double(r.totalFloatMinutes)},
            {"freeFloatMinutes", double(r.freeFloatMinutes)},
            {"critical", r.critical},
            {"drivingPredecessors", strings(r.drivingPredecessors)},
            {"remainingStart", nullable(r.remainingStart)},
            {"completedActual", r.completedActual}};
}
TaskSchedule decodeRow(const QJsonObject &o) {
    keys(o, {"taskId", "start", "finish", "earlyStartMinutes", "earlyFinishMinutes", "lateStartMinutes",
             "lateFinishMinutes", "totalFloatMinutes", "freeFloatMinutes", "critical", "drivingPredecessors",
             "remainingStart", "completedActual"});
    TaskSchedule r;
    r.taskId = string(o, "taskId");
    r.start = string(o, "start");
    r.finish = string(o, "finish");
    r.earlyStartMinutes = integer(o, "earlyStartMinutes");
    r.earlyFinishMinutes = integer(o, "earlyFinishMinutes");
    r.lateStartMinutes = integer(o, "lateStartMinutes");
    r.lateFinishMinutes = integer(o, "lateFinishMinutes");
    r.totalFloatMinutes = integer(o, "totalFloatMinutes");
    r.freeFloatMinutes = integer(o, "freeFloatMinutes");
    r.critical = boolean(o, "critical");
    r.drivingPredecessors = readStrings(o, "drivingPredecessors");
    r.remainingStart = optionalString(o, "remainingStart");
    r.completedActual = boolean(o, "completedActual");
    return r;
}
QJsonObject encodeBaseline(const Baseline &b) {
    QJsonArray schedule;
    for (const auto &row : b.schedule)
        schedule.append(encodeRow(row));
    return {{"id", qs(b.id)},
            {"name", qs(b.name)},
            {"approvedBy", qs(b.approvedBy)},
            {"reason", qs(b.reason)},
            {"createdAt", qs(b.createdAt)},
            {"sourceDocumentId", qs(b.sourceDocumentId)},
            {"inputRevision", double(b.inputRevision)},
            {"version", double(b.version)},
            {"input", encodePlan(b.input)},
            {"engineVersion", qs(b.engineVersion)},
            {"projectFinish", qs(b.projectFinish)},
            {"schedule", schedule}};
}
Baseline decodeBaseline(const QJsonObject &o, int schema) {
    keys(o, {"id", "name", "approvedBy", "reason", "createdAt", "sourceDocumentId", "inputRevision", "version", "input",
             "engineVersion", "projectFinish", "schedule"});
    Baseline b;
    b.id = string(o, "id");
    b.name = string(o, "name");
    b.approvedBy = string(o, "approvedBy");
    b.reason = string(o, "reason");
    b.createdAt = string(o, "createdAt");
    b.sourceDocumentId = string(o, "sourceDocumentId");
    b.inputRevision = std::uint64_t(integer(o, "inputRevision"));
    b.version = std::uint64_t(integer(o, "version"));
    b.input = decodePlan(object(o.value("input")), schema);
    b.engineVersion = canonicalEngineIdentifier(string(o, "engineVersion"));
    b.projectFinish = string(o, "projectFinish");
    for (const auto &v : array(o, "schedule"))
        b.schedule.push_back(decodeRow(object(v)));
    return b;
}
QJsonObject encodeEntry(const ProgressEntry &e) {
    QJsonObject out{{"id", qs(e.id)},
                    {"taskId", qs(e.taskId)},
                    {"statusAt", qs(e.statusAt)},
                    {"recordedAt", qs(e.recordedAt)},
                    {"reporter", qs(e.reporter)},
                    {"reason", qs(e.reason)},
                    {"previousEntryId", qs(e.previousEntryId)},
                    {"recordedRevision", double(e.recordedRevision)},
                    {"sourceDocumentId", qs(e.sourceDocumentId)},
                    {"planDurationMinutes",
                     e.planDurationMinutes ? QJsonValue(double(*e.planDurationMinutes)) : QJsonValue(QJsonValue::Null)},
                    {"actualStart", nullable(e.actualStart)},
                    {"actualFinish", nullable(e.actualFinish)},
                    {"actualDurationMinutes", double(e.actualDurationMinutes)},
                    {"remainingMinutes", double(e.remainingMinutes)}};
    if (e.remainingWorkUnitMinutes)
        out["remainingWorkUnitMinutes"] = double(*e.remainingWorkUnitMinutes);
    return out;
}
ProgressEntry decodeEntry(const QJsonObject &o, int schema) {
    QStringList fields{"id",
                       "taskId",
                       "statusAt",
                       "recordedAt",
                       "reporter",
                       "reason",
                       "previousEntryId",
                       "recordedRevision",
                       "sourceDocumentId",
                       "planDurationMinutes",
                       "actualStart",
                       "actualFinish",
                       "actualDurationMinutes",
                       "remainingMinutes"};
    if (schema >= 9)
        fields.append("remainingWorkUnitMinutes");
    if (schema >= 18)
        fields.append({"printPreferences", "networkPositions"});
    keys(o, fields);
    ProgressEntry e;
    e.id = string(o, "id");
    e.taskId = string(o, "taskId");
    e.statusAt = string(o, "statusAt");
    e.recordedAt = string(o, "recordedAt");
    e.reporter = string(o, "reporter");
    e.reason = string(o, "reason");
    e.previousEntryId = string(o, "previousEntryId");
    e.recordedRevision = std::uint64_t(integer(o, "recordedRevision"));
    e.sourceDocumentId = string(o, "sourceDocumentId");
    if (!o.contains("planDurationMinutes"))
        throw AppError(ErrorCode::FILE_PROJECTJSON_018, {});
    if (!o.value("planDurationMinutes").isNull())
        e.planDurationMinutes = integer(o, "planDurationMinutes", 5000000);
    e.actualStart = optionalString(o, "actualStart");
    e.actualFinish = optionalString(o, "actualFinish");
    if (o.contains("remainingWorkUnitMinutes"))
        e.remainingWorkUnitMinutes = integer(o, "remainingWorkUnitMinutes", double(maxWork));
    e.actualDurationMinutes = integer(o, "actualDurationMinutes", 5000000);
    e.remainingMinutes = integer(o, "remainingMinutes", 5000000);
    return e;
}
QJsonObject encodeScenario(const LevelingScenario &s) {
    QJsonArray entries;
    for (const auto &e : s.progress)
        entries.append(encodeEntry(e));
    QJsonObject result{{"id", qs(s.id)},
                       {"name", qs(s.name)},
                       {"createdAt", qs(s.createdAt)},
                       {"sourceDocumentId", qs(s.sourceDocumentId)},
                       {"inputRevision", double(s.inputRevision)},
                       {"policy", qs(s.policy)},
                       {"priority", qs(s.priority)},
                       {"horizonWorkDays", s.horizonWorkDays},
                       {"input", encodePlan(s.input)},
                       {"statusAt", nullable(s.statusAt)},
                       {"progress", entries}};
    if (s.progressPolicy)
        result["progressPolicy"] = encodeProgressPolicySelection(*s.progressPolicy);
    if (!s.materialActuals.empty())
        result["materialActuals"] = encodeMaterialActuals(s.materialActuals);
    if (!s.remainingWork.empty())
        result["remainingWork"] = encodeRemainingWork(s.remainingWork);
    return result;
}
LevelingScenario decodeScenario(const QJsonObject &o, int schema) {
    QStringList fields{
        "id",    "name",     "createdAt", "sourceDocumentId", "inputRevision", "policy", "priority", "horizonWorkDays",
        "input", "statusAt", "progress"};
    if (schema >= 14)
        fields.append("progressPolicy");
    if (schema >= 18)
        fields.append({"printPreferences", "networkPositions"});
    if (schema >= 22)
        fields.append("remainingWork");
    if (schema >= 83)
        fields.append("materialActuals");
    keys(o, fields);
    LevelingScenario s;
    if (schema >= 83 && o.contains("materialActuals"))
        s.materialActuals = decodeMaterialActuals(o["materialActuals"]);
    s.id = string(o, "id");
    s.name = string(o, "name");
    s.createdAt = string(o, "createdAt");
    s.sourceDocumentId = string(o, "sourceDocumentId");
    s.inputRevision = std::uint64_t(integer(o, "inputRevision"));
    s.policy = string(o, "policy");
    s.priority = string(o, "priority");
    s.horizonWorkDays = int(integer(o, "horizonWorkDays", 5200));
    s.input = decodePlan(object(o["input"]), schema);
    s.statusAt = optionalString(o, "statusAt");
    if (schema >= 22 && o.contains("remainingWork"))
        s.remainingWork = decodeRemainingWork(o["remainingWork"]);
    if (schema >= 14 && o.contains("progressPolicy"))
        s.progressPolicy = decodeProgressPolicySelection(o["progressPolicy"]);
    for (const auto &v : array(o, "progress"))
        s.progress.push_back(decodeEntry(object(v), schema));
    return s;
}
QJsonObject encodeCostEntry(const CostEntry &e) {
    return {{"id", qs(e.id)},
            {"taskId", qs(e.taskId)},
            {"statusAt", qs(e.statusAt)},
            {"recordedAt", qs(e.recordedAt)},
            {"reporter", qs(e.reporter)},
            {"reason", qs(e.reason)},
            {"previousEntryId", qs(e.previousEntryId)},
            {"sourceDocumentId", qs(e.sourceDocumentId)},
            {"recordedRevision", double(e.recordedRevision)},
            {"currency", qs(e.currency)},
            {"actualMinor", money(e.actualMinor)},
            {"physicalBasisPoints",
             e.physicalBasisPoints ? QJsonValue(*e.physicalBasisPoints) : QJsonValue(QJsonValue::Null)}};
}
CostEntry decodeCostEntry(const QJsonObject &o) {
    keys(o, {"id", "taskId", "statusAt", "recordedAt", "reporter", "reason", "previousEntryId", "sourceDocumentId",
             "recordedRevision", "currency", "actualMinor", "physicalBasisPoints"});
    CostEntry e;
    e.id = string(o, "id");
    e.taskId = string(o, "taskId");
    e.statusAt = string(o, "statusAt");
    e.recordedAt = string(o, "recordedAt");
    e.reporter = string(o, "reporter");
    e.reason = string(o, "reason");
    e.previousEntryId = string(o, "previousEntryId");
    e.sourceDocumentId = string(o, "sourceDocumentId");
    e.recordedRevision = std::uint64_t(integer(o, "recordedRevision"));
    e.currency = string(o, "currency");
    e.actualMinor = readMoney(o, "actualMinor");
    if (!o.contains("physicalBasisPoints"))
        throw AppError(ErrorCode::FILE_PROJECTJSON_019, {});
    if (!o["physicalBasisPoints"].isNull())
        e.physicalBasisPoints = int(integer(o, "physicalBasisPoints", 10000));
    return e;
}
} // namespace
QJsonObject encodePlanInput(const PlanInput &p) {
    return encodePlan(p);
}
PlanInput decodePlanInput(const QJsonObject &p, int schema) {
    return decodePlan(p, schema);
}
QJsonObject encodeProject(const Project &p, bool includeXerFinancials) {
    auto o = encodePlan(p);
    if (!p.xmlBaselineMerges.empty())
        o["xmlBaselineMerges"] = encodeXmlBaselineMerges(p.xmlBaselineMerges);
    if (includeXerFinancials && !p.currencyConversions.empty())
        o["currencyConversions"] = encodeCurrencyConversions(p.currencyConversions);
    if (includeXerFinancials && !p.ledgerIdMigrations.empty())
        o["ledgerIdMigrations"] = encodeLedgerIdMigrations(p.ledgerIdMigrations);
    if (includeXerFinancials && !p.xerScheduleAdoptions.empty())
        o["xerScheduleAdoptions"] = encodeXerScheduleAdoptions(p.xerScheduleAdoptions);
    if (includeXerFinancials && !p.financialPeriodEvents.empty())
        o["financialPeriodEvents"] = encodeFinancialPeriodEvents(p.financialPeriodEvents);
    if (includeXerFinancials && !p.externalEvmAdoptions.empty())
        o["externalEvmAdoptions"] = encodeExternalEvmAdoptions(p.externalEvmAdoptions);
    if (includeXerFinancials && p.xerFinancials)
        o["xerFinancials"] = encodeXerFinancials(*p.xerFinancials);
    if (includeXerFinancials && !p.xerFinancialRevisions.empty())
        o["xerFinancialRevisions"] = encodeXerFinancialRevisions(p.xerFinancialRevisions);
    o["documentId"] = qs(p.documentId);
    o["derivedFrom"] = qs(p.derivedFrom);
    o["revision"] = double(p.revision);
    o["statusAt"] = nullable(p.statusAt);
    QJsonArray baselines, progress;
    for (const auto &b : p.baselines)
        baselines.append(encodeBaseline(b));
    for (const auto &e : p.progress)
        progress.append(encodeEntry(e));
    QJsonArray scenarios;
    for (const auto &s : p.scenarios)
        scenarios.append(encodeScenario(s));
    QJsonArray costs;
    for (const auto &e : p.costEntries)
        costs.append(encodeCostEntry(e));
    o["baselines"] = baselines;
    o["progress"] = progress;
    o["scenarios"] = scenarios;
    o["costEntries"] = costs;
    if (p.remainingLeveling)
        o["remainingLeveling"] = encodeLeveling(*p.remainingLeveling);
    if (!p.levelingDecisions.empty())
        o["levelingDecisions"] = encodeLevelingDecisions(p.levelingDecisions);
    if (p.progressPolicy)
        o["progressPolicy"] = encodeProgressPolicySelection(*p.progressPolicy);
    if (!p.materialActuals.empty())
        o["materialActuals"] = encodeMaterialActuals(p.materialActuals);
    if (!p.periodActuals.empty())
        o["periodActuals"] = encodePeriodActuals(p.periodActuals);
    if (!p.savedTaskViews.empty())
        o["savedTaskViews"] = encodeSavedTaskViews(p.savedTaskViews);
    if (!p.savedResourceViews.empty())
        o["savedResourceViews"] = encodeSavedResourceViews(p.savedResourceViews);
    if (!p.remainingWork.empty())
        o["remainingWork"] = encodeRemainingWork(p.remainingWork);
    if (!p.stepReports.empty())
        o["stepReports"] = encodeStepReports(p.stepReports);
    encodePresentation(o, p);
    return o;
}
Project decodeProject(const QJsonObject &o, int schema) {
    auto fields = planKeys(schema);
    if (schema >= 83)
        fields.append("materialActuals");
    if (schema >= 76)
        fields.append("xmlBaselineMerges");
    if (schema >= 25)
        fields.append("xerFinancials");
    if (schema >= 26)
        fields.append("xerFinancialRevisions");
    if (schema >= 29)
        fields.append("externalEvmAdoptions");
    if (schema >= 30)
        fields.append("financialPeriodEvents");
    if (schema >= 32)
        fields.append("xerScheduleAdoptions");
    if (schema >= 33)
        fields.append("ledgerIdMigrations");
    if (schema >= 34)
        fields.append("currencyConversions");
    fields.append({"documentId", "derivedFrom", "revision"});
    if (schema >= 4)
        fields.append({"statusAt", "baselines", "progress"});
    if (schema >= 5)
        fields.append("scenarios");
    if (schema >= 6)
        fields.append("costEntries");
    if (schema >= 10)
        fields.append("savedTaskViews");
    if (schema >= 11)
        fields.append("periodActuals");
    if (schema >= 12)
        fields.append("remainingLeveling");
    if (schema >= 13)
        fields.append("progressPolicy");
    if (schema >= 15)
        fields.append("levelingDecisions");
    if (schema >= 18)
        fields.append({"printPreferences", "networkPositions"});
    if (schema >= 19)
        fields.append("stepReports");
    if (schema >= 21)
        fields.append("savedResourceViews");
    if (schema >= 22)
        fields.append("remainingWork");
    keys(o, fields);
    auto plan = o;
    for (const auto &key : {"documentId",
                            "derivedFrom",
                            "revision",
                            "statusAt",
                            "baselines",
                            "progress",
                            "scenarios",
                            "costEntries",
                            "savedTaskViews",
                            "periodActuals",
                            "materialActuals",
                            "remainingLeveling",
                            "progressPolicy",
                            "levelingDecisions",
                            "printPreferences",
                            "networkPositions",
                            "stepReports",
                            "savedResourceViews",
                            "remainingWork",
                            "xerFinancials",
                            "xerFinancialRevisions",
                            "externalEvmAdoptions",
                            "financialPeriodEvents",
                            "xerScheduleAdoptions",
                            "ledgerIdMigrations",
                            "currencyConversions",
                            "xmlBaselineMerges"})
        plan.remove(key);
    Project p;
    if (schema >= 76 && o.contains("xmlBaselineMerges"))
        p.xmlBaselineMerges = decodeXmlBaselineMerges(o["xmlBaselineMerges"]);
    if (schema >= 34 && o.contains("currencyConversions"))
        p.currencyConversions = decodeCurrencyConversions(o["currencyConversions"]);
    if (schema >= 33 && o.contains("ledgerIdMigrations"))
        p.ledgerIdMigrations = decodeLedgerIdMigrations(o["ledgerIdMigrations"]);
    if (schema >= 32 && o.contains("xerScheduleAdoptions"))
        p.xerScheduleAdoptions = decodeXerScheduleAdoptions(o["xerScheduleAdoptions"]);
    if (schema >= 30 && o.contains("financialPeriodEvents"))
        p.financialPeriodEvents = decodeFinancialPeriodEvents(o["financialPeriodEvents"]);
    if (schema >= 29 && o.contains("externalEvmAdoptions"))
        p.externalEvmAdoptions = decodeExternalEvmAdoptions(o["externalEvmAdoptions"]);
    if (schema >= 25 && o.contains("xerFinancials"))
        p.xerFinancials = decodeXerFinancials(o["xerFinancials"]);
    if (schema >= 26 && o.contains("xerFinancialRevisions"))
        p.xerFinancialRevisions = decodeXerFinancialRevisions(o["xerFinancialRevisions"], schema >= 27, schema >= 28,
                                                              schema >= 31, schema >= 34);
    static_cast<PlanInput &>(p) = decodePlan(plan, schema);
    p.documentId = string(o, "documentId");
    p.derivedFrom = string(o, "derivedFrom");
    p.revision = std::uint64_t(integer(o, "revision"));
    if (schema >= 4) {
        p.statusAt = optionalString(o, "statusAt");
        for (const auto &b : array(o, "baselines"))
            p.baselines.push_back(decodeBaseline(object(b), schema));
        for (const auto &e : array(o, "progress"))
            p.progress.push_back(decodeEntry(object(e), schema));
    }
    if (schema >= 5)
        for (const auto &v : array(o, "scenarios"))
            p.scenarios.push_back(decodeScenario(object(v), schema));
    if (schema >= 6)
        for (const auto &v : array(o, "costEntries"))
            p.costEntries.push_back(decodeCostEntry(object(v)));
    if (schema >= 10 && o.contains("savedTaskViews"))
        p.savedTaskViews = decodeSavedTaskViews(o.value("savedTaskViews"));
    if (schema >= 21 && o.contains("savedResourceViews"))
        p.savedResourceViews = decodeSavedResourceViews(o["savedResourceViews"]);
    if (schema >= 22 && o.contains("remainingWork"))
        p.remainingWork = decodeRemainingWork(o["remainingWork"]);
    if (schema >= 83 && o.contains("materialActuals"))
        p.materialActuals = decodeMaterialActuals(o["materialActuals"]);
    if (schema >= 11 && o.contains("periodActuals"))
        p.periodActuals = decodePeriodActuals(o.value("periodActuals"));
    if (schema >= 12 && o.contains("remainingLeveling"))
        p.remainingLeveling = decodeLeveling(o["remainingLeveling"]);
    if (schema >= 13 && o.contains("progressPolicy"))
        p.progressPolicy = decodeProgressPolicySelection(o["progressPolicy"]);
    if (schema >= 15 && o.contains("levelingDecisions"))
        p.levelingDecisions = decodeLevelingDecisions(o["levelingDecisions"]);
    if (schema < 14 && p.progressPolicy && (p.planLeveling || p.remainingLeveling))
        throw AppError(ErrorCode::PROGRESS_POLICY_LEVELING);
    if (schema < 17 && nativeSchemaFor(p) >= 17)
        throw AppError(ErrorCode::ASSIGNMENT_PERIOD_INVALID, {"기간별 배정/정책은 schema17부터 지원합니다."});
    if (schema >= 19 && o.contains("stepReports"))
        p.stepReports = decodeStepReports(o["stepReports"]);
    if (schema < 20 && nativeSchemaFor(p) >= 20)
        throw AppError(ErrorCode::FILE_VERSION_UNSUPPORTED);
    const auto readSchema = nativeReadSchemaFor(p);
    if (readSchema >= 21 && schema < readSchema)
        throw AppError(ErrorCode::FILE_VERSION_UNSUPPORTED);
    if (schema < 19 && nativeSchemaFor(p) >= 19)
        throw AppError(ErrorCode::FILE_VERSION_UNSUPPORTED);
    if (schema >= 18)
        decodePresentation(p, o);
    validate(p);
    validateCurrencyCurrent(p);
    validateLedgerIdCurrent(p);
    if (!p.xerScheduleAdoptions.empty() && p.xerScheduleAdoptions.back().recordedRevision == p.revision &&
        p.xerScheduleAdoptions.back().after != static_cast<const PlanInput &>(p))
        throw AppError(ErrorCode::FILE_PRIMAVERAXER_080,
                       {"채택 직후의 현재 계획과 저장된 채택 결과가 일치하지 않습니다."});
    for (const auto &b : p.baselines)
        verifyBaseline(b);
    return p;
}
} // namespace ppm::json
