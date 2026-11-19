#include "domain/XmlSchedule.h"
#include "domain/XmlSummaryDuration.h"
#include "domain/XmlCalendar.h"
#include "domain/XmlFormula.h"
#include "domain/XmlArithmetic.h"
#include "domain/XmlLogical.h"
#include "domain/XmlLike.h"
#include "persistence/ClassificationJson.h"
#include "domain/Classification.h"

namespace ppm::json {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s);
}
std::string str(const QJsonValue &v) {
    if (!v.isString())
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"파일의 문자열 형식을 확인하세요."});
    return v.toString().toStdString();
}
QJsonObject obj(const QJsonValue &v, const QStringList &fields) {
    if (!v.isObject())
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"파일의 객체 형식을 확인하세요."});
    const auto o = v.toObject();
    if (o.size() != fields.size())
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"파일 필드가 누락되었거나 알 수 없습니다."});
    for (const auto &field : fields)
        if (!o.contains(field))
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"파일 필드가 누락되었습니다."});
    return o;
}
QJsonArray arr(const QJsonValue &v, qsizetype limit) {
    if (!v.isArray() || v.toArray().size() > limit)
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"파일 배열의 형식·개수 제한을 확인하세요."});
    return v.toArray();
}
int integer(const QJsonValue &v, int maximum) {
    if (!v.isDouble() || v.toDouble() != v.toInt(-1) || v.toInt(-1) < 0 || v.toInt(-1) > maximum)
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"파일 정수의 형식·범위를 확인하세요."});
    return v.toInt();
}
bool boolean(const QJsonValue &v) {
    if (!v.isBool())
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"파일 참/거짓 형식을 확인하세요."});
    return v.toBool();
}
QJsonArray strings(const std::vector<std::string> &values) {
    QJsonArray a;
    for (const auto &v : values)
        a.append(qs(v));
    return a;
}
std::vector<std::string> strings(const QJsonValue &value, int maximum) {
    std::vector<std::string> result;
    for (const auto &v : arr(value, maximum))
        result.push_back(str(v));
    return result;
}
} // namespace
QJsonArray encodeXmlCodeMasks(const std::vector<XmlCodeMask> &masks) {
    QJsonArray a;
    for (const auto &m : masks)
        a.append(
            QJsonObject{{"level", m.level}, {"type", m.type}, {"length", m.length}, {"separator", qs(m.separator)}});
    return a;
}
std::vector<XmlCodeMask> decodeXmlCodeMasks(const QJsonValue &v) {
    std::vector<XmlCodeMask> result;
    for (const auto &entry : arr(v, 16)) {
        const auto m = obj(entry, {"level", "type", "length", "separator"});
        result.push_back(
            {integer(m["level"], 16), integer(m["type"], 3), integer(m["length"], 255), str(m["separator"])});
    }
    return result;
}
QJsonArray encodeCustomFields(const std::vector<CustomField> &definitions) {
    QJsonArray fields;
    for (const auto &field : definitions) {
        QJsonArray options;
        for (const auto &o : field.options)
            options.append(QJsonObject{{"id", qs(o.id)}, {"name", qs(o.name)}});
        QJsonObject definition{{"id", qs(field.id)},
                               {"name", qs(field.name)},
                               {"type", qs(fieldTypeToken(field.type))},
                               {"unit", qs(field.unit)},
                               {"options", options}};
        if (!field.expression.empty())
            definition["expression"] = qs(field.expression);
        if (field.xmlFormula) {
            QJsonArray refs;
            for (const auto &r : field.xmlFormula->references)
                refs.append(QJsonObject{{"token", qs(r.token)}, {"key", qs(r.key)}, {"scope", int(r.scope)}});
            definition["xmlFormula"] = QJsonObject{
                {"source", qs(field.xmlFormula->source)}, {"enabled", field.xmlFormula->enabled}, {"references", refs}};
        }
        if (field.xmlRollUp)
            definition["xmlRollUp"] = QJsonObject{{"calculationType", field.xmlRollUp->calculationType},
                                                  {"rollupType", field.xmlRollUp->rollupType}};
        if (field.xmlRollDown)
            definition["xmlRollDown"] = QJsonObject{{"assignmentFieldId", qs(field.xmlRollDown->assignmentFieldId)},
                                                    {"automatic", field.xmlRollDown->automatic}};
        if (field.xmlIdentity)
            definition["xmlIdentity"] = QJsonObject{{"fieldId", qs(field.xmlIdentity->fieldId)},
                                                    {"guid", qs(field.xmlIdentity->guid)},
                                                    {"fieldName", qs(field.xmlIdentity->fieldName)}};
        if (field.xmlLookup) {
            const auto &l = *field.xmlLookup;
            QJsonArray values;
            for (const auto &v : l.values)
                values.append(QJsonObject{{"optionId", qs(v.optionId)},
                                          {"valueId", qs(v.valueId)},
                                          {"guid", qs(v.guid)},
                                          {"value", qs(v.value)},
                                          {"description", qs(v.description)},
                                          {"parentId", qs(v.parentId)},
                                          {"listed", v.listed}});
            definition["xmlLookup"] = QJsonObject{{"valueType", qs(fieldTypeToken(l.valueType))},
                                                  {"tableGuid", qs(l.tableGuid)},
                                                  {"tableName", qs(l.tableName)},
                                                  {"fieldGuid", qs(l.fieldGuid)},
                                                  {"fieldName", qs(l.fieldName)},
                                                  {"defaultOptionId", qs(l.defaultOptionId)},
                                                  {"maxValues", l.maxValues},
                                                  {"restrictValues", l.restrictValues},
                                                  {"leafOnly", l.leafOnly},
                                                  {"values", values}};
            if (!l.masks.empty() || l.allLevelsRequired) {
                auto lookup = definition["xmlLookup"].toObject();
                lookup["codeMask"] =
                    QJsonObject{{"levels", encodeXmlCodeMasks(l.masks)}, {"allLevelsRequired", l.allLevelsRequired}};
                definition["xmlLookup"] = lookup;
            }
        }
        fields.append(definition);
    }
    return fields;
}
QJsonObject encodeClassification(const PlanInput &p) {
    auto fields = encodeCustomFields(p.customFields);
    QJsonArray types;
    for (const auto &type : p.codeTypes) {
        QJsonArray values;
        for (const auto &v : type.values)
            values.append(QJsonObject{{"id", qs(v.id)}, {"name", qs(v.name)}, {"parentId", qs(v.parentId)}});
        types.append(QJsonObject{{"id", qs(type.id)}, {"name", qs(type.name)}, {"values", values}});
    }
    return {{"customFields", fields}, {"codeTypes", types}};
}
std::vector<CustomField> decodeCustomFields(const QJsonValue &value, int schema) {
    std::vector<CustomField> result;
    for (const auto &v : arr(value, 64)) {
        auto fields = QStringList{"id", "name", "type", "unit", "options"};
        if (schema >= 23 && v.isObject() && v.toObject().contains("expression"))
            fields.append("expression");
        if (schema >= 37 && v.isObject() && v.toObject().contains("xmlIdentity"))
            fields.append("xmlIdentity");
        if (schema >= 38 && v.isObject() && v.toObject().contains("xmlLookup"))
            fields.append("xmlLookup");
        if (schema >= 42 && v.isObject() && v.toObject().contains("xmlRollDown"))
            fields.append("xmlRollDown");
        if (schema >= 44 && v.isObject() && v.toObject().contains("xmlFormula"))
            fields.append("xmlFormula");
        if (schema >= 43 && v.isObject() && v.toObject().contains("xmlRollUp"))
            fields.append("xmlRollUp");
        const auto f = obj(v, fields);
        CustomField field{str(f["id"]), str(f["name"]), fieldTypeFromToken(str(f["type"])), str(f["unit"]), {}};
        if (schema < 40 && (field.type == CustomFieldType::Start || field.type == CustomFieldType::Finish))
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"시작/종료 일시는 schema40이 필요합니다."});
        if (schema < 39 && int(field.type) >= int(CustomFieldType::Flag))
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"추가 필드 형식은 schema39가 필요합니다."});
        if (f.contains("xmlFormula")) {
            const auto r = obj(f["xmlFormula"], {"source", "enabled", "references"});
            XmlFormula x;
            x.source = str(r["source"]);
            x.enabled = boolean(r["enabled"]);
            if (schema < 72 && x.enabled && xmlDepreciationFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 SLN/SYD는 schema72 이상입니다."});
            if (schema < 71 && x.enabled && xmlPartitionFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 Partition은 schema71 이상입니다."});
            if (schema < 70 && x.enabled && xmlColorFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 RGB/QBColor는 schema70 이상입니다."});
            if (schema < 69 && x.enabled && xmlNumericDisplayFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 숫자 표시 함수는 schema69 이상입니다."});
            if (schema < 68 && x.enabled && xmlDateDisplayFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 FormatDateTime은 schema68 이상입니다."});
            if (schema < 67 && x.enabled && xmlDateNameFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                               {"활성 MonthName/WeekdayName은 schema67 이상입니다."});
            if (schema < 66 && x.enabled && xmlUnicodeFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 AscW/ChrW는 schema66 이상입니다."});
            if (schema < 65 && x.enabled && xmlLikeFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 Like는schema65 이상입니다."});
            if (schema < 64 && x.enabled && xmlLogicalFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 Eqv/Imp는schema64 이상입니다."});
            if (schema < 63 && x.enabled && xmlArithmeticFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 ^/정수 나눗셈은schema63 이상입니다."});
            if (schema < 61 && x.enabled && xmlProjectConversionFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                               {"활성 ProjDateValue/ProjDateConv/ProjDurConv는schema61 이상입니다."});
            if (schema < 60 && x.enabled && xmlConversionFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 CDec/CSng/CVar는schema60 이상입니다."});
            if (schema < 59 && x.enabled && xmlStringFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                               {"활성 Replace/InStrRev/StrReverse는schema59 이상입니다."});
            if (schema < 58 && x.enabled && xmlMathFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                               {"활성 Atn/Cos/Exp/Log/Sin/Tan은schema58 이상입니다."});
            if (schema < 57 && x.enabled && xmlNumberTextFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 Str/Val/Hex/Oct는schema57 이상입니다."});
            if (schema < 56 && x.enabled && xmlTimeFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 TimeSerial/TimeValue는schema56 이상입니다."});
            // Existing three-argument DateAdd/DateDiff allowed late-bound or
            // unsupported interval values; keep those old documents readable.
            if (schema < 55 && x.enabled && xmlNewDateSyntax(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"새 날짜 함수 문법은schema55 이상입니다."});
            if (schema < 54 && x.enabled && xmlFormattingFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 Format/StrConv 함수는 schema54 이상입니다."});
            if (schema < 53 && x.enabled && xmlTimeZoneFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"활성 시간대 함수는 schema53 이상입니다."});
            if (schema < 52 && x.enabled && xmlAdditionalFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"추가 XML 함수는 schema52 이상입니다."});
            if (schema < 46 && xmlCalendarFormula(x))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"근무 달력 함수는 schema46 이상입니다."});
            for (const auto &value : arr(r["references"], 128)) {
                const auto ref = obj(value, {"token", "key", "scope"});
                if (schema < 74 && xmlDurationReference(str(ref["key"])) &&
                    FieldScope(integer(ref["scope"], 3)) == FieldScope::Project)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"프로젝트 요약 기간은 schema74 이상입니다."});
                if (schema < 73 && xmlBaselineSlot(str(ref["key"])))
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"번호별 기준선은 schema73 이상입니다."});
                if (schema < 62 && ref["key"] == "builtin:baselinecost")
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"Baseline Cost 바인딩은schema62 이상입니다."});
                if (schema < 51 && xmlEarlyLateReference(str(ref["key"])))
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"Early/Late 참조는 schema51 이상입니다."});
                if (schema < 50 && xmlBaselineReference(str(ref["key"])))
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"기준선 참조는 schema50 이상입니다."});
                if (schema < 49 && xmlRemainingReference(str(ref["key"])))
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"잔여 참조는 schema49 이상입니다."});
                if (schema < 48 && xmlActualQuantityReference(str(ref["key"])))
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"실제 기간/공수 참조는 schema48 이상입니다."});
                if (schema < 47 && xmlActualReference(str(ref["key"])))
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"실적 일정 참조는 schema47 이상입니다."});
                if (schema < 45 && (ref["key"] == "builtin:start" || ref["key"] == "builtin:finish"))
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"계산된 일정 참조는 schema45 이상입니다."});
                x.references.push_back({str(ref["token"]), str(ref["key"]), FieldScope(integer(ref["scope"], 3))});
            }
            field.xmlFormula = std::move(x);
        }
        if (f.contains("xmlRollUp")) {
            const auto r = obj(f["xmlRollUp"], {"calculationType", "rollupType"});
            field.xmlRollUp = XmlRollUp{integer(r["calculationType"], 2), integer(r["rollupType"], 7)};
        }
        if (f.contains("xmlRollDown")) {
            const auto r = obj(f["xmlRollDown"], {"assignmentFieldId", "automatic"});
            field.xmlRollDown = XmlRollDown{str(r["assignmentFieldId"]), boolean(r["automatic"])};
        }
        if (f.contains("xmlIdentity")) {
            const auto x = obj(f["xmlIdentity"], {"fieldId", "guid", "fieldName"});
            field.xmlIdentity = XmlFieldIdentity{str(x["fieldId"]), str(x["guid"]), str(x["fieldName"])};
        }
        if (f.contains("xmlLookup")) {
            auto allowed = QStringList{"valueType",       "tableGuid", "tableName",      "fieldGuid", "fieldName",
                                       "defaultOptionId", "maxValues", "restrictValues", "leafOnly",  "values"};
            if (schema >= 41 && f["xmlLookup"].toObject().contains("codeMask"))
                allowed.append("codeMask");
            const auto o = obj(f["xmlLookup"], allowed);
            XmlLookup l;
            l.valueType = fieldTypeFromToken(str(o["valueType"]));
            if (schema < 40 && (l.valueType == CustomFieldType::Start || l.valueType == CustomFieldType::Finish))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"시작/종료 목록은 schema40이 필요합니다."});
            if (schema < 39 && int(l.valueType) >= int(CustomFieldType::Flag))
                throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"추가 목록 형식은 schema39가 필요합니다."});
            l.fieldGuid = str(o["fieldGuid"]);
            l.fieldName = str(o["fieldName"]);
            l.tableGuid = str(o["tableGuid"]);
            l.tableName = str(o["tableName"]);
            l.defaultOptionId = str(o["defaultOptionId"]);
            l.maxValues = integer(o["maxValues"], 1000);
            l.restrictValues = boolean(o["restrictValues"]);
            l.leafOnly = boolean(o["leafOnly"]);
            if (o.contains("codeMask")) {
                const auto m = obj(o["codeMask"], {"levels", "allLevelsRequired"});
                l.masks = decodeXmlCodeMasks(m["levels"]);
                l.allLevelsRequired = boolean(m["allLevelsRequired"]);
            }
            for (const auto &entry : arr(o["values"], 1000)) {
                const auto v =
                    obj(entry, {"optionId", "valueId", "guid", "value", "description", "parentId", "listed"});
                l.values.push_back({str(v["optionId"]), str(v["valueId"]), str(v["guid"]), str(v["value"]),
                                    str(v["description"]), str(v["parentId"]), boolean(v["listed"])});
            }
            field.xmlLookup = std::move(l);
        }
        if (f.contains("expression")) {
            field.expression = str(f["expression"]);
            if (field.expression.empty() || field.expression.size() > 2048)
                throw AppError(ErrorCode::FORMULA_INVALID, {"파일 수식의 길이를 확인하세요."});
        }
        for (const auto &entry : arr(f["options"], 1000)) {
            const auto option = obj(entry, {"id", "name"});
            field.options.push_back({str(option["id"]), str(option["name"])});
        }
        result.push_back(std::move(field));
    }
    return result;
}
void decodeClassification(PlanInput &p, const QJsonObject &o, int schema) {
    p.customFields = decodeCustomFields(o.value("customFields"), schema);
    std::size_t count = 0;
    for (const auto &v : arr(o.value("codeTypes"), 64)) {
        const auto t = obj(v, {"id", "name", "values"});
        ActivityCodeType type{str(t["id"]), str(t["name"]), {}};
        const auto values = arr(t["values"], 10000);
        count += std::size_t(values.size());
        if (count > 10000)
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"코드 전체 노드 제한을 초과했습니다."});
        for (const auto &entry : values) {
            const auto node = obj(entry, {"id", "name", "parentId"});
            type.values.push_back({str(node["id"]), str(node["name"]), str(node["parentId"])});
        }
        p.codeTypes.push_back(std::move(type));
    }
}
QJsonArray encodeAttributes(const std::vector<TaskAttribute> &values) {
    QJsonArray result;
    for (const auto &v : values)
        result.append(QJsonObject{{"key", qs(v.key)}, {"value", qs(v.value)}});
    return result;
}
std::vector<TaskAttribute> decodeAttributes(const QJsonValue &value) {
    std::vector<TaskAttribute> result;
    for (const auto &v : arr(value, 128)) {
        const auto o = obj(v, {"key", "value"});
        result.push_back({str(o["key"]), str(o["value"])});
    }
    return result;
}
QJsonObject encodeTaskViewSettings(const TaskViewSettings &s) {
    QJsonArray filters, columns;
    for (const auto &f : s.attributeFilters)
        filters.append(QJsonObject{{"key", qs(f.key)}, {"operation", qs(f.operation)}, {"value", qs(f.value)}});
    for (const auto &c : s.columns)
        columns.append(QJsonObject{{"key", qs(c.key)}, {"width", c.width}, {"hidden", c.hidden}});
    return {{"wbsId", qs(s.wbsId)},
            {"query", qs(s.query)},
            {"filter", s.filter},
            {"sort", s.sort},
            {"groupBy", strings(s.groupBy)},
            {"groupPath", strings(s.groupPath)},
            {"attributeFilters", filters},
            {"sortKey", qs(s.sortKey)},
            {"descending", s.descending},
            {"columns", columns}};
}
QJsonArray encodeSavedTaskViews(const std::vector<SavedTaskView> &views) {
    QJsonArray result;
    for (const auto &v : views)
        result.append(
            QJsonObject{{"id", qs(v.id)}, {"name", qs(v.name)}, {"settings", encodeTaskViewSettings(v.settings)}});
    return result;
}
std::vector<SavedTaskView> decodeSavedTaskViews(const QJsonValue &value) {
    std::vector<SavedTaskView> result;
    for (const auto &v : arr(value, 64)) {
        const auto o = obj(v, {"id", "name", "settings"});
        SavedTaskView view{str(o["id"]), str(o["name"]), {}};
        const auto s = obj(o["settings"], {"wbsId", "query", "filter", "sort", "groupBy", "groupPath",
                                           "attributeFilters", "sortKey", "descending", "columns"});
        auto &settings = view.settings;
        settings.wbsId = str(s["wbsId"]);
        settings.query = str(s["query"]);
        settings.filter = integer(s["filter"], 3);
        settings.sort = integer(s["sort"], 6);
        settings.groupBy = strings(s["groupBy"], 3);
        settings.groupPath = strings(s["groupPath"], 3);
        settings.sortKey = str(s["sortKey"]);
        settings.descending = boolean(s["descending"]);
        for (const auto &f : arr(s["attributeFilters"], 8)) {
            const auto filter = obj(f, {"key", "operation", "value"});
            settings.attributeFilters.push_back({str(filter["key"]), str(filter["operation"]), str(filter["value"])});
        }
        for (const auto &c : arr(s["columns"], 142)) {
            const auto column = obj(c, {"key", "width", "hidden"});
            settings.columns.push_back({str(column["key"]), integer(column["width"], 2000), boolean(column["hidden"])});
        }
        result.push_back(std::move(view));
    }
    return result;
}
QJsonArray encodeSavedResourceViews(const std::vector<SavedResourceView> &views) {
    QJsonArray out;
    for (const auto &v : views) {
        QJsonArray filters;
        for (const auto &f : v.settings.filters)
            filters.append(QJsonObject{{"key", qs(f.key)}, {"operation", qs(f.operation)}, {"value", qs(f.value)}});
        out.append(QJsonObject{{"id", qs(v.id)},
                               {"name", qs(v.name)},
                               {"settings", QJsonObject{{"query", qs(v.settings.query)},
                                                        {"sortKey", qs(v.settings.sortKey)},
                                                        {"descending", v.settings.descending},
                                                        {"groupBy", strings(v.settings.groupBy)},
                                                        {"filters", filters}}}});
    }
    return out;
}
std::vector<SavedResourceView> decodeSavedResourceViews(const QJsonValue &value) {
    std::vector<SavedResourceView> result;
    for (const auto &entry : arr(value, 64)) {
        const auto v = obj(entry, {"id", "name", "settings"});
        const auto s = obj(v["settings"], {"query", "sortKey", "descending", "groupBy", "filters"});
        SavedResourceView view;
        view.id = str(v["id"]);
        view.name = str(v["name"]);
        view.settings.query = str(s["query"]);
        view.settings.sortKey = str(s["sortKey"]);
        view.settings.descending = boolean(s["descending"]);
        view.settings.groupBy = strings(s["groupBy"], 3);
        for (const auto &f : arr(s["filters"], 8)) {
            const auto x = obj(f, {"key", "operation", "value"});
            view.settings.filters.push_back({str(x["key"]), str(x["operation"]), str(x["value"])});
        }
        result.push_back(std::move(view));
    }
    return result;
}
} // namespace ppm::json
