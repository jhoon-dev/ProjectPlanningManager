#include "domain/XmlCalendar.h"
#include "domain/ResourceAvailability.h"
#include "domain/XmlSchedule.h"
#include "domain/XmlSummaryDuration.h"
#include "domain/XmlFormula.h"
#include "domain/XmlArithmetic.h"
#include "domain/XmlLogical.h"
#include "domain/XmlLike.h"
#include "domain/NativeSchema.h"
#include "domain/ResourcePlan.h"
#include "domain/MaterialActuals.h"
#include "domain/Classification.h"
#include "domain/XmlCodes.h"
#include "domain/XmlRollDown.h"
#include "domain/XmlRollUp.h"
#include "domain/FormulaFields.h"
#include "domain/RemainingWork.h"
#include "domain/Productivity.h"
#include "domain/CalculationControl.h"
#include "domain/Wbs.h"
#include <algorithm>
#include <charconv>
#include <functional>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace ppm {
namespace {
[[noreturn]] void invalid(const std::string &reason) {
    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {reason});
}
bool label(const std::string &text, std::size_t limit = 1024) {
    return !text.empty() && text.size() <= limit &&
           std::any_of(text.begin(), text.end(), [](unsigned char c) { return c > 32; }) &&
           std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool textValue(const std::string &text) {
    return text.size() <= 16384 && std::none_of(text.begin(), text.end(), [](unsigned char c) {
               return (c < 32 && c != '\n' && c != '\r' && c != '\t') || c == 127;
           });
}
std::string normalizedGuid(std::string value) {
    if (value.size() != 36)
        invalid("XML 필드 GUID 형식을 확인하세요.");
    for (std::size_t i = 0; i < value.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        auto &c = value[i];
        if (c >= 'A' && c <= 'F')
            c += 'a' - 'A';
        if (dash ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            invalid("XML 필드 GUID 형식을 확인하세요.");
    }
    if (value == "00000000-0000-0000-0000-000000000000")
        invalid("XML 필드 GUID가 비어 있습니다.");
    return value;
}
bool keyExists(const PlanInput &p, const std::string &key) {
    return key == "wbs" || customField(p, key) || codeType(p, key);
}
std::string codePath(const ActivityCodeType &type, std::string id) {
    std::unordered_map<std::string, const ActivityCode *> nodes;
    for (const auto &node : type.values)
        nodes.emplace(node.id, &node);
    std::vector<std::string> parts;
    std::unordered_set<std::string> visited;
    while (!id.empty()) {
        if (!nodes.contains(id) || !visited.insert(id).second || parts.size() >= 16)
            invalid("작업 코드의 부모 참조 또는 계층이 올바르지 않습니다.");
        const auto &node = *nodes.at(id);
        parts.push_back(node.name);
        id = node.parentId;
    }
    std::string result;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it)
        result += (result.empty() ? "" : " › ") + *it;
    return result;
}
} // namespace
namespace {
std::vector<std::string> decodeChoices(const std::string &value) {
    constexpr std::string_view prefix = "@choices:";
    if (!value.starts_with(prefix))
        return {value};
    if (value.size() > 65536)
        invalid("다중 선택 값의 길이 한도를 넘었습니다.");
    std::vector<std::string> ids;
    std::size_t at = prefix.size();
    while (at < value.size()) {
        const auto colon = value.find(':', at);
        std::size_t length = 0;
        if (colon == std::string::npos)
            invalid("다중 선택 값의 형식이 올바르지 않습니다.");
        const auto [end, ec] = std::from_chars(value.data() + at, value.data() + colon, length);
        if (ec != std::errc{} || end != value.data() + colon || !length || length > 1024 ||
            length > value.size() - colon - 1 || ids.size() >= 1000)
            invalid("다중 선택 값의 형식·개수를 확인하세요.");
        ids.push_back(value.substr(colon + 1, length));
        at = colon + 1 + length;
    }
    return ids;
}
} // namespace
bool multipleChoice(const CustomField &f) {
    return f.xmlLookup && f.xmlLookup->maxValues != 1;
}
std::string choiceValue(const CustomField &f, const std::vector<std::string> &selected) {
    auto ids = selected;
    if (f.type != CustomFieldType::Choice || ids.empty() || ids.size() > 1000 ||
        (!multipleChoice(f) && ids.size() != 1) ||
        (f.xmlLookup && f.xmlLookup->maxValues > 0 && ids.size() > std::size_t(f.xmlLookup->maxValues)))
        invalid("목록 선택 개수를 확인하세요. 미설정은 값 제거를 사용하세요.");
    std::sort(ids.begin(), ids.end());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if ((i && ids[i] == ids[i - 1]) ||
            std::none_of(f.options.begin(), f.options.end(), [&](const auto &o) { return o.id == ids[i]; }))
            invalid("중복 선택 또는 없는 목록 옵션입니다.");
        if (f.xmlLookup)
            validateXmlCodeSelection(*f.xmlLookup, ids[i]);
        if (f.xmlLookup && f.xmlLookup->leafOnly &&
            std::any_of(f.xmlLookup->values.begin(), f.xmlLookup->values.end(),
                        [&](const auto &o) { return o.parentId == ids[i]; }))
            invalid("이 목록은 마지막 계층 항목만 선택할 수 있습니다.");
    }
    if (!multipleChoice(f))
        return ids.front();
    std::string result = "@choices:";
    for (const auto &id : ids)
        result += std::to_string(id.size()) + ":" + id;
    if (result.size() > 65536)
        invalid("다중 선택 값의 길이 한도를 넘었습니다.");
    return result;
}
std::vector<std::string> choiceSelection(const CustomField &f, const std::string &value) {
    auto ids = multipleChoice(f) ? decodeChoices(value) : std::vector<std::string>{value};
    (void)choiceValue(f, ids);
    std::sort(ids.begin(), ids.end());
    return ids;
}
FieldDuration parseFieldDuration(const std::string &value) {
    auto number = value;
    bool estimated = !number.empty() && number.back() == '?';
    if (estimated)
        number.pop_back();
    bool elapsed = false;
    if (number.ends_with(" em")) {
        elapsed = true;
        number.resize(number.size() - 3);
    } else if (number.ends_with(" m"))
        number.resize(number.size() - 2);
    return {parseFieldNumber(number), elapsed, estimated};
}
std::string formatFieldDuration(const FieldDuration &v) {
    return formatFieldNumber(v.minutes) + (v.elapsed ? " em" : " m") + (v.estimated ? "?" : "");
}
std::int64_t parseFieldCost(const std::string &value, const std::string &currency) {
    if (value.empty() || value.size() > 24)
        invalid("사용자 금액의 길이·형식을 확인하세요.");
    const auto decimals = currencyDecimals(currency);
    std::size_t at = (value[0] == '-' || value[0] == '+') ? 1 : 0;
    const auto start = at;
    std::int64_t amount = 0;
    while (at < value.size() && value[at] >= '0' && value[at] <= '9') {
        const auto digit = value[at++] - '0';
        if (amount > (maxMoney - digit) / 10)
            invalid("사용자 금액 한도를 초과했습니다.");
        amount = amount * 10 + digit;
    }
    if (at == start || amount > maxMoney / (decimals ? 100 : 1))
        invalid("사용자 금액의 형식·한도를 확인하세요.");
    amount *= decimals ? 100 : 1;
    if (at < value.size() && value[at] == '.') {
        if (!decimals)
            invalid("이 통화의 사용자 금액은 정수여야 합니다.");
        ++at;
        const auto first = at;
        int factor = 100;
        while (at < value.size() && value[at] >= '0' && value[at] <= '9' && factor > 1) {
            factor /= 10;
            amount += (value[at++] - '0') * factor;
        }
        if (at == first)
            invalid("소수점 뒤 금액을 입력하세요.");
    }
    if (at != value.size() || amount > maxMoney)
        invalid("사용자 금액의 통화 정밀도·한도를 확인하세요.");
    return value[0] == '-' ? -amount : amount;
}
std::string formatFieldCost(std::int64_t minor, const std::string &currency) {
    if (minor < -maxMoney || minor > maxMoney)
        invalid("사용자 금액 한도를 초과했습니다.");
    const bool negative = minor < 0;
    auto n = negative ? -minor : minor;
    if (!currencyDecimals(currency))
        return std::to_string(minor);
    auto result = (negative ? "-" : "") + std::to_string(n / 100);
    if (n % 100) {
        result += ".";
        result += char('0' + n % 100 / 10);
        if (n % 10)
            result += char('0' + n % 10);
    }
    return result;
}
bool numericCustomField(CustomFieldType t) {
    return t == CustomFieldType::Number || t == CustomFieldType::Cost || t == CustomFieldType::Duration;
}
std::int64_t customFieldNumber(const PlanInput &p, const CustomField &f, const std::string &value) {
    if (f.type == CustomFieldType::Cost)
        return parseFieldCost(value, p.currency) * (fieldNumberScale / (currencyDecimals(p.currency) ? 100 : 1));
    if (f.type == CustomFieldType::Duration)
        return parseFieldDuration(value).minutes;
    return parseFieldNumber(value);
}
std::string normalizeLookupScalar(CustomFieldType type, const std::string &value, const std::string &currency) {
    if (type == CustomFieldType::Number)
        return formatFieldNumber(parseFieldNumber(value));
    if (type == CustomFieldType::Flag) {
        if (value == "1" || value == "true" || value == "예")
            return "1";
        if (value == "0" || value == "false" || value == "아니요")
            return "0";
        invalid("Flag는 참/거짓(1/0)만 입력하세요.");
    }
    if (type == CustomFieldType::Cost)
        return formatFieldCost(parseFieldCost(value, currency), currency);
    if (type == CustomFieldType::Duration)
        return formatFieldDuration(parseFieldDuration(value));
    if (type == CustomFieldType::Start || type == CustomFieldType::Finish)
        return normalizeFieldDateTime(value);
    if (type == CustomFieldType::Date) {
        if (validDate(value))
            return value;
        const auto stamp = normalizeFieldDateTime(value);
        return stamp.size() == 19 && stamp.substr(10) == "T00:00:00" ? stamp.substr(0, 10) : stamp;
    }
    if (type != CustomFieldType::Text || !textValue(value))
        invalid("목록 값의 형식·길이를 확인하세요.");
    return value;
}
std::int64_t parseFieldNumber(const std::string &value) {
    if (value.empty() || value.size() > 20)
        invalid("숫자는 소수 6자리 이내로 입력하세요.");
    std::size_t at = 0;
    const bool negative = value[0] == '-';
    if (negative || value[0] == '+')
        ++at;
    const auto first = at;
    std::int64_t whole = 0, fraction = 0, factor = fieldNumberScale;
    while (at < value.size() && value[at] >= '0' && value[at] <= '9') {
        whole = whole * 10 + value[at++] - '0';
        if (whole > 999999999)
            invalid("숫자 필드 범위는 ±999999999.999999입니다.");
    }
    if (at == first)
        invalid("숫자 필드에는 숫자를 입력하세요.");
    if (at < value.size() && value[at] == '.') {
        const auto decimal = ++at;
        while (at < value.size() && value[at] >= '0' && value[at] <= '9' && factor > 1) {
            factor /= 10;
            fraction += (value[at++] - '0') * factor;
        }
        if (at == decimal)
            invalid("소수점 뒤에 숫자가 필요합니다.");
    }
    if (at != value.size())
        invalid("숫자는 소수 6자리 이내로 입력하세요. 쉼표·지수 표기는 지원하지 않습니다.");
    const auto amount = whole * fieldNumberScale + fraction;
    return negative ? -amount : amount;
}
std::string formatFieldNumber(std::int64_t value) {
    if (value < -999999999999999LL || value > 999999999999999LL)
        invalid("숫자 필드 범위를 초과했습니다.");
    const bool negative = value < 0;
    const auto absolute = negative ? -value : value;
    auto text = std::string(negative ? "-" : "") + std::to_string(absolute / fieldNumberScale);
    if (const auto fraction = absolute % fieldNumberScale) {
        auto decimals = std::to_string(fieldNumberScale + fraction).substr(1);
        while (decimals.back() == '0')
            decimals.pop_back();
        text += "." + decimals;
    }
    return text;
}
std::string fieldTypeToken(CustomFieldType type) {
    switch (type) {
    case CustomFieldType::Text:
        return "text";
    case CustomFieldType::Number:
        return "number";
    case CustomFieldType::Date:
        return "date";
    case CustomFieldType::Choice:
        return "choice";
    case CustomFieldType::Flag:
        return "flag";
    case CustomFieldType::Cost:
        return "cost";
    case CustomFieldType::Duration:
        return "duration";
    case CustomFieldType::Start:
        return "start";
    case CustomFieldType::Finish:
        return "finish";
    }
    invalid("지원하지 않는 사용자 필드 형식입니다.");
}
CustomFieldType fieldTypeFromToken(const std::string &token) {
    if (token == "text")
        return CustomFieldType::Text;
    if (token == "number")
        return CustomFieldType::Number;
    if (token == "date")
        return CustomFieldType::Date;
    if (token == "flag")
        return CustomFieldType::Flag;
    if (token == "cost")
        return CustomFieldType::Cost;
    if (token == "start")
        return CustomFieldType::Start;
    if (token == "finish")
        return CustomFieldType::Finish;
    if (token == "duration")
        return CustomFieldType::Duration;
    if (token == "choice")
        return CustomFieldType::Choice;
    invalid("지원하지 않는 사용자 필드 형식입니다.");
}
const std::vector<CustomField> &customFields(const PlanInput &p, FieldScope scope) {
    switch (scope) {
    case FieldScope::Task:
        return p.customFields;
    case FieldScope::Project:
        return p.projectFields;
    case FieldScope::Resource:
        return p.resourceFields;
    case FieldScope::Assignment:
        return p.assignmentFields;
    }
    invalid("지원하지 않는 사용자 필드 범위입니다.");
}
std::vector<CustomField> &customFields(PlanInput &p, FieldScope scope) {
    // The const overload validates the enum before selecting the mutable dictionary.
    (void)customFields(static_cast<const PlanInput &>(p), scope);
    return scope == FieldScope::Task       ? p.customFields
           : scope == FieldScope::Project  ? p.projectFields
           : scope == FieldScope::Resource ? p.resourceFields
                                           : p.assignmentFields;
}
const CustomField *customField(const PlanInput &p, const std::string &key, FieldScope scope) {
    if (!key.starts_with("field:"))
        return nullptr;
    for (const auto &field : customFields(p, scope))
        if (key.substr(6) == field.id)
            return &field;
    return nullptr;
}
const ActivityCodeType *codeType(const PlanInput &p, const std::string &key) {
    if (!key.starts_with("code:"))
        return nullptr;
    for (const auto &type : p.codeTypes)
        if (key.substr(5) == type.id)
            return &type;
    return nullptr;
}
std::vector<std::string> attributeKeys(const PlanInput &p, FieldScope scope) {
    std::vector<std::string> result;
    for (const auto &field : customFields(p, scope))
        result.push_back("field:" + field.id);
    if (scope == FieldScope::Task)
        for (const auto &type : p.codeTypes)
            result.push_back("code:" + type.id);
    return result;
}
std::string attributeName(const PlanInput &p, const std::string &key, FieldScope scope) {
    if (scope == FieldScope::Task && key == "wbs")
        return "WBS";
    if (const auto *field = customField(p, key, scope))
        return field->name;
    if (const auto *type = scope == FieldScope::Task ? codeType(p, key) : nullptr)
        return type->name;
    invalid("분류 키를 찾을 수 없습니다: " + key);
}
std::string attributeUnit(const PlanInput &p, const std::string &key, FieldScope scope) {
    if (const auto *field = customField(p, key, scope)) {
        if (field->type == CustomFieldType::Cost ||
            (field->xmlLookup && field->xmlLookup->valueType == CustomFieldType::Cost))
            return p.currency;
        return field->unit;
    }
    return {};
}
std::optional<std::string> attributeValue(const Task &task, const std::string &key) {
    if (key == "wbs")
        return task.wbsId.empty() ? std::nullopt : std::optional{task.wbsId};
    return attributeValue(task.attributes, key);
}
std::optional<std::string> attributeValue(const std::vector<TaskAttribute> &attributes, const std::string &key) {
    for (const auto &value : attributes)
        if (value.key == key)
            return value.value;
    return {};
}
std::string normalizeAttributeValue(const PlanInput &p, const std::string &key, const std::string &value,
                                    FieldScope scope, bool allowComputedLiteral) {
    if (scope == FieldScope::Task && key == "wbs") {
        if (std::none_of(p.wbsNodes.begin(), p.wbsNodes.end(), [&](const auto &n) { return n.id == value; }))
            invalid("WBS를 찾을 수 없습니다.");
        return value;
    }
    if (const auto *field = customField(p, key, scope)) {
        if (calculatedField(*field) && !allowComputedLiteral)
            throw AppError(ErrorCode::FORMULA_INVALID, {"계산식 필드의 값은 직접 입력하지 않습니다: " + field->name});
        switch (field->type) {
        case CustomFieldType::Text:
            if (!textValue(value))
                invalid("텍스트 값의 길이 또는 제어문자를 확인하세요.");
            return value;
        case CustomFieldType::Number:
            return formatFieldNumber(parseFieldNumber(value));
        case CustomFieldType::Date:
        case CustomFieldType::Start:
        case CustomFieldType::Finish:
            return normalizeLookupScalar(field->type, value, p.currency);
        case CustomFieldType::Flag:
        case CustomFieldType::Cost:
        case CustomFieldType::Duration:
            return normalizeLookupScalar(field->type, value, p.currency);
        case CustomFieldType::Choice:
            return choiceValue(*field, choiceSelection(*field, value));
        }
    }
    if (const auto *type = scope == FieldScope::Task ? codeType(p, key) : nullptr) {
        if (std::none_of(type->values.begin(), type->values.end(), [&](const auto &o) { return o.id == value; }))
            invalid("작업 코드 값을 찾을 수 없습니다.");
        return value;
    }
    invalid("분류 키를 찾을 수 없습니다: " + key);
}
std::string attributeText(const PlanInput &p, const std::string &key, const std::string &value, FieldScope scope) {
    if (scope == FieldScope::Task && key == "wbs") {
        for (const auto &n : p.wbsNodes)
            if (n.id == value)
                return n.name;
    }
    if (const auto *field = customField(p, key, scope)) {
        if (multipleChoice(*field)) {
            std::string text;
            for (const auto &id : choiceSelection(*field, value)) {
                const auto option = std::find_if(field->options.begin(), field->options.end(),
                                                 [&](const auto &o) { return o.id == id; });
                text += (text.empty() ? "" : ", ") + option->name;
            }
            return text;
        }
        if (field->type == CustomFieldType::Flag)
            return value == "1" ? "예" : "아니요";
        if (field->type == CustomFieldType::Choice)
            for (const auto &option : field->options)
                if (option.id == value)
                    return option.name;
        if (field->type != CustomFieldType::Choice)
            return value.empty() ? "(빈 문자열)" : value;
    }
    if (const auto *type = scope == FieldScope::Task ? codeType(p, key) : nullptr)
        return codePath(*type, value);
    invalid("분류 값을 찾을 수 없습니다.");
}
namespace {
void validateFieldDefinitions(const PlanInput &p, const std::vector<CustomField> &fields) {
    if (fields.size() > 64)
        invalid("범위별 사용자 필드는 최대 64개입니다.");
    std::unordered_set<std::string> fieldIds, fieldNames;
    for (const auto &field : fields) {
        calculationCheckpoint();
        if (!label(field.id) || !label(field.name) || !fieldIds.insert(field.id).second ||
            !fieldNames.insert(field.name).second || (!field.unit.empty() && !label(field.unit, 128)) ||
            field.options.size() > 1000)
            invalid("필드의 ID·이름·단위·개수 또는 중복을 확인하세요.");
        fieldTypeToken(field.type);
        if ((field.type == CustomFieldType::Cost ||
             (field.xmlLookup && field.xmlLookup->valueType == CustomFieldType::Cost)) &&
            !field.unit.empty())
            invalid("금액 필드의 단위는 프로젝트 통화를 따릅니다.");
        if (field.xmlLookup) {
            const auto &l = *field.xmlLookup;
            validateXmlLookupMask(l);
            fieldTypeToken(l.valueType);
            if (l.tableGuid.empty() &&
                (l.leafOnly || !l.tableName.empty() ||
                 std::none_of(l.values.begin(), l.values.end(), [](const auto &v) { return v.listed; })))
                invalid("일반 목록의 계층·이름·빈 사전을 확인하세요.");
            if (field.type != CustomFieldType::Choice || !field.expression.empty() ||
                l.valueType == CustomFieldType::Choice || l.maxValues < 0 || l.maxValues > 1000 ||
                l.values.size() != field.options.size() ||
                (!l.tableGuid.empty() && normalizedGuid(l.tableGuid) != l.tableGuid) ||
                (!l.fieldGuid.empty() && normalizedGuid(l.fieldGuid) != l.fieldGuid) ||
                (!l.fieldName.empty() && !label(l.fieldName)) || (!l.tableName.empty() && !label(l.tableName)))
                invalid("XML 목록의 형식·선택 한도·사전 대응을 확인하세요.");
            std::unordered_set<std::string> ids, guids, mapped, localValues;
            for (const auto &v : l.values) {
                if (!mapped.insert(v.optionId).second ||
                    std::none_of(field.options.begin(), field.options.end(),
                                 [&](const auto &o) { return o.id == v.optionId; }) ||
                    normalizeLookupScalar(l.valueType, v.value, p.currency) != v.value || !textValue(v.description))
                    invalid("XML 목록 항목의 형식·옵션 참조·중복을 확인하세요.");
                if (dateTimeCustomField(l.valueType) && !l.tableGuid.empty() && v.listed)
                    (void)packFieldDateTime(v.value);
                if (l.valueType == CustomFieldType::Duration) {
                    const auto d = parseFieldDuration(v.value);
                    if (d.elapsed || d.estimated || d.minutes % (fieldNumberScale / 10))
                        invalid("기간 목록은 일반 정수 1/10분만 지원합니다.");
                }
                if (l.tableGuid.empty() && !localValues.insert(v.value).second)
                    invalid("일반 목록의 직접 값이 중복됩니다.");
                if (!v.listed && (l.restrictValues || !v.valueId.empty() || !v.guid.empty() || !v.parentId.empty()))
                    invalid("열린 목록의 직접 값은 외부 참조를 가질 수 없습니다.");
                if (v.listed) {
                    if (v.valueId.empty() && v.guid.empty())
                        invalid("XML 목록 항목의 ID/GUID가 없습니다.");
                    if (!v.valueId.empty()) {
                        unsigned n = 0;
                        const auto [end, ec] =
                            std::from_chars(v.valueId.data(), v.valueId.data() + v.valueId.size(), n);
                        if (ec != std::errc{} || end != v.valueId.data() + v.valueId.size() || n > 2147483647 ||
                            std::to_string(n) != v.valueId || !ids.insert(v.valueId).second)
                            invalid("XML 목록의 값 ID·중복을 확인하세요.");
                    }
                    if ((!v.guid.empty() && (normalizedGuid(v.guid) != v.guid || !guids.insert(v.guid).second)) ||
                        (!l.tableGuid.empty() && v.guid.empty()) ||
                        (l.tableGuid.empty() && (!v.guid.empty() || !v.parentId.empty())))
                        invalid("XML 목록의 값 GUID를 확인하세요.");
                }
            }
            for (const auto &v : l.values) {
                std::set<std::string> visited;
                auto current = v.parentId;
                while (!current.empty()) {
                    const auto parent = std::find_if(l.values.begin(), l.values.end(),
                                                     [&](const auto &o) { return o.optionId == current && o.listed; });
                    if (parent == l.values.end() || parent->valueId.empty() || parent->valueId == "0" ||
                        current == v.optionId || !visited.insert(current).second || visited.size() >= 16)
                        invalid("XML 목록 계층의 부모·순환·깊이를 확인하세요.");
                    current = parent->parentId;
                }
            }
            if (!l.defaultOptionId.empty()) {
                (void)choiceValue(field, {l.defaultOptionId});
                if (std::none_of(l.values.begin(), l.values.end(),
                                 [&](const auto &v) { return v.optionId == l.defaultOptionId && v.listed; }))
                    invalid("기본값은 목록 항목이어야 합니다.");
            }
        }
        if (field.type != CustomFieldType::Choice && !field.options.empty())
            invalid("목록 형식만 옵션을 가질 수 있습니다.");
        std::unordered_set<std::string> optionIds, names;
        for (const auto &option : field.options)
            if (!label(option.id) || !label(option.name) || !optionIds.insert(option.id).second ||
                !names.insert(option.name).second)
                invalid("목록 옵션의 ID·이름 또는 중복을 확인하세요.");
    }
}
void validateFieldValues(const PlanInput &p, const std::vector<TaskAttribute> &values, FieldScope scope) {
    if (values.size() > 64)
        invalid("객체별 사용자 필드 값은 최대 64개입니다.");
    std::unordered_set<std::string> keys;
    for (const auto &value : values) {
        calculationCheckpoint();
        if (!value.key.starts_with("field:") || !keys.insert(value.key).second ||
            normalizeAttributeValue(p, value.key, value.value, scope,
                                    customField(p, value.key, scope) &&
                                        customField(p, value.key, scope)->xmlFormula.has_value()) != value.value)
            invalid("사용자 필드 값의 범위·참조·형식·중복을 확인하세요.");
    }
}
} // namespace
void validateClassification(const PlanInput &p) {
    if (p.customFields.size() > 64 || p.codeTypes.size() > 64)
        invalid("필드와 코드 종류는 각각 최대 64개입니다.");
    validateFieldDefinitions(p, p.customFields);
    validateFieldDefinitions(p, p.projectFields);
    validateFieldDefinitions(p, p.resourceFields);
    validateFieldDefinitions(p, p.assignmentFields);
    std::unordered_set<std::string> xmlIds, xmlGuids;
    std::size_t lookupValues = 0;
    std::map<std::string, XmlLookup> sharedLookups;
    std::map<std::string, std::string> lookupGuids, lookupIds;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope)) {
            if (f.xmlLookup) {
                auto table = sharedXmlLookup(*f.xmlLookup);
                if (!table.tableGuid.empty()) {
                    if (sharedLookups.contains(table.tableGuid) && sharedLookups.at(table.tableGuid) != table)
                        invalid("공유 GUID 목록의 정의가 서로 다릅니다.");
                    sharedLookups.emplace(table.tableGuid, table);
                }
                auto owner = table.tableGuid;
                if (owner.empty()) {
                    owner = "local:" + std::to_string(int(table.valueType));
                    for (const auto &v : table.values)
                        for (const auto &part : {v.optionId, v.valueId, v.value, v.parentId})
                            owner += std::to_string(part.size()) + ":" + part;
                }
                for (const auto &v : table.values) {
                    if (!v.guid.empty() && lookupGuids.contains(v.guid) && lookupGuids.at(v.guid) != owner)
                        invalid("다른 목록의 값 GUID가 중복됩니다.");
                    if (!v.guid.empty())
                        lookupGuids.emplace(v.guid, owner);
                    if (!v.valueId.empty() && lookupIds.contains(v.valueId) && lookupIds.at(v.valueId) != owner)
                        invalid("다른 목록의 값 ID가 중복됩니다.");
                    if (!v.valueId.empty())
                        lookupIds.emplace(v.valueId, owner);
                }
                if (!f.xmlIdentity && !f.xmlLookup->fieldGuid.empty() &&
                    !xmlGuids.insert(f.xmlLookup->fieldGuid).second)
                    invalid("XML 필드 GUID가 중복됩니다.");
                lookupValues += f.xmlLookup->values.size();
                if (lookupValues > 100000)
                    invalid("전체 목록 항목은 100,000개까지입니다.");
                if (f.xmlIdentity && !f.xmlLookup->fieldGuid.empty() && f.xmlIdentity->guid != f.xmlLookup->fieldGuid)
                    invalid("목록 필드와 기업 필드 GUID가 서로 다릅니다.");
            }
            if (scope == FieldScope::Assignment && !f.expression.empty())
                invalid("배정 필드는 수동 값만 지원합니다.");
            if (!f.xmlIdentity)
                continue;
            const auto &x = *f.xmlIdentity;
            unsigned id = 0;
            const auto [end, ec] = std::from_chars(x.fieldId.data(), x.fieldId.data() + x.fieldId.size(), id);
            const unsigned base = scope == FieldScope::Task       ? 0x0b408000u
                                  : scope == FieldScope::Project  ? 0x0b608000u
                                  : scope == FieldScope::Resource ? 0x0c408000u
                                                                  : 0x0f408000u;
            const bool localAssignment = scope == FieldScope::Assignment && id >= 0x0f404000u && id <= 0x0f4040c8u;
            if (ec != std::errc{} || end != x.fieldId.data() + x.fieldId.size() || std::to_string(id) != x.fieldId ||
                (!(id >= base && id <= base + 65535) && !localAssignment) || !xmlIds.insert(x.fieldId).second ||
                (!x.fieldName.empty() && !label(x.fieldName)) ||
                (!x.guid.empty() && !xmlGuids.insert(normalizedGuid(x.guid)).second))
                invalid("XML 필드의 ID·범위·GUID·이름 또는 중복을 확인하세요.");
        }
    validateFormulaFields(p);
    validateFieldValues(p, p.projectAttributes, FieldScope::Project);
    for (const auto &resource : p.resources)
        validateFieldValues(p, resource.attributes, FieldScope::Resource);
    for (const auto &assignment : p.assignments)
        validateFieldValues(p, assignment.attributes, FieldScope::Assignment);
    std::unordered_set<std::string> typeIds, typeNames;
    std::size_t totalCodes = 0;
    for (const auto &type : p.codeTypes) {
        calculationCheckpoint();
        totalCodes += type.values.size();
        if (totalCodes > 10000 || !label(type.id) || !label(type.name) || !typeIds.insert(type.id).second ||
            !typeNames.insert(type.name).second)
            invalid("작업 코드 종류·ID·이름·전체 노드 수를 확인하세요.");
        std::unordered_map<std::string, const ActivityCode *> nodes;
        std::unordered_set<std::string> siblings;
        for (const auto &node : type.values) {
            if (!label(node.id) || !label(node.name) || !nodes.emplace(node.id, &node).second ||
                !siblings.insert(std::to_string(node.parentId.size()) + ":" + node.parentId + node.name).second)
                invalid("코드 ID·이름 또는 같은 부모의 중복 이름을 확인하세요.");
        }
        for (const auto &node : type.values) {
            calculationCheckpoint();
            auto id = node.id;
            std::unordered_set<std::string> seen;
            while (!id.empty()) {
                if (!nodes.contains(id) || !seen.insert(id).second || seen.size() > 16)
                    invalid("코드 계층은 순환·누락 부모 없이 16단계 이내여야 합니다.");
                id = nodes.at(id)->parentId;
            }
        }
    }
    for (const auto &task : p.tasks) {
        calculationCheckpoint();
        if (task.attributes.size() > 128)
            invalid("작업 분류 값의 개수 제한을 초과했습니다.");
        std::unordered_set<std::string> keys;
        for (const auto &value : task.attributes) {
            if (value.key == "wbs" || !keys.insert(value.key).second ||
                normalizeAttributeValue(p, value.key, value.value, FieldScope::Task,
                                        customField(p, value.key) &&
                                            customField(p, value.key)->xmlFormula.has_value()) != value.value)
                invalid("작업 분류 값의 참조·형식·중복을 확인하세요: " + task.id);
        }
    }
}
void validateTaskView(const PlanInput &p, const TaskViewSettings &v) {
    if ((!v.wbsId.empty() &&
         std::none_of(p.wbsNodes.begin(), p.wbsNodes.end(), [&](const auto &n) { return n.id == v.wbsId; })) ||
        !textValue(v.query) || v.filter < 0 || v.filter > 3 || v.sort < 0 || v.sort > 6 || v.groupBy.size() > 3 ||
        v.groupPath.size() > v.groupBy.size() || v.attributeFilters.size() > 8 || v.columns.size() > 142)
        invalid("보기의 WBS·필터·정렬·그룹·열 제한을 확인하세요.");
    std::unordered_set<std::string> groups, columns;
    for (std::size_t i = 0; i < v.groupBy.size(); ++i) {
        const auto &key = v.groupBy[i];
        if (!keyExists(p, key) || !groups.insert(key).second)
            invalid("보기 그룹 키가 없거나 중복됩니다.");
        if (i < v.groupPath.size()) {
            const auto &value = v.groupPath[i];
            if (value != "m" &&
                (!value.starts_with("v") ||
                 normalizeAttributeValue(p, key, value.substr(1), FieldScope::Task, true) != value.substr(1)))
                invalid("선택 그룹 값이 올바르지 않습니다.");
        }
    }
    if (!v.sortKey.empty() && !keyExists(p, v.sortKey))
        invalid("분류 정렬 키가 없습니다.");
    for (const auto &filter : v.attributeFilters) {
        if (!keyExists(p, filter.key) || (filter.operation != "equals" && filter.operation != "contains" &&
                                          filter.operation != "missing" && filter.operation != "present"))
            invalid("분류 필터 키·연산이 올바르지 않습니다.");
        if (filter.operation == "equals" &&
            normalizeAttributeValue(p, filter.key, filter.value, FieldScope::Task, true) != filter.value)
            invalid("같음 필터 값이 정규 형식이 아닙니다.");
        if (filter.operation == "contains" && !textValue(filter.value))
            invalid("포함 필터 문자열을 확인하세요.");
        if ((filter.operation == "missing" || filter.operation == "present") && !filter.value.empty())
            invalid("설정 여부 필터는 값이 없어야 합니다.");
    }
    for (const auto &column : v.columns) {
        const bool builtin =
            std::find(builtinTaskColumns.begin(), builtinTaskColumns.end(), column.key) != builtinTaskColumns.end();
        if ((!builtin && (!keyExists(p, column.key) || column.key == "wbs")) || !columns.insert(column.key).second ||
            column.width < 40 || column.width > 2000)
            invalid("보기 열의 참조·폭·중복을 확인하세요.");
    }
}
AttributeLabels attributeLabels(const PlanInput &p, FieldScope scope) {
    AttributeLabels result;
    for (const auto &field : customFields(p, scope))
        if (field.type == CustomFieldType::Flag)
            result["field:" + field.id] = {{"0", "아니요"}, {"1", "예"}};
        else if (field.type == CustomFieldType::Choice) {
            auto &labels = result["field:" + field.id];
            for (const auto &option : field.options)
                labels.emplace(option.id, option.name);
        }
    if (scope != FieldScope::Task)
        return result;
    for (const auto &type : p.codeTypes) {
        std::unordered_map<std::string, const ActivityCode *> nodes;
        for (const auto &node : type.values)
            nodes.emplace(node.id, &node);
        auto &labels = result["code:" + type.id];
        for (const auto &node : type.values) {
            calculationCheckpoint();
            std::vector<std::string> parts;
            auto id = node.id;
            std::unordered_set<std::string> seen;
            while (!id.empty()) {
                if (!nodes.contains(id) || !seen.insert(id).second || parts.size() >= 16)
                    invalid("코드 부모 참조·계층을 확인하세요.");
                parts.push_back(nodes.at(id)->name);
                id = nodes.at(id)->parentId;
            }
            std::string text;
            for (auto it = parts.rbegin(); it != parts.rend(); ++it)
                text += (text.empty() ? "" : " › ") + *it;
            labels.emplace(node.id, std::move(text));
        }
    }
    auto &wbs = result["wbs"];
    for (const auto &node : p.wbsNodes)
        wbs.emplace(node.id, node.name);
    return result;
}
std::string attributeText(const AttributeLabels &labels, const std::string &key, const std::string &value) {
    if (const auto found = labels.find(key); found != labels.end()) {
        if (const auto name = found->second.find(value); name != found->second.end())
            return name->second;
        if (value.starts_with("@choices:")) {
            std::string result;
            for (const auto &id : decodeChoices(value)) {
                const auto name = found->second.find(id);
                if (name == found->second.end())
                    invalid("다중 선택의 표시 이름을 찾을 수 없습니다.");
                result += (result.empty() ? "" : ", ") + name->second;
            }
            return result;
        }
        invalid("분류 값의 표시 이름을 찾을 수 없습니다.");
    }
    return value.empty() ? "(빈 문자열)" : value;
}
void validateSavedTaskViews(const Project &p) {
    if (p.savedTaskViews.size() > 64)
        invalid("저장된 보기는 최대 64개입니다.");
    std::unordered_set<std::string> ids, names;
    for (const auto &view : p.savedTaskViews) {
        calculationCheckpoint();
        if (!label(view.id) || !label(view.name) || !ids.insert(view.id).second || !names.insert(view.name).second)
            invalid("저장된 보기의 ID·이름·중복을 확인하세요.");
        validateTaskView(p, view.settings);
    }
}
bool classificationReferenced(const Project &p, const std::string &key, FieldScope scope) {
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, s))
            if (f.xmlFormula)
                for (const auto &r : f.xmlFormula->references)
                    if (r.scope == scope && r.key == key)
                        return true;

    if (scope == FieldScope::Assignment) {
        for (auto s : {FieldScope::Task, FieldScope::Resource})
            for (const auto &f : customFields(p, s))
                if (f.xmlRollDown && "field:" + f.xmlRollDown->assignmentFieldId == key)
                    return true;
        for (const auto &a : p.assignments)
            if (std::find(a.rollDownMissing.begin(), a.rollDownMissing.end(), key) != a.rollDownMissing.end())
                return true;
    }
    if (scope == FieldScope::Assignment)
        return std::any_of(p.assignments.begin(), p.assignments.end(),
                           [&](const auto &a) { return attributeValue(a.attributes, key).has_value(); });
    if (scope == FieldScope::Project)
        return attributeValue(p.projectAttributes, key).has_value();
    if (scope == FieldScope::Resource) {
        if (std::any_of(p.resources.begin(), p.resources.end(),
                        [&](const auto &r) { return attributeValue(r.attributes, key).has_value(); }))
            return true;
        for (const auto &view : p.savedResourceViews) {
            const auto &v = view.settings;
            if (v.sortKey == key || std::find(v.groupBy.begin(), v.groupBy.end(), key) != v.groupBy.end())
                return true;
            for (const auto &f : v.filters)
                if (f.key == key)
                    return true;
        }
        return false;
    }
    (void)customFields(p, scope);
    if (attributeValue(p.taskSummaryAttributes, key))
        return true;
    for (const auto &g : p.wbsNodes)
        if (attributeValue(g.attributes, key))
            return true;
    for (const auto &task : p.tasks)
        if (attributeValue(task, key))
            return true;
    for (const auto &view : p.savedTaskViews) {
        const auto &v = view.settings;
        if (v.sortKey == key || std::find(v.groupBy.begin(), v.groupBy.end(), key) != v.groupBy.end())
            return true;
        for (const auto &f : v.attributeFilters)
            if (f.key == key)
                return true;
        for (const auto &c : v.columns)
            if (c.key == key)
                return true;
    }
    return false;
}
bool hasObjectFields(const PlanInput &p) {
    return !p.projectFields.empty() || !p.resourceFields.empty() || !p.assignmentFields.empty() ||
           !p.projectAttributes.empty() ||
           std::any_of(p.resources.begin(), p.resources.end(), [](const auto &r) { return !r.attributes.empty(); });
}
bool hasClassification(const PlanInput &p) {
    return hasObjectFields(p) || !p.customFields.empty() || !p.codeTypes.empty() ||
           std::any_of(p.tasks.begin(), p.tasks.end(), [](const auto &t) { return !t.attributes.empty(); });
}
bool hasExtendedXmlFields(const PlanInput &p) {
    if (!p.assignmentFields.empty() ||
        std::any_of(p.assignments.begin(), p.assignments.end(), [](const auto &a) { return !a.attributes.empty(); }))
        return true;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource})
        for (const auto &f : customFields(p, scope))
            if (f.xmlIdentity)
                return true;
    return false;
}
std::string assignmentAttributeId(const Assignment &a) {
    return std::to_string(a.taskId.size()) + ":" + a.taskId + a.resourceId;
}
bool hasXmlLookups(const PlanInput &p) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlLookup)
                return true;
    return false;
}
static int nativeSchemaForImpl(const Project &p, bool reading) {
    const auto materialFloat = [](const Project &v) {
        return std::any_of(v.baselines.begin(), v.baselines.end(),
                           [](const auto &b) { return b.engineVersion == materialFloatEngine; });
    };
    if (materialFloat(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && materialFloat(*e.before)) || (e.after && materialFloat(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && materialFloat(*e.before)) || (e.after && materialFloat(*e.after));
        }))
        return 84;
    const auto actualMaterial = [](const Project &v) {
        if (!v.materialActuals.empty() || v.resourcePlan.autoDelayRemainingForReceipts)
            return true;
        for (const auto &s : v.scenarios)
            if (!s.materialActuals.empty() || s.input.resourcePlan.autoDelayRemainingForReceipts)
                return true;
        for (const auto &b : v.baselines)
            if (b.input.resourcePlan.autoDelayRemainingForReceipts)
                return true;
        for (const auto &e : v.xerScheduleAdoptions)
            if (e.before.resourcePlan.autoDelayRemainingForReceipts ||
                e.after.resourcePlan.autoDelayRemainingForReceipts ||
                e.source.input.resourcePlan.autoDelayRemainingForReceipts)
                return true;
        for (const auto &e : v.levelingDecisions)
            for (const auto *s : {&e.beforePlan, &e.afterPlan, &e.beforeRemaining, &e.afterRemaining})
                if (*s && (*s)->policy == remainingMaterialPolicy)
                    return true;
        return false;
    };
    if (actualMaterial(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && actualMaterial(*e.before)) || (e.after && actualMaterial(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && actualMaterial(*e.before)) || (e.after && actualMaterial(*e.after));
        }))
        return 83;
    const auto autoMaterial = [](const Project &v) {
        for (const auto &e : v.levelingDecisions)
            for (const auto *s : {&e.beforePlan, &e.afterPlan, &e.beforeRemaining, &e.afterRemaining})
                if (*s && (*s)->policy == materialArrivalPolicy)
                    return true;
        if (v.resourcePlan.autoDelayForReceipts)
            return true;
        for (const auto &b : v.baselines)
            if (b.input.resourcePlan.autoDelayForReceipts)
                return true;
        for (const auto &s : v.scenarios)
            if (s.input.resourcePlan.autoDelayForReceipts)
                return true;
        for (const auto &e : v.xerScheduleAdoptions)
            if (e.before.resourcePlan.autoDelayForReceipts || e.after.resourcePlan.autoDelayForReceipts ||
                e.source.input.resourcePlan.autoDelayForReceipts)
                return true;
        return false;
    };
    if (autoMaterial(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && autoMaterial(*e.before)) || (e.after && autoMaterial(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && autoMaterial(*e.before)) || (e.after && autoMaterial(*e.after));
        }))
        return 82;
    const auto inventory = [](const Project &v) {
        if (hasMaterialInventory(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasMaterialInventory(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (hasMaterialInventory(s.input))
                return true;
        for (const auto &e : v.xerScheduleAdoptions)
            if (hasMaterialInventory(e.before) || hasMaterialInventory(e.after) || hasMaterialInventory(e.source.input))
                return true;
        return false;
    };
    if (inventory(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && inventory(*e.before)) || (e.after && inventory(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && inventory(*e.before)) || (e.after && inventory(*e.after));
        }))
        return 81;
    const auto load = [](const Project &v) {
        if (hasXmlResourceLoadFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlResourceLoadFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlResourceLoadFunctions(s.input, true))
                return true;
        for (const auto &e : v.xerScheduleAdoptions)
            if (hasXmlResourceLoadFunctions(e.before, true) || hasXmlResourceLoadFunctions(e.after, true) ||
                hasXmlResourceLoadFunctions(e.source.input, true))
                return true;
        return false;
    };
    if (load(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) { return (e.before && load(*e.before)) || (e.after && load(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && load(*e.before)) || (e.after && load(*e.after)); }))
        return 80;
    const auto availability = [](const Project &v) {
        const auto plan = [](const PlanInput &a) {
            return std::any_of(a.resources.begin(), a.resources.end(), hasResourceAvailability);
        };
        if (plan(v))
            return true;
        for (const auto &b : v.baselines)
            if (plan(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (plan(s.input))
                return true;
        for (const auto &e : v.xerScheduleAdoptions)
            if (plan(e.before) || plan(e.after) || plan(e.source.input))
                return true;
        return false;
    };
    if (availability(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && availability(*e.before)) || (e.after && availability(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && availability(*e.before)) || (e.after && availability(*e.after));
        }))
        return 79;
    const auto units = [](const Project &v) {
        const auto plan = [](const PlanInput &a) {
            return hasXmlUnitReferences(a) || hasXmlAvailabilityFunctions(a, true);
        };
        if (plan(v))
            return true;
        for (const auto &b : v.baselines)
            if (plan(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (plan(s.input))
                return true;
        for (const auto &e : v.xerScheduleAdoptions)
            if (plan(e.before) || plan(e.after) || plan(e.source.input))
                return true;
        return false;
    };
    if (units(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) { return (e.before && units(*e.before)) || (e.after && units(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && units(*e.before)) || (e.after && units(*e.after)); }))
        return 78;
    const auto outsideTimes = [](const Project &v) {
        const auto data = [](const XmlBaselineSet &s) {
            return std::any_of(s.rows.begin(), s.rows.end(), [](const auto &r) { return r.timephasedOutside; });
        };
        const auto plan = [&](const PlanInput &a) { return a.xmlBaselines && data(*a.xmlBaselines); };
        if (plan(v))
            return true;
        for (const auto &b : v.baselines)
            if (plan(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (plan(s.input))
                return true;
        for (const auto &e : v.xerScheduleAdoptions)
            if (plan(e.before) || plan(e.after) || plan(e.source.input))
                return true;
        for (const auto &e : v.xmlBaselineMerges)
            if (data(e.incoming) || data(e.after) || (e.before && data(*e.before)))
                return true;
        return false;
    };
    if (outsideTimes(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && outsideTimes(*e.before)) || (e.after && outsideTimes(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && outsideTimes(*e.before)) || (e.after && outsideTimes(*e.after));
        }))
        return 77;
    const auto merged = [](const Project &v) {
        const auto plan = [](const PlanInput &a) { return a.xmlBaselines && !a.xmlBaselines->origins.empty(); };
        if (!v.xmlBaselineMerges.empty() || plan(v))
            return true;
        for (const auto &b : v.baselines)
            if (plan(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (plan(s.input))
                return true;
        for (const auto &e : v.xerScheduleAdoptions)
            if (plan(e.before) || plan(e.after) || plan(e.source.input))
                return true;
        return false;
    };
    if (merged(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) { return (e.before && merged(*e.before)) || (e.after && merged(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && merged(*e.before)) || (e.after && merged(*e.after)); }))
        return 76;
    const auto externalBaselines = [](const Project &v) {
        if (v.xmlBaselines)
            return true;
        for (const auto &b : v.baselines)
            if (b.input.xmlBaselines)
                return true;
        for (const auto &s : v.scenarios)
            if (s.input.xmlBaselines)
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (a.before.xmlBaselines || a.after.xmlBaselines || a.source.input.xmlBaselines)
                return true;
        return false;
    };
    if (externalBaselines(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && externalBaselines(*e.before)) || (e.after && externalBaselines(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && externalBaselines(*e.before)) || (e.after && externalBaselines(*e.after));
        }))
        return 75;
    const auto summaryDurations = [reading](const Project &v) {
        if (hasXmlSummaryDurationReferences(v, reading))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlSummaryDurationReferences(b.input, reading))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlSummaryDurationReferences(s.input, reading))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlSummaryDurationReferences(a.before, reading) ||
                hasXmlSummaryDurationReferences(a.after, reading) ||
                hasXmlSummaryDurationReferences(a.source.input, reading))
                return true;
        return false;
    };
    if (summaryDurations(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && summaryDurations(*e.before)) || (e.after && summaryDurations(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && summaryDurations(*e.before)) || (e.after && summaryDurations(*e.after));
        }))
        return 74;
    const auto numberedBaselines = [](const Project &v) {
        if (hasXmlNumberedBaselineReferences(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlNumberedBaselineReferences(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlNumberedBaselineReferences(s.input))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlNumberedBaselineReferences(a.before) || hasXmlNumberedBaselineReferences(a.after) ||
                hasXmlNumberedBaselineReferences(a.source.input))
                return true;
        return false;
    };
    if (numberedBaselines(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && numberedBaselines(*e.before)) || (e.after && numberedBaselines(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && numberedBaselines(*e.before)) || (e.after && numberedBaselines(*e.after));
        }))
        return 73;
    const auto depreciation = [](const Project &v) {
        if (hasXmlDepreciationFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlDepreciationFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlDepreciationFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlDepreciationFunctions(a.before, true) || hasXmlDepreciationFunctions(a.after, true) ||
                hasXmlDepreciationFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (depreciation(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && depreciation(*e.before)) || (e.after && depreciation(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && depreciation(*e.before)) || (e.after && depreciation(*e.after));
        }))
        return 72;
    const auto partition = [](const Project &v) {
        if (hasXmlPartitionFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlPartitionFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlPartitionFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlPartitionFunctions(a.before, true) || hasXmlPartitionFunctions(a.after, true) ||
                hasXmlPartitionFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (partition(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && partition(*e.before)) || (e.after && partition(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && partition(*e.before)) || (e.after && partition(*e.after));
        }))
        return 71;
    const auto colors = [](const Project &v) {
        if (hasXmlColorFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlColorFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlColorFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlColorFunctions(a.before, true) || hasXmlColorFunctions(a.after, true) ||
                hasXmlColorFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (colors(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) { return (e.before && colors(*e.before)) || (e.after && colors(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && colors(*e.before)) || (e.after && colors(*e.after)); }))
        return 70;
    const auto numericDisplay = [](const Project &v) {
        if (hasXmlNumericDisplayFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlNumericDisplayFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlNumericDisplayFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlNumericDisplayFunctions(a.before, true) || hasXmlNumericDisplayFunctions(a.after, true) ||
                hasXmlNumericDisplayFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (numericDisplay(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && numericDisplay(*e.before)) || (e.after && numericDisplay(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && numericDisplay(*e.before)) || (e.after && numericDisplay(*e.after));
        }))
        return 69;
    const auto dateDisplay = [](const Project &v) {
        if (hasXmlDateDisplayFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlDateDisplayFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlDateDisplayFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlDateDisplayFunctions(a.before, true) || hasXmlDateDisplayFunctions(a.after, true) ||
                hasXmlDateDisplayFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (dateDisplay(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && dateDisplay(*e.before)) || (e.after && dateDisplay(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && dateDisplay(*e.before)) || (e.after && dateDisplay(*e.after));
        }))
        return 68;
    const auto dateNames = [](const Project &v) {
        if (hasXmlDateNameFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlDateNameFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlDateNameFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlDateNameFunctions(a.before, true) || hasXmlDateNameFunctions(a.after, true) ||
                hasXmlDateNameFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (dateNames(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && dateNames(*e.before)) || (e.after && dateNames(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && dateNames(*e.before)) || (e.after && dateNames(*e.after));
        }))
        return 67;
    const auto unicode = [](const Project &v) {
        if (hasXmlUnicodeFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlUnicodeFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlUnicodeFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlUnicodeFunctions(a.before, true) || hasXmlUnicodeFunctions(a.after, true) ||
                hasXmlUnicodeFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (unicode(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && unicode(*e.before)) || (e.after && unicode(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && unicode(*e.before)) || (e.after && unicode(*e.after)); }))
        return 66;
    const auto like = [](const Project &v) {
        if (hasXmlLikeOperator(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlLikeOperator(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlLikeOperator(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlLikeOperator(a.before, true) || hasXmlLikeOperator(a.after, true) ||
                hasXmlLikeOperator(a.source.input, true))
                return true;
        return false;
    };
    if (like(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) { return (e.before && like(*e.before)) || (e.after && like(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && like(*e.before)) || (e.after && like(*e.after)); }))
        return 65;
    const auto logical = [](const Project &v) {
        if (hasXmlLogicalOperators(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlLogicalOperators(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlLogicalOperators(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlLogicalOperators(a.before, true) || hasXmlLogicalOperators(a.after, true) ||
                hasXmlLogicalOperators(a.source.input, true))
                return true;
        return false;
    };
    if (logical(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && logical(*e.before)) || (e.after && logical(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && logical(*e.before)) || (e.after && logical(*e.after)); }))
        return 64;
    const auto arithmetic = [](const Project &v) {
        if (hasXmlArithmeticOperators(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlArithmeticOperators(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlArithmeticOperators(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlArithmeticOperators(a.before, true) || hasXmlArithmeticOperators(a.after, true) ||
                hasXmlArithmeticOperators(a.source.input, true))
                return true;
        return false;
    };
    if (arithmetic(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && arithmetic(*e.before)) || (e.after && arithmetic(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && arithmetic(*e.before)) || (e.after && arithmetic(*e.after));
        }))
        return 63;
    const auto baselineCosts = [](const Project &v) {
        if (hasXmlBaselineCostReferences(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlBaselineCostReferences(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlBaselineCostReferences(s.input))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlBaselineCostReferences(a.before) || hasXmlBaselineCostReferences(a.after) ||
                hasXmlBaselineCostReferences(a.source.input))
                return true;
        return false;
    };
    if (baselineCosts(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && baselineCosts(*e.before)) || (e.after && baselineCosts(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && baselineCosts(*e.before)) || (e.after && baselineCosts(*e.after));
        }))
        return 62;
    const auto projectConversions = [](const Project &v) {
        if (hasXmlProjectConversionFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlProjectConversionFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlProjectConversionFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlProjectConversionFunctions(a.before, true) || hasXmlProjectConversionFunctions(a.after, true) ||
                hasXmlProjectConversionFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (projectConversions(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && projectConversions(*e.before)) || (e.after && projectConversions(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && projectConversions(*e.before)) || (e.after && projectConversions(*e.after));
        }))
        return 61;
    const auto conversions = [](const Project &v) {
        if (hasXmlConversionFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlConversionFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlConversionFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlConversionFunctions(a.before, true) || hasXmlConversionFunctions(a.after, true) ||
                hasXmlConversionFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (conversions(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && conversions(*e.before)) || (e.after && conversions(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && conversions(*e.before)) || (e.after && conversions(*e.after));
        }))
        return 60;
    const auto strings = [](const Project &v) {
        if (hasXmlStringFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlStringFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlStringFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlStringFunctions(a.before, true) || hasXmlStringFunctions(a.after, true) ||
                hasXmlStringFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (strings(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && strings(*e.before)) || (e.after && strings(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && strings(*e.before)) || (e.after && strings(*e.after)); }))
        return 59;
    const auto math = [](const Project &v) {
        if (hasXmlMathFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlMathFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlMathFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlMathFunctions(a.before, true) || hasXmlMathFunctions(a.after, true) ||
                hasXmlMathFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (math(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) { return (e.before && math(*e.before)) || (e.after && math(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && math(*e.before)) || (e.after && math(*e.after)); }))
        return 58;
    const auto numberText = [](const Project &v) {
        if (hasXmlNumberTextFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlNumberTextFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlNumberTextFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlNumberTextFunctions(a.before, true) || hasXmlNumberTextFunctions(a.after, true) ||
                hasXmlNumberTextFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (numberText(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && numberText(*e.before)) || (e.after && numberText(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && numberText(*e.before)) || (e.after && numberText(*e.after));
        }))
        return 57;
    const auto times = [](const Project &v) {
        if (hasXmlTimeFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlTimeFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlTimeFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlTimeFunctions(a.before, true) || hasXmlTimeFunctions(a.after, true) ||
                hasXmlTimeFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (times(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) { return (e.before && times(*e.before)) || (e.after && times(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && times(*e.before)) || (e.after && times(*e.after)); }))
        return 56;
    const auto hasDates = [reading](const PlanInput &v) {
        if (!reading)
            return hasXmlExtendedDateFunctions(v, true);
        for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
            for (const auto &f : customFields(v, scope))
                if (f.xmlFormula && f.xmlFormula->enabled && xmlNewDateSyntax(*f.xmlFormula))
                    return true;
        return false;
    };
    const auto extendedDates = [&](const Project &v) {
        if (hasDates(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasDates(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (hasDates(s.input))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasDates(a.before) || hasDates(a.after) || hasDates(a.source.input))
                return true;
        return false;
    };
    if (extendedDates(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && extendedDates(*e.before)) || (e.after && extendedDates(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && extendedDates(*e.before)) || (e.after && extendedDates(*e.after));
        }))
        return 55;
    const auto formatting = [](const Project &v) {
        if (hasXmlFormattingFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlFormattingFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlFormattingFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlFormattingFunctions(a.before, true) || hasXmlFormattingFunctions(a.after, true) ||
                hasXmlFormattingFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (formatting(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && formatting(*e.before)) || (e.after && formatting(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && formatting(*e.before)) || (e.after && formatting(*e.after));
        }))
        return 54;
    const auto timeZones = [](const Project &v) {
        if (hasXmlTimeZoneFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlTimeZoneFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlTimeZoneFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlTimeZoneFunctions(a.before, true) || hasXmlTimeZoneFunctions(a.after, true) ||
                hasXmlTimeZoneFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (timeZones(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && timeZones(*e.before)) || (e.after && timeZones(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && timeZones(*e.before)) || (e.after && timeZones(*e.after));
        }))
        return 53;
    const auto additional = [](const Project &v) {
        if (hasXmlAdditionalFunctions(v, true))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlAdditionalFunctions(b.input, true))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlAdditionalFunctions(s.input, true))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlAdditionalFunctions(a.before, true) || hasXmlAdditionalFunctions(a.after, true) ||
                hasXmlAdditionalFunctions(a.source.input, true))
                return true;
        return false;
    };
    if (additional(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && additional(*e.before)) || (e.after && additional(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && additional(*e.before)) || (e.after && additional(*e.after));
        }))
        return 52;
    const auto timing = [](const Project &v) {
        if (hasXmlEarlyLateReferences(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlEarlyLateReferences(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlEarlyLateReferences(s.input))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlEarlyLateReferences(a.before) || hasXmlEarlyLateReferences(a.after) ||
                hasXmlEarlyLateReferences(a.source.input))
                return true;
        return false;
    };
    if (timing(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) { return (e.before && timing(*e.before)) || (e.after && timing(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && timing(*e.before)) || (e.after && timing(*e.after)); }))
        return 51;
    const auto baseline = [](const Project &v) {
        if (hasXmlBaselineReferences(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlBaselineReferences(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlBaselineReferences(s.input))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlBaselineReferences(a.before) || hasXmlBaselineReferences(a.after) ||
                hasXmlBaselineReferences(a.source.input))
                return true;
        return false;
    };
    if (baseline(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && baseline(*e.before)) || (e.after && baseline(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && baseline(*e.before)) || (e.after && baseline(*e.after));
        }))
        return 50;
    const auto remaining = [](const Project &v) {
        if (hasXmlRemainingReferences(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlRemainingReferences(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlRemainingReferences(s.input))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlRemainingReferences(a.before) || hasXmlRemainingReferences(a.after) ||
                hasXmlRemainingReferences(a.source.input))
                return true;
        return false;
    };
    if (remaining(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && remaining(*e.before)) || (e.after && remaining(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && remaining(*e.before)) || (e.after && remaining(*e.after));
        }))
        return 49;
    const auto actuals = [](const Project &v, bool quantities = false) {
        const auto has = [&](const PlanInput &input) {
            return quantities ? hasXmlActualQuantityReferences(input) : hasXmlActualReferences(input);
        };
        if (has(v))
            return true;
        for (const auto &b : v.baselines)
            if (has(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (has(s.input))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (has(a.before) || has(a.after) || has(a.source.input))
                return true;
        return false;
    };
    if (actuals(p, true) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && actuals(*e.before, true)) || (e.after && actuals(*e.after, true));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && actuals(*e.before, true)) || (e.after && actuals(*e.after, true));
        }))
        return 48;
    if (actuals(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && actuals(*e.before)) || (e.after && actuals(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && actuals(*e.before)) || (e.after && actuals(*e.after)); }))
        return 47;

    const auto calendared = [](const Project &v) {
        if ((hasXmlCalendarFunctions(v) || !v.xmlFormulaCalendarId.empty()))
            return true;
        for (const auto &b : v.baselines)
            if ((hasXmlCalendarFunctions(b.input) || !b.input.xmlFormulaCalendarId.empty()))
                return true;
        for (const auto &s : v.scenarios)
            if ((hasXmlCalendarFunctions(s.input) || !s.input.xmlFormulaCalendarId.empty()))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if ((hasXmlCalendarFunctions(a.before) || !a.before.xmlFormulaCalendarId.empty()) ||
                (hasXmlCalendarFunctions(a.after) || !a.after.xmlFormulaCalendarId.empty()) ||
                (hasXmlCalendarFunctions(a.source.input) || !a.source.input.xmlFormulaCalendarId.empty()))
                return true;
        return false;
    };
    if (calendared(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && calendared(*e.before)) || (e.after && calendared(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && calendared(*e.before)) || (e.after && calendared(*e.after));
        }))
        return 46;

    const auto scheduled = [](const Project &v) {
        if (hasXmlScheduleReferences(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlScheduleReferences(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlScheduleReferences(s.input))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlScheduleReferences(a.before) || hasXmlScheduleReferences(a.after) ||
                hasXmlScheduleReferences(a.source.input))
                return true;
        return false;
    };
    if (scheduled(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && scheduled(*e.before)) || (e.after && scheduled(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && scheduled(*e.before)) || (e.after && scheduled(*e.after));
        }))
        return 45;

    const auto xmlFormulaProject = [](const Project &v) {
        if (hasXmlFormulas(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlFormulas(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlFormulas(s.input))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlFormulas(a.before) || hasXmlFormulas(a.after) || hasXmlFormulas(a.source.input))
                return true;
        return false;
    };
    if (xmlFormulaProject(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && xmlFormulaProject(*e.before)) || (e.after && xmlFormulaProject(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && xmlFormulaProject(*e.before)) || (e.after && xmlFormulaProject(*e.after));
        }))
        return 44;

    const auto rolled = [](const Project &v) {
        if (hasXmlRollUp(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlRollUp(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlRollUp(s.input))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlRollUp(a.before) || hasXmlRollUp(a.after) || hasXmlRollUp(a.source.input))
                return true;
        return false;
    };
    if (rolled(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) { return (e.before && rolled(*e.before)) || (e.after && rolled(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && rolled(*e.before)) || (e.after && rolled(*e.after)); }))
        return 43;
    const auto rollDownProject = [](const Project &v) {
        if (hasXmlRollDown(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlRollDown(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlRollDown(s.input))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlRollDown(a.before) || hasXmlRollDown(a.after) || hasXmlRollDown(a.source.input))
                return true;
        return false;
    };
    if (rollDownProject(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && rollDownProject(*e.before)) || (e.after && rollDownProject(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && rollDownProject(*e.before)) || (e.after && rollDownProject(*e.after));
        }))
        return 42;
    const auto maskedProject = [](const Project &v) {
        if (hasXmlCodeMasks(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasXmlCodeMasks(b.input))
                return true;
        for (const auto &s : v.scenarios)
            if (hasXmlCodeMasks(s.input))
                return true;
        for (const auto &a : v.xerScheduleAdoptions)
            if (hasXmlCodeMasks(a.before) || hasXmlCodeMasks(a.after) || hasXmlCodeMasks(a.source.input))
                return true;
        return false;
    };
    if (maskedProject(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && maskedProject(*e.before)) || (e.after && maskedProject(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && maskedProject(*e.before)) || (e.after && maskedProject(*e.after));
        }))
        return 41;
    const auto dateTimeProject = [](const Project &v) {
        if (hasDateTimeFields(v))
            return true;
        for (const auto &b : v.baselines)
            if (hasDateTimeFields(b.input))
                return true;
        for (const auto &b : v.scenarios)
            if (hasDateTimeFields(b.input))
                return true;
        for (const auto &b : v.xerScheduleAdoptions)
            if (hasDateTimeFields(b.before) || hasDateTimeFields(b.after) || hasDateTimeFields(b.source.input))
                return true;
        return false;
    };
    if (dateTimeProject(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && dateTimeProject(*e.before)) || (e.after && dateTimeProject(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && dateTimeProject(*e.before)) || (e.after && dateTimeProject(*e.after));
        }))
        return 40;
    const auto typedInput = [](const PlanInput &v) {
        for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
            for (const auto &f : customFields(v, scope))
                if (int(f.type) >= int(CustomFieldType::Flag) ||
                    (f.xmlLookup && int(f.xmlLookup->valueType) >= int(CustomFieldType::Flag)))
                    return true;
        return false;
    };
    const auto typed = [&](const Project &v) {
        if (typedInput(v))
            return true;
        for (const auto &b : v.baselines)
            if (typedInput(b.input))
                return true;
        for (const auto &b : v.scenarios)
            if (typedInput(b.input))
                return true;
        for (const auto &b : v.xerScheduleAdoptions)
            if (typedInput(b.before) || typedInput(b.after) || typedInput(b.source.input))
                return true;
        return false;
    };
    if (typed(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) { return (e.before && typed(*e.before)) || (e.after && typed(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && typed(*e.before)) || (e.after && typed(*e.after)); }))
        return 39;
    const auto lookups = [](const Project &v) {
        return hasXmlLookups(v) ||
               std::any_of(v.baselines.begin(), v.baselines.end(),
                           [](const auto &b) { return hasXmlLookups(b.input); }) ||
               std::any_of(v.scenarios.begin(), v.scenarios.end(),
                           [](const auto &b) { return hasXmlLookups(b.input); }) ||
               std::any_of(v.xerScheduleAdoptions.begin(), v.xerScheduleAdoptions.end(), [](const auto &b) {
                   return hasXmlLookups(b.before) || hasXmlLookups(b.after) || hasXmlLookups(b.source.input);
               });
    };
    if (lookups(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && lookups(*e.before)) || (e.after && lookups(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(),
                    [&](const auto &e) { return (e.before && lookups(*e.before)) || (e.after && lookups(*e.after)); }))
        return 38;
    const auto extendedXml = [](const Project &v) {
        return hasExtendedXmlFields(v) ||
               std::any_of(v.baselines.begin(), v.baselines.end(),
                           [](const auto &b) { return hasExtendedXmlFields(b.input); }) ||
               std::any_of(v.scenarios.begin(), v.scenarios.end(),
                           [](const auto &s) { return hasExtendedXmlFields(s.input); }) ||
               std::any_of(v.xerScheduleAdoptions.begin(), v.xerScheduleAdoptions.end(), [](const auto &s) {
                   return hasExtendedXmlFields(s.before) || hasExtendedXmlFields(s.after) ||
                          hasExtendedXmlFields(s.source.input);
               });
    };
    if (extendedXml(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && extendedXml(*e.before)) || (e.after && extendedXml(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && extendedXml(*e.before)) || (e.after && extendedXml(*e.after));
        }))
        return 37;
    const auto metadata = [](const Project &v) {
        return hasResourcePlanning(v) ||
               std::any_of(v.baselines.begin(), v.baselines.end(),
                           [](const auto &b) { return hasResourcePlanning(b.input); }) ||
               std::any_of(v.scenarios.begin(), v.scenarios.end(),
                           [](const auto &s) { return hasResourcePlanning(s.input); }) ||
               std::any_of(v.xerScheduleAdoptions.begin(), v.xerScheduleAdoptions.end(), [](const auto &s) {
                   return hasResourcePlanning(s.before) || hasResourcePlanning(s.after) ||
                          hasResourcePlanning(s.source.input);
               });
    };
    if (metadata(p) ||
        std::any_of(
            p.currencyConversions.begin(), p.currencyConversions.end(),
            [&](const auto &e) { return (e.before && metadata(*e.before)) || (e.after && metadata(*e.after)); }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && metadata(*e.before)) || (e.after && metadata(*e.after));
        }))
        return 36;
    const auto datedSelection = [](const auto &s) { return s && s->policy == datedEffortPolicy; };
    const auto advancedDated = [&](const Project &v) {
        return hasDatedEffort(v) || datedSelection(v.planLeveling) || datedSelection(v.remainingLeveling) ||
               std::any_of(v.scenarios.begin(), v.scenarios.end(),
                           [&](const auto &s) {
                               Project frozen;
                               static_cast<PlanInput &>(frozen) = s.input;
                               frozen.remainingWork = s.remainingWork;
                               return s.policy == datedEffortPolicy || hasDatedEffort(frozen);
                           }) ||
               std::any_of(v.levelingDecisions.begin(), v.levelingDecisions.end(), [&](const auto &e) {
                   return datedSelection(e.beforePlan) || datedSelection(e.afterPlan) ||
                          datedSelection(e.beforeRemaining) || datedSelection(e.afterRemaining);
               });
    };
    if (advancedDated(p) ||
        std::any_of(p.currencyConversions.begin(), p.currencyConversions.end(),
                    [&](const auto &e) {
                        return (e.before && advancedDated(*e.before)) || (e.after && advancedDated(*e.after));
                    }) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && advancedDated(*e.before)) || (e.after && advancedDated(*e.after));
        }))
        return 35;
    const auto currencySteps = [](const Project &v) {
        return std::any_of(v.xerFinancialRevisions.begin(), v.xerFinancialRevisions.end(),
                           [](const auto &r) { return r.relink && !r.relink->incomingCurrencySteps.empty(); });
    };
    if (!p.currencyConversions.empty() || currencySteps(p) ||
        std::any_of(p.ledgerIdMigrations.begin(), p.ledgerIdMigrations.end(), [&](const auto &e) {
            return (e.before && currencySteps(*e.before)) || (e.after && currencySteps(*e.after));
        }))
        return 34;
    if (!p.ledgerIdMigrations.empty())
        return 33;
    if (!p.xerScheduleAdoptions.empty())
        return 32;
    for (const auto &r : p.xerFinancialRevisions)
        if (r.relink && (r.relink->incomingConversion || r.relink->previousConversion))
            return 31;
    if (!p.financialPeriodEvents.empty())
        return 30;
    if (!p.externalEvmAdoptions.empty())
        return 29;
    if (std::any_of(p.xerFinancialRevisions.begin(), p.xerFinancialRevisions.end(),
                    [](const auto &r) { return r.relink.has_value(); }))
        return 28;
    if (xerFinancialsNeedSchema27(p))
        return 27;
    if (!p.xerFinancialRevisions.empty())
        return 26;
    if (p.xerFinancials)
        return 25;
    const auto workPolicy = [](const auto &s) { return s && s->policy == "serial-effort-work-periods-v1"; };
    if (hasWorkAssignmentPeriods(p) || workPolicy(p.planLeveling) || workPolicy(p.remainingLeveling) ||
        std::any_of(p.baselines.begin(), p.baselines.end(),
                    [](const auto &b) { return hasWorkAssignmentPeriods(b.input); }) ||
        std::any_of(p.scenarios.begin(), p.scenarios.end(),
                    [](const auto &s) {
                        return hasWorkAssignmentPeriods(s.input) || s.policy == "serial-effort-work-periods-v1";
                    }) ||
        std::any_of(p.levelingDecisions.begin(), p.levelingDecisions.end(), [&](const auto &e) {
            return workPolicy(e.beforePlan) || workPolicy(e.afterPlan) || workPolicy(e.beforeRemaining) ||
                   workPolicy(e.afterRemaining);
        }))
        return 24;
    if (hasFormulaFields(p) ||
        std::any_of(p.baselines.begin(), p.baselines.end(), [](const auto &b) { return hasFormulaFields(b.input); }) ||
        std::any_of(p.scenarios.begin(), p.scenarios.end(), [](const auto &s) { return hasFormulaFields(s.input); }))
        return 23;
    const auto dated = [](const auto &s) { return s && s->policy == "serial-dated-remaining-v1"; };
    if (!p.remainingWork.empty() || dated(p.planLeveling) || dated(p.remainingLeveling) ||
        std::any_of(
            p.scenarios.begin(), p.scenarios.end(),
            [&](const auto &s) { return !s.remainingWork.empty() || s.policy == "serial-dated-remaining-v1"; }) ||
        std::any_of(p.levelingDecisions.begin(), p.levelingDecisions.end(), [&](const auto &d) {
            return dated(d.beforePlan) || dated(d.afterPlan) || dated(d.beforeRemaining) || dated(d.afterRemaining);
        }))
        return 22;
    if (!p.savedResourceViews.empty())
        return 21;
    const auto advanced = [](const auto &s) { return s && advancedPriority(s->priority); };
    const auto resourceInput = [&](const PlanInput &v) { return hasResourcePlan(v) || advanced(v.planLeveling); };
    if (resourceInput(p) || advanced(p.remainingLeveling) ||
        std::any_of(p.baselines.begin(), p.baselines.end(), [&](const auto &b) { return resourceInput(b.input); }) ||
        std::any_of(p.scenarios.begin(), p.scenarios.end(),
                    [&](const auto &v) { return resourceInput(v.input) || advancedPriority(v.priority); }) ||
        std::any_of(p.levelingDecisions.begin(), p.levelingDecisions.end(), [&](const auto &v) {
            return advanced(v.beforePlan) || advanced(v.afterPlan) || advanced(v.beforeRemaining) ||
                   advanced(v.afterRemaining);
        }))
        return 20;
    const auto extended = [](const PlanInput &input) { return !input.activities.empty(); };
    const auto actual = [](const auto &selection) {
        return selection && selection->policy == actualDatesProgressPolicy;
    };
    if (extended(p) || !p.stepReports.empty() || actual(p.progressPolicy) ||
        std::any_of(p.baselines.begin(), p.baselines.end(), [&](const auto &b) { return extended(b.input); }) ||
        std::any_of(p.scenarios.begin(), p.scenarios.end(),
                    [&](const auto &v) { return extended(v.input) || actual(v.progressPolicy); }))
        return 19;
    if (p.printPreferences || !p.networkPositions.empty())
        return 18;
    const auto periodPolicy = [](const std::optional<LevelingSelection> &s) {
        return s && s->policy == "serial-effort-periods-v1";
    };
    const auto shaped = [&](const PlanInput &input) {
        return hasAssignmentPeriods(input) || periodPolicy(input.planLeveling);
    };
    if (shaped(p) || periodPolicy(p.remainingLeveling) ||
        std::any_of(p.levelingDecisions.begin(), p.levelingDecisions.end(),
                    [&](const auto &e) {
                        return periodPolicy(e.beforePlan) || periodPolicy(e.afterPlan) ||
                               periodPolicy(e.beforeRemaining) || periodPolicy(e.afterRemaining);
                    }) ||
        std::any_of(p.baselines.begin(), p.baselines.end(), [&](const auto &b) { return shaped(b.input); }) ||
        std::any_of(p.scenarios.begin(), p.scenarios.end(),
                    [&](const auto &s) { return shaped(s.input) || s.policy == "serial-effort-periods-v1"; }))
        return 17;
    if (hasObjectFields(p))
        return 16;
    for (const auto &b : p.baselines)
        if (hasObjectFields(b.input))
            return 16;
    for (const auto &s : p.scenarios)
        if (hasObjectFields(s.input))
            return 16;
    if (!p.levelingDecisions.empty())
        return 15;
    if (p.progressPolicy && (p.planLeveling || p.remainingLeveling))
        return 14;
    for (const auto &s : p.scenarios)
        if (s.progressPolicy)
            return 14;
    if (p.progressPolicy)
        return 13;
    if (p.planLeveling || p.remainingLeveling)
        return 12;
    for (const auto &b : p.baselines)
        if (b.input.planLeveling)
            return 12;
    for (const auto &s : p.scenarios)
        if (s.input.planLeveling)
            return 12;
    if (!p.periodActuals.empty())
        return 11;
    if (hasClassification(p) || !p.savedTaskViews.empty())
        return 10;
    for (const auto &b : p.baselines)
        if (hasClassification(b.input))
            return 10;
    for (const auto &s : p.scenarios)
        if (hasClassification(s.input))
            return 10;
    return 9;
}
int nativeSchemaFor(const Project &p) {
    return nativeSchemaForImpl(p, false);
}
int nativeReadSchemaFor(const Project &p) {
    return nativeSchemaForImpl(p, true);
}
} // namespace ppm
