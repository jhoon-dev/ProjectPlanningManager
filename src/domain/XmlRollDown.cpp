#include "domain/FormulaFields.h"
#include "domain/XmlRollDown.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <set>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::CLASSIFICATION_INVALID, {s});
}
std::string signature(const CustomField &f, const std::string &id) {
    if (!f.xmlLookup) {
        auto i = std::find_if(f.options.begin(), f.options.end(), [&](const auto &o) { return o.id == id; });
        if (i == f.options.end())
            invalid("Roll-down 목록 옵션이 없습니다.");
        return i->name;
    }
    const auto &l = *f.xmlLookup;
    std::string current = id;
    std::vector<std::string> parts;
    std::set<std::string> seen;
    while (!current.empty()) {
        if (!seen.insert(current).second || seen.size() > 16)
            invalid("Roll-down 목록 부모 순환·깊이 초과");
        const auto i =
            std::find_if(l.values.begin(), l.values.end(), [&](const auto &v) { return v.optionId == current; });
        if (i == l.values.end())
            invalid("Roll-down 목록 값이 없습니다.");
        parts.push_back(i->value);
        current = i->parentId;
    }
    std::string result;
    for (auto i = parts.rbegin(); i != parts.rend(); ++i)
        result += std::to_string(i->size()) + ":" + *i;
    return result;
}
std::unordered_map<std::string, std::string> choiceMap(const CustomField &source, const CustomField &target) {
    if (source.type != target.type || source.unit != target.unit || !source.expression.empty() ||
        calculatedField(target))
        invalid("Roll-down의 원본·배정 형식/단위가 다르거나 계산식 필드입니다.");
    std::unordered_map<std::string, std::string> result;
    if (source.type != CustomFieldType::Choice)
        return result;
    if (bool(source.xmlLookup) != bool(target.xmlLookup) ||
        (source.xmlLookup && source.xmlLookup->valueType != target.xmlLookup->valueType) ||
        multipleChoice(source) != multipleChoice(target))
        invalid("Roll-down 목록의 값 형식·단일/다중 선택이 다릅니다.");
    if (source.xmlLookup && source.xmlLookup->maxValues != target.xmlLookup->maxValues)
        invalid("Roll-down 목록의 선택 한도가 다릅니다.");
    std::unordered_map<std::string, std::vector<std::string>> byValue;
    for (const auto &o : target.options)
        byValue[signature(target, o.id)].push_back(o.id);
    for (const auto &o : source.options) {
        calculationCheckpoint();
        std::string selected;
        if (source.xmlLookup && source.xmlLookup->tableGuid == target.xmlLookup->tableGuid &&
            !source.xmlLookup->tableGuid.empty()) {
            const auto &values = source.xmlLookup->values;
            const auto item =
                std::find_if(values.begin(), values.end(), [&](const auto &v) { return v.optionId == o.id; });
            if (item != values.end() && !item->guid.empty())
                for (const auto &v : target.xmlLookup->values)
                    if (v.guid == item->guid)
                        selected = v.optionId;
        }
        if (selected.empty()) {
            auto i = byValue.find(signature(source, o.id));
            if (i == byValue.end() || i->second.size() != 1)
                invalid("Roll-down 배정 목록에 대응 값이 없거나 중복됩니다.");
            selected = i->second.front();
        }
        result.emplace(o.id, selected);
    }
    return result;
}
} // namespace
bool rollDownCompatible(const CustomField &s, const CustomField &t) {
    try {
        (void)choiceMap(s, t);
        return true;
    } catch (const AppError &) {
        return false;
    }
}
bool hasXmlRollDown(const PlanInput &p) {
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, s))
            if (f.xmlRollDown)
                return true;
    return std::any_of(p.assignments.begin(), p.assignments.end(),
                       [](const auto &a) { return !a.rollDownMissing.empty(); });
}
RollDownValues::RollDownValues(const PlanInput &p) : p_(p) {

    std::set<std::string> targets;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlRollDown) {
                calculationCheckpoint();
                const auto &r = *f.xmlRollDown;
                if (r.assignmentFieldId.empty()) {
                    if (r.automatic)
                        invalid("자동 roll-down에는 배정 필드가 필요합니다.");
                    continue;
                }
                if (scope != FieldScope::Task && scope != FieldScope::Resource)
                    invalid("Roll-down 원본은 작업·자원 필드여야 합니다.");
                const auto *target = customField(p, "field:" + r.assignmentFieldId, FieldScope::Assignment);
                if (!target || !targets.insert(r.assignmentFieldId).second)
                    invalid("Roll-down 대상 배정 필드가 없거나 여러 원본이 충돌합니다.");
                auto choices = choiceMap(f, *target);
                if (r.automatic)
                    bindings_.emplace("field:" + target->id, Binding{scope, &f, target, std::move(choices)});
            }
    if (!bindings_.empty()) {
        for (const auto &t : p.tasks)
            tasks_.emplace(t.id, &t);
        for (const auto &r : p.resources)
            resources_.emplace(r.id, &r);
    }
}
const CustomField *RollDownValues::source(const std::string &key) const {
    auto i = bindings_.find(key);
    return i == bindings_.end() ? nullptr : i->second.source;
}
std::optional<std::string> RollDownValues::value(const Assignment &a, const std::string &key) const {
    if (const auto *f = customField(p_, key, FieldScope::Assignment); f && calculatedField(*f))
        return formulaRawValue(formulaValues(p_, FieldScope::Assignment, nullptr, nullptr, &a)->at(key));
    if (auto manual = attributeValue(a.attributes, key))
        return manual;
    if (std::find(a.rollDownMissing.begin(), a.rollDownMissing.end(), key) != a.rollDownMissing.end())
        return {};
    const auto found = bindings_.find(key);
    if (found == bindings_.end())
        return {};
    const auto &b = found->second;
    if (b.scope == FieldScope::Task) {
        auto i = tasks_.find(a.taskId);
        if (i == tasks_.end())
            invalid("Roll-down 원본 작업이 없습니다.");
    } else {
        auto i = resources_.find(a.resourceId);
        if (i == resources_.end())
            invalid("Roll-down 원본 자원이 없습니다.");
    }
    auto raw = b.scope == FieldScope::Task
                   ? resolvedAttributeValue(p_, *tasks_.at(a.taskId), "field:" + b.source->id)
                   : resolvedAttributeValue(p_, *resources_.at(a.resourceId), "field:" + b.source->id);
    if (!raw)
        return {};
    if (b.source->type == CustomFieldType::Choice) {
        auto ids = choiceSelection(*b.source, *raw);
        for (auto &id : ids) {
            auto i = b.choices.find(id);
            if (i == b.choices.end())
                invalid("Roll-down 목록 값 대응 누락");
            id = i->second;
        }
        raw = choiceValue(*b.target, ids);
    }
    return normalizeAttributeValue(p_, key, *raw, FieldScope::Assignment);
}
std::vector<TaskAttribute> RollDownValues::attributes(const Assignment &a) const {
    auto result = a.attributes;
    for (const auto &f : p_.assignmentFields)
        if (calculatedField(f)) {
            const auto key = "field:" + f.id;
            std::erase_if(result, [&](const auto &v) { return v.key == key; });
            if (auto v = value(a, key))
                result.push_back({key, *v});
        }

    for (const auto &[key, b] : bindings_) {
        (void)b;
        calculationCheckpoint();
        if (!attributeValue(result, key))
            if (auto v = value(a, key))
                result.push_back({key, *v});
    }
    return result;
}
std::string RollDownValues::origin(const Assignment &a, const std::string &key) const {
    if (const auto *f = customField(p_, key, FieldScope::Assignment); f && calculatedField(*f))
        return "XML 계산식";

    if (attributeValue(a.attributes, key))
        return "직접 입력";
    if (std::find(a.rollDownMissing.begin(), a.rollDownMissing.end(), key) != a.rollDownMissing.end())
        return "명시 미설정";
    auto i = bindings_.find(key);
    return i == bindings_.end() ? "미설정"
                                : (i->second.scope == FieldScope::Task ? "작업 roll-down · " : "자원 roll-down · ") +
                                      i->second.source->name;
}
void validateXmlRollDown(const PlanInput &p) {
    if (!hasXmlRollDown(p))
        return;
    RollDownValues resolved(p);
    std::size_t cells = 0;
    for (const auto &a : p.assignments) {
        if (a.rollDownMissing.size() > 64)
            invalid("배정 명시 미설정 한도 초과");
        std::set<std::string> seen;
        for (const auto &key : a.rollDownMissing)
            if (!customField(p, key, FieldScope::Assignment) || !seen.insert(key).second ||
                attributeValue(a.attributes, key))
                invalid("배정 명시 미설정의 필드·중복·직접 값 충돌");
        for (const auto &f : p.assignmentFields)
            if (resolved.source("field:" + f.id)) {
                calculationCheckpoint();
                if (++cells > 100000)
                    invalid("Roll-down 배정 조회 셀 한도100,000개 초과");
                (void)resolved.value(a, "field:" + f.id);
            }
    }
}
void extendRollDownLookups(PlanInput &p) {
    for (auto scope : {FieldScope::Task, FieldScope::Resource})
        for (const auto &f : customFields(p, scope))
            if (f.xmlRollDown && !f.xmlRollDown->assignmentFieldId.empty() && f.xmlLookup) {
                auto &fields = p.assignmentFields;
                auto target = std::find_if(fields.begin(), fields.end(),
                                           [&](const auto &v) { return v.id == f.xmlRollDown->assignmentFieldId; });
                if (target == fields.end() || !target->xmlLookup || target->xmlLookup->restrictValues ||
                    target->xmlLookup->valueType != f.xmlLookup->valueType)
                    continue;
                auto &l = *target->xmlLookup;
                for (const auto &v : f.xmlLookup->values)
                    if (!v.listed && std::none_of(l.values.begin(), l.values.end(),
                                                  [&](const auto &t) { return !t.listed && t.value == v.value; })) {
                        if (l.values.size() >= 1000)
                            invalid("Roll-down 열린 목록 항목 한도 초과");
                        auto copy = v;
                        int n = 1;
                        do {
                            copy.optionId = "roll-down-" + std::to_string(n++);
                        } while (std::any_of(l.values.begin(), l.values.end(),
                                             [&](const auto &t) { return t.optionId == copy.optionId; }));
                        auto option = std::find_if(f.options.begin(), f.options.end(),
                                                   [&](const auto &o) { return o.id == v.optionId; });
                        if (option == f.options.end())
                            invalid("Roll-down 열린 목록 옵션 누락");
                        auto label = option->name;
                        if (std::any_of(target->options.begin(), target->options.end(),
                                        [&](const auto &o) { return o.name == label; }))
                            label += " [" + copy.optionId + "]";
                        l.values.push_back(copy);
                        target->options.push_back({copy.optionId, label});
                    }
            }
}
} // namespace jobs
