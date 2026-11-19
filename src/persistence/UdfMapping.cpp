#include "domain/XmlFormula.h"
#include "persistence/UdfMapping.h"
#include "domain/XmlRollDown.h"
#include "domain/Classification.h"
#include "domain/FormulaFields.h"
#include <QJsonObject>
#include <QUuid>
#include <charconv>
#include <algorithm>
#include <map>
#include <set>
namespace ppm {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
void note(QJsonArray &d, const std::string &path, const std::string &message) {
    d.append(QJsonObject{{"path", q(path)}, {"message", q(message)}});
}
using SlotKey = std::pair<FieldScope, CustomFieldType>;
const auto xmlSlots = [] {
    std::map<SlotKey, std::vector<int>> result;
    auto add = [&](FieldScope s, CustomFieldType t, std::vector<int> head, int first, int last) {
        for (int i = first; i <= last; ++i)
            head.push_back(i);
        result[{s, t}] = std::move(head);
    };
    add(FieldScope::Task, CustomFieldType::Text,
        {188743731, 188743734, 188743737, 188743740, 188743743, 188743746, 188743747, 188743748, 188743749, 188743750},
        188743997, 188744016);
    add(FieldScope::Task, CustomFieldType::Number, {188743767, 188743768, 188743769, 188743770, 188743771}, 188743982,
        188743996);
    add(FieldScope::Task, CustomFieldType::Date, {}, 188743945, 188743954);
    add(FieldScope::Resource, CustomFieldType::Text,
        {205520904, 205520905, 205520926, 205520927, 205520928, 205520993, 205520994, 205520995, 205520996, 205520997},
        205521121, 205521140);
    add(FieldScope::Resource, CustomFieldType::Number, {205521008, 205521009, 205521010, 205521011, 205521012},
        205521101, 205521115);
    add(FieldScope::Resource, CustomFieldType::Date, {}, 205521069, 205521078);
    add(FieldScope::Task, CustomFieldType::Flag,
        {188743752, 188743753, 188743754, 188743755, 188743756, 188743757, 188743758, 188743759, 188743760, 188743761},
        188743972, 188743981);
    add(FieldScope::Task, CustomFieldType::Cost, {188743786, 188743787, 188743788}, 188743938, 188743944);
    add(FieldScope::Task, CustomFieldType::Duration, {188743783, 188743784, 188743785}, 188743955, 188743961);
    add(FieldScope::Resource, CustomFieldType::Flag,
        {205521023, 205521024, 205521025, 205521026, 205521027, 205521028, 205521029, 205521030, 205521031, 205521022},
        205521091, 205521100);
    add(FieldScope::Resource, CustomFieldType::Cost, {205521019, 205521020, 205521021}, 205521062, 205521068);
    add(FieldScope::Resource, CustomFieldType::Duration, {205521013, 205521014, 205521015}, 205521079, 205521085);
    result[{FieldScope::Task, CustomFieldType::Start}] = {188743732, 188743735, 188743738, 188743741, 188743744,
                                                          188743962, 188743964, 188743966, 188743968, 188743970};
    result[{FieldScope::Task, CustomFieldType::Finish}] = {188743733, 188743736, 188743739, 188743742, 188743745,
                                                           188743963, 188743965, 188743967, 188743969, 188743971};
    add(FieldScope::Resource, CustomFieldType::Start, {205520998, 205520999, 205521000, 205521001, 205521002},
        205521116, 205521120);
    add(FieldScope::Resource, CustomFieldType::Finish, {205521003, 205521004, 205521005, 205521006, 205521007},
        205521086, 205521090);
    return result;
}();
std::string scopeName(FieldScope s) {
    return s == FieldScope::Task         ? "TASK"
           : s == FieldScope::Resource   ? "RSRC"
           : s == FieldScope::Assignment ? "ASSIGNMENT"
                                         : "PROJECT";
}
} // namespace
std::string xmlSlotName(const std::string &id) {
    for (const auto &[key, values] : xmlSlots)
        for (std::size_t i = 0; i < values.size(); ++i)
            if (std::to_string(values[i]) == id) {
                auto name = fieldTypeToken(key.second);
                name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
                return name + std::to_string(i + 1);
            }
    for (int n = 0; n < 10; ++n)
        if (id == std::to_string(188744096 + n * 2) || id == std::to_string(205521174 + n * 2))
            return "Outline Code" + std::to_string(n + 1);
    return "";
}
std::optional<std::pair<FieldScope, CustomFieldType>> xmlUdfType(const std::string &id) {
    if (xmlOutlineUdf(id)) {
        const auto n = std::stoll(id);
        return std::pair{n < 200000000 ? FieldScope::Task : FieldScope::Resource, CustomFieldType::Text};
    }
    for (const auto &[key, values] : xmlSlots)
        for (auto v : values)
            if (std::to_string(v) == id)
                return key;
    return {};
}
bool xmlOutlineUdf(const std::string &id) {
    for (int n = 0; n < 10; ++n)
        if (id == std::to_string(188744096 + n * 2) || id == std::to_string(205521174 + n * 2))
            return true;
    return false;
}
std::optional<FieldScope> xmlDynamicUdfScope(const std::string &id) {
    unsigned n = 0;
    const auto [end, ec] = std::from_chars(id.data(), id.data() + id.size(), n);
    if (ec != std::errc{} || end != id.data() + id.size() || std::to_string(n) != id)
        return {};
    for (auto [base, scope] : {std::pair{0x0b408000u, FieldScope::Task},
                               {0x0b608000u, FieldScope::Project},
                               {0x0c408000u, FieldScope::Resource},
                               {0x0f408000u, FieldScope::Assignment}})
        if (n >= base && n <= base + 65535)
            return scope;
    if (n >= 0x0f404000u && n <= 0x0f4040c8u)
        return FieldScope::Assignment;
    return {};
}
ExternalField importUdfField(Project &p, bool xml, FieldScope scope, const std::string &id, const std::string &name,
                             CustomFieldType type, QJsonArray &mapping) {
    auto native = (xml ? "xml-udf-" : "xer-udf-") + scopeName(scope) + "-" + id;
    auto &fields = customFields(p, scope);
    if (fields.size() >= 64 || std::any_of(fields.begin(), fields.end(), [&](const auto &f) { return f.id == native; }))
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"UDF 사전 한도 또는 중복 ID: " + id});
    fields.push_back({native, name.empty() ? scopeName(scope) + " UDF " + id : name, type, {}, {}});
    mapping.append(QJsonObject{{"kind", "udf"},
                               {"scope", q(scopeName(scope))},
                               {"externalId", q(id)},
                               {"ppmId", q(native)},
                               {"name", q(fields.back().name)},
                               {"type", q(fieldTypeToken(type))}});
    return {scope, native, id, fields.back().name, type};
}
void importUdfValue(Project &p, const ExternalField &f, std::vector<TaskAttribute> &values, const std::string &value) {
    auto key = "field:" + f.nativeId;
    if (attributeValue(values, key))
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 UDF 값: " + f.externalId});
    values.push_back({key, normalizeAttributeValue(p, key, value, f.scope)});
}
std::vector<ExternalField> exportUdfFields(const Project &p, bool xml, QJsonArray &diagnostics, QJsonArray &mapping) {
    const RollDownValues inherited(p);
    std::vector<ExternalField> out;
    std::set<std::string> used;
    if (xml)
        for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
            for (const auto &f : customFields(p, scope)) {
                if (f.xmlIdentity)
                    used.insert(f.xmlIdentity->fieldId);
                const auto prefix = "xml-udf-" + scopeName(scope) + "-";
                if (f.id.starts_with(prefix)) {
                    auto id = f.id.substr(prefix.size());
                    auto type = xmlUdfType(id);
                    auto effective = f.xmlLookup                         ? f.xmlLookup->valueType
                                     : f.type == CustomFieldType::Choice ? CustomFieldType::Text
                                                                         : f.type;
                    if (type && type->first == scope && type->second == effective)
                        used.insert(id);
                }
            }
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope)) {
            auto effective = xml && f.xmlLookup                  ? f.xmlLookup->valueType
                             : f.type == CustomFieldType::Choice ? CustomFieldType::Text
                                                                 : f.type;
            if (!xml && (effective == CustomFieldType::Start || effective == CustomFieldType::Finish))
                effective = CustomFieldType::Text;
            if (!xml && effective == CustomFieldType::Date) {
                const auto &attrs = customFields(p, scope);
                (void)attrs;
                // XML date/time values retain seconds in XER text; legacy all-day dates stay dates.
                bool timed = false;
                auto inspect = [&](const std::optional<std::string> &v) {
                    if (v && v->size() > 10)
                        timed = true;
                };
                if (scope == FieldScope::Project)
                    inspect(resolvedProjectAttributeValue(p, "field:" + f.id));
                if (scope == FieldScope::Task)
                    for (const auto &t : p.tasks)
                        inspect(resolvedAttributeValue(p, t, "field:" + f.id));
                if (scope == FieldScope::Resource)
                    for (const auto &r : p.resources)
                        inspect(resolvedAttributeValue(p, r, "field:" + f.id));
                if (scope == FieldScope::Assignment)
                    for (const auto &a : p.assignments)
                        inspect(inherited.value(a, "field:" + f.id));
                if (timed) {
                    effective = CustomFieldType::Text;
                    note(diagnostics, "UDF/" + f.id,
                         "초·소수 초가 있는 날짜는 XER 텍스트로 투영합니다. 원래 형식은 source.ppm에 보존합니다.");
                }
            }
            if (!xml && (effective == CustomFieldType::Flag || effective == CustomFieldType::Duration))
                effective = CustomFieldType::Text;
            if (!xml && effective == CustomFieldType::Cost)
                effective = CustomFieldType::Text;
            std::string id;
            std::optional<XmlFieldIdentity> identity;
            if (xml && (f.xmlIdentity || scope == FieldScope::Project || scope == FieldScope::Assignment)) {
                if (f.xmlIdentity)
                    identity = f.xmlIdentity;
                else {
                    unsigned slot = scope == FieldScope::Project ? 0x0b608000u : 0x0f408000u;
                    while (used.contains(std::to_string(slot)))
                        ++slot;
                    id = std::to_string(slot);
                    used.insert(id);
                    const auto identityKey =
                        std::to_string(p.documentId.size()) + ":" + p.documentId + scopeName(scope) + ":" + f.id;
                    const auto guid =
                        QUuid::createUuidV5(QUuid("{81b16ac7-138a-4b56-8511-3de9af342f09}"), q(identityKey).toUtf8());
                    identity = XmlFieldIdentity{id, guid.toString(QUuid::WithoutBraces).toStdString(), f.name};
                }
                id = identity->fieldId;
            } else if (xml) {
                auto prefix = "xml-udf-" + scopeName(scope) + "-";
                if (f.id.starts_with(prefix)) {
                    auto existing = f.id.substr(prefix.size());
                    auto type = xmlUdfType(existing);
                    if (type && type->first == scope && type->second == effective)
                        id = existing;
                }
                if (id.empty())
                    for (auto slot : xmlSlots.at({scope, effective}))
                        if (!used.contains(std::to_string(slot))) {
                            id = std::to_string(slot);
                            used.insert(id);
                            break;
                        }
                if (id.empty())
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                                   {"XML 사용자 필드 슬롯이 부족합니다: " + scopeName(scope)});
            } else
                id = std::to_string(out.size() + 1);
            out.push_back({scope, f.id, id, f.name, effective, identity});
            mapping.append(QJsonObject{{"kind", "udf"},
                                       {"scope", q(scopeName(scope))},
                                       {"externalId", q(id)},
                                       {"ppmId", q(f.id)},
                                       {"name", q(f.name)},
                                       {"type", q(fieldTypeToken(effective))}});
            if (identity) {
                auto entry = mapping.last().toObject();
                entry["xmlGuid"] = q(identity->guid);
                entry["xmlFieldName"] = q(identity->fieldName);
                entry["representation"] = "hex-element";
                mapping[mapping.size() - 1] = entry;
            }
            if (!xml && int(f.type) >= int(CustomFieldType::Flag))
                note(diagnostics, "UDF/" + f.id,
                     "XML 추가 형식은 XER에 정적 값으로 투영합니다. Flag/기간 의미와 원래 형식은 source.ppm에 "
                     "보존합니다.");
            if (!xml && f.xmlIdentity)
                note(diagnostics, "UDF/" + f.id,
                     "XML 기업 필드 ID/GUID는 source.ppm에 보존하고 XER에는 정적 값으로 투영합니다.");
            if (f.type == CustomFieldType::Choice && !xml)
                note(diagnostics, "UDF/" + f.id,
                     "목록은 표시 이름의 텍스트로 투영합니다. 옵션 ID·사전은 source.ppm에 보존합니다.");
            if (!f.unit.empty())
                note(diagnostics, "UDF/" + f.id, "단위 메타데이터는 source.ppm에 보존합니다: " + f.unit);
            if (calculatedField(f))
                note(diagnostics, "UDF/" + f.id,
                     "계산식은 현재 숫자 결과만 투영합니다. 식·의존성은 source.ppm에 보존합니다.");
        }
    if (xml) {
        std::set<std::string> reserved;
        for (const auto &f : out) {
            const auto *native = customField(p, "field:" + f.nativeId, f.scope);
            if (native->xmlLookup)
                for (const auto &v : native->xmlLookup->values)
                    if (!v.valueId.empty())
                        reserved.insert(v.valueId);
        }
        unsigned next = 1;
        auto allocate = [&] {
            while (reserved.contains(std::to_string(next)))
                ++next;
            auto id = std::to_string(next++);
            reserved.insert(id);
            return id;
        };
        std::map<std::string, XmlLookup> tables;
        std::set<std::string> localIds;
        for (auto &f : out) {
            const auto &native = *customField(p, "field:" + f.nativeId, f.scope);
            if (native.type != CustomFieldType::Choice)
                continue;
            XmlLookup l;
            if (native.xmlLookup)
                l = *native.xmlLookup;
            else {
                for (const auto &o : native.options)
                    l.values.push_back({o.id, allocate(), {}, o.name, {}, {}, true});
                if (f.xmlIdentity || native.options.empty()) {
                    auto uuid = [&](const std::string &key) {
                        return QUuid::createUuidV5(QUuid("{bfb1fbba-5b10-4342-9ba4-073b081411ce}"),
                                                   q(p.documentId + ":" + f.nativeId + ":" + key).toUtf8())
                            .toString(QUuid::WithoutBraces)
                            .toStdString();
                    };
                    l.tableGuid = uuid("table");
                    l.tableName = native.name;
                    for (auto &v : l.values)
                        v.guid = uuid(v.optionId);
                }
            }
            if (l.tableGuid.empty()) {
                for (auto &v : l.values)
                    if (v.listed && !localIds.insert(v.valueId).second) {
                        v.valueId = allocate();
                        localIds.insert(v.valueId);
                    }
            } else {
                auto shared = sharedXmlLookup(l);
                if (tables.contains(l.tableGuid) && tables.at(l.tableGuid) != shared)
                    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"공유 XML 목록 GUID의 사전이 서로 다릅니다."});
                tables.emplace(l.tableGuid, std::move(shared));
            }
            f.lookup = std::move(l);
        }
    }
    return out;
}
std::optional<std::string> exportUdfValue(const Project &p, const ExternalField &f, const Task *t, const Resource *r,
                                          QJsonArray &diagnostics, const Assignment *a,
                                          const RollDownValues *inherited) {
    const auto key = "field:" + f.nativeId;
    const auto *definition = customField(p, key, f.scope);
    auto value = a && calculatedField(*definition)
                     ? formulaRawValue(formulaValues(p, FieldScope::Assignment, t, r, a)->at(key))
                 : a ? (inherited           ? inherited->value(*a, key)
                        : hasXmlRollDown(p) ? RollDownValues(p).value(*a, key)
                                            : attributeValue(a->attributes, key))
                 : t ? resolvedAttributeValue(p, *t, key)
                 : r ? resolvedAttributeValue(p, *r, key)
                     : resolvedProjectAttributeValue(p, key);
    if (!value && calculatedField(*definition)) {
        const auto computed = formulaValues(p, f.scope, t, r, a);
        note(diagnostics,
             "UDF/" + f.nativeId + "/" +
                 (t   ? t->id
                  : r ? r->id
                      : "PROJECT"),
             formulaValueText(computed->at(key)) + " · 외부 값 생략");
    }
    if (value && (definition->type == CustomFieldType::Flag || definition->type == CustomFieldType::Duration))
        return attributeText(p, key, *value, f.scope);
    if (value && definition->type == CustomFieldType::Choice)
        return attributeText(p, key, *value, f.scope);
    return value;
}
} // namespace ppm
