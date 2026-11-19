#include "domain/XmlFormula.h"
#include "application/Document.h"
#include "domain/Classification.h"
#include "domain/XmlCodes.h"
#include "domain/XmlRollDown.h"
#include "domain/XmlRollUp.h"
#include <algorithm>
#include <unordered_set>

namespace ppm {
std::string Document::putCustomField(CustomField field, FieldScope scope) {
    requireEditable();
    auto p = *project_;
    if (field.id.empty())
        field.id = store_.newId();
    for (auto &option : field.options)
        if (option.id.empty())
            option.id = store_.newId();
    auto &fields = customFields(p, scope);
    auto found = std::find_if(fields.begin(), fields.end(), [&](const auto &f) { return f.id == field.id; });
    if (found == fields.end())
        fields.push_back(field);
    else {
        if (found->xmlLookup && (found->type != field.type || found->options != field.options ||
                                 found->xmlLookup != field.xmlLookup || found->xmlIdentity != field.xmlIdentity))
            throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"외부 목록 사전은 읽기 전용입니다."});
        const bool values =
            scope == FieldScope::Task
                ? (std::any_of(p.tasks.begin(), p.tasks.end(),
                               [&](const auto &t) { return attributeValue(t, "field:" + field.id).has_value(); }) ||
                   attributeValue(p.taskSummaryAttributes, "field:" + field.id).has_value() ||
                   std::any_of(
                       p.wbsNodes.begin(), p.wbsNodes.end(),
                       [&](const auto &g) { return attributeValue(g.attributes, "field:" + field.id).has_value(); }))
                : classificationReferenced(p, "field:" + field.id, scope);
        if (found->type != field.type && values)
            throw AppError(ErrorCode::CLASSIFICATION_IN_USE, {field.name + " · 값이 있는 필드의 형식 변경"});
        *found = field;
    }
    edit(std::move(p), "사용자 정의 필드 등록·변경");
    return field.id;
}
void Document::setXmlLookupMasks(FieldScope scope, const std::string &key, std::vector<XmlCodeMask> masks,
                                 bool required) {
    requireEditable();
    auto p = *project_;
    ppm::setXmlLookupMasks(p, scope, key, masks, required);
    edit(std::move(p), "XML 코드 마스크 변경");
}
std::string Document::createXmlLookupCode(FieldScope scope, const std::string &key, const std::string &parent,
                                          std::optional<std::string> segment, const std::string &description) {
    requireEditable();
    auto p = *project_;
    auto id = appendXmlLookupCode(p, scope, key, parent, segment, description, store_.newId());
    edit(std::move(p), "XML 목록 코드 생성");
    return id;
}
void Document::setXmlWbsCodes(XmlWbsCodes codes, bool fillMissing) {
    requireEditable();
    auto p = *project_;
    p.xmlWbs = std::move(codes);
    validateXmlWbsCodes(p);
    synchronizeXmlWbsCodes(p, nullptr, fillMissing);
    edit(std::move(p), "XML WBS 코드 마스크·생성");
}
void Document::setXmlFormula(FieldScope scope, const std::string &key, std::optional<XmlFormula> value) {
    requireEditable();
    auto p = *project_;
    auto &fields = customFields(p, scope);
    auto f = std::find_if(fields.begin(), fields.end(), [&](const auto &v) { return "field:" + v.id == key; });
    if (f == fields.end())
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {key});
    f->xmlFormula = std::move(value);
    edit(std::move(p), "XML 외부 계산식 설정");
}
void Document::setXmlRollUp(FieldScope scope, const std::string &key, std::optional<XmlRollUp> rule) {
    requireEditable();
    auto p = *project_;
    if (scope != FieldScope::Task && scope != FieldScope::Resource)
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"작업·자원 요약 필드를 선택하세요."});
    auto &fields = customFields(p, scope);
    auto f = std::find_if(fields.begin(), fields.end(), [&](const auto &v) { return "field:" + v.id == key; });
    if (f == fields.end())
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {key});
    f->xmlRollUp = rule;
    edit(std::move(p), "XML 요약 roll-up 설정");
}
void Document::setSummaryAttribute(const std::string &wbsId, const std::string &key, std::optional<std::string> value) {
    requireEditable();
    auto p = *project_;
    const auto *f = customField(p, key);
    if (!f)
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {key});
    if (f->xmlRollUp && f->xmlRollUp->calculationType != 0)
        throw AppError(ErrorCode::CLASSIFICATION_INVALID,
                       {"자동 요약/외부 계산식의 명시 값은 직접 편집하지 않습니다."});
    auto *attrs = &p.taskSummaryAttributes;
    if (!wbsId.empty()) {
        auto g = std::find_if(p.wbsNodes.begin(), p.wbsNodes.end(), [&](const auto &n) { return n.id == wbsId; });
        if (g == p.wbsNodes.end())
            throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {wbsId});
        attrs = &g->attributes;
    }
    if (value)
        value = normalizeAttributeValue(p, key, *value, FieldScope::Task, true);
    std::erase_if(*attrs, [&](const auto &v) { return v.key == key; });
    if (value)
        attrs->push_back({key, *value});
    edit(std::move(p), "명시 요약 필드 값 변경");
}
void Document::setXmlRollDown(FieldScope scope, const std::string &key, std::optional<XmlRollDown> rule) {
    requireEditable();
    auto p = *project_;
    if (scope != FieldScope::Task && scope != FieldScope::Resource)
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"Roll-down 원본은 작업·자원 필드입니다."});
    auto &fields = customFields(p, scope);
    auto found = std::find_if(fields.begin(), fields.end(), [&](const auto &f) { return "field:" + f.id == key; });
    if (found == fields.end())
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {key});
    found->xmlRollDown = std::move(rule);
    extendRollDownLookups(p);
    edit(std::move(p), "XML 배정 roll-down 설정");
}
std::string Document::createRollDownField(FieldScope scope, const std::string &key, const std::string &name,
                                          bool automatic) {
    requireEditable();
    auto p = *project_;
    if (scope != FieldScope::Task && scope != FieldScope::Resource)
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"Roll-down 원본은 작업·자원 필드입니다."});
    auto &fields = customFields(p, scope);
    auto found = std::find_if(fields.begin(), fields.end(), [&](const auto &f) { return "field:" + f.id == key; });
    if (found == fields.end())
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {key});
    auto target = *found;
    target.id = store_.newId();
    target.name = name;
    target.xmlIdentity.reset();
    target.xmlRollDown.reset();
    target.xmlFormula.reset();
    target.xmlRollUp.reset();
    if (target.xmlLookup) {
        target.xmlLookup->fieldGuid.clear();
        target.xmlLookup->fieldName.clear();
    }
    p.assignmentFields.push_back(target);
    found->xmlRollDown = XmlRollDown{target.id, automatic};
    edit(std::move(p), "배정 필드 생성·XML roll-down 설정");
    return target.id;
}
void Document::removeCustomField(const std::string &id, FieldScope scope) {
    requireEditable();
    auto p = *project_;
    if (classificationReferenced(p, "field:" + id, scope))
        throw AppError(ErrorCode::CLASSIFICATION_IN_USE, {id});
    if (!std::erase_if(customFields(p, scope), [&](const auto &f) { return f.id == id; }))
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {id});
    edit(std::move(p), "사용자 정의 필드 삭제");
}
std::string Document::putActivityCodeType(ActivityCodeType type) {
    requireEditable();
    auto p = *project_;
    if (type.id.empty())
        type.id = store_.newId();
    for (auto &value : type.values)
        if (value.id.empty())
            value.id = store_.newId();
    auto found = std::find_if(p.codeTypes.begin(), p.codeTypes.end(), [&](const auto &t) { return t.id == type.id; });
    if (found == p.codeTypes.end())
        p.codeTypes.push_back(type);
    else
        *found = type;
    edit(std::move(p), "작업 코드 사전 등록·변경");
    return type.id;
}
void Document::removeActivityCodeType(const std::string &id) {
    requireEditable();
    auto p = *project_;
    if (classificationReferenced(p, "code:" + id))
        throw AppError(ErrorCode::CLASSIFICATION_IN_USE, {id});
    if (!std::erase_if(p.codeTypes, [&](const auto &t) { return t.id == id; }))
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {id});
    edit(std::move(p), "작업 코드 종류 삭제");
}
void Document::setTaskAttribute(const std::vector<std::string> &taskIds, const std::string &key,
                                std::optional<std::string> value) {
    requireEditable();
    auto p = *project_;
    if (const auto *f = customField(p, key); f && calculatedField(*f))
        throw AppError(ErrorCode::FORMULA_INVALID, {"계산식 필드는 직접 수정하지 않습니다."});
    if (taskIds.empty() || key == "wbs" || (!customField(p, key) && !codeType(p, key)))
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {key});
    if (value)
        value = normalizeAttributeValue(p, key, *value);
    std::unordered_set<std::string> requested(taskIds.begin(), taskIds.end());
    if (requested.size() != taskIds.size())
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 작업 ID"});
    for (auto &task : p.tasks) {
        calculationCheckpoint();
        if (!requested.erase(task.id))
            continue;
        const auto existing =
            std::find_if(task.attributes.begin(), task.attributes.end(), [&](const auto &a) { return a.key == key; });
        if (!value)
            std::erase_if(task.attributes, [&](const auto &a) { return a.key == key; });
        else if (existing == task.attributes.end())
            task.attributes.push_back({key, *value});
        else
            existing->value = *value;
    }
    if (!requested.empty())
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {*requested.begin()});
    edit(std::move(p), "작업 분류 값 변경");
}
namespace {
void setValue(std::vector<TaskAttribute> &attributes, const std::string &key, const std::optional<std::string> &value) {
    const auto found = std::find_if(attributes.begin(), attributes.end(), [&](const auto &a) { return a.key == key; });
    if (!value)
        std::erase_if(attributes, [&](const auto &a) { return a.key == key; });
    else if (found == attributes.end())
        attributes.push_back({key, *value});
    else
        found->value = *value;
}
} // namespace
void Document::setProjectAttribute(const std::string &key, std::optional<std::string> value) {
    requireEditable();
    auto p = *project_;
    if (const auto *f = customField(p, key, FieldScope::Project); f && calculatedField(*f))
        throw AppError(ErrorCode::FORMULA_INVALID, {"계산식 필드는 직접 수정하지 않습니다."});
    if (!customField(p, key, FieldScope::Project))
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {key});
    if (value)
        value = normalizeAttributeValue(p, key, *value, FieldScope::Project);
    setValue(p.projectAttributes, key, value);
    edit(std::move(p), "프로젝트 사용자 필드 값 변경");
}
void Document::setResourceAttribute(const std::vector<std::string> &ids, const std::string &key,
                                    std::optional<std::string> value) {
    requireEditable();
    auto p = *project_;
    if (const auto *f = customField(p, key, FieldScope::Resource); f && calculatedField(*f))
        throw AppError(ErrorCode::FORMULA_INVALID, {"계산식 필드는 직접 수정하지 않습니다."});
    if (ids.empty() || !customField(p, key, FieldScope::Resource))
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {key});
    if (value)
        value = normalizeAttributeValue(p, key, *value, FieldScope::Resource);
    std::unordered_set<std::string> requested(ids.begin(), ids.end());
    if (requested.size() != ids.size())
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 자원 ID"});
    for (auto &resource : p.resources) {
        calculationCheckpoint();
        if (requested.erase(resource.id))
            setValue(resource.attributes, key, value);
    }
    if (!requested.empty())
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {*requested.begin()});
    edit(std::move(p), "자원 사용자 필드 값 변경");
}
void Document::setAssignmentAttribute(const std::vector<std::string> &ids, const std::string &key,
                                      std::optional<std::string> value) {
    requireEditable();
    auto p = *project_;
    if (const auto *f = customField(p, key, FieldScope::Assignment); f && calculatedField(*f))
        throw AppError(ErrorCode::FORMULA_INVALID, {"계산식 필드는 직접 수정하지 않습니다."});
    if (ids.empty() || !customField(p, key, FieldScope::Assignment))
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {key});
    if (value)
        value = normalizeAttributeValue(p, key, *value, FieldScope::Assignment);
    std::unordered_set<std::string> requested(ids.begin(), ids.end());
    if (requested.size() != ids.size())
        throw AppError(ErrorCode::CLASSIFICATION_INVALID, {"중복 배정 ID"});
    for (auto &assignment : p.assignments) {
        calculationCheckpoint();
        if (requested.erase(assignmentAttributeId(assignment))) {
            setValue(assignment.attributes, key, value);
            std::erase(assignment.rollDownMissing, key);
        }
    }
    if (!requested.empty())
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {*requested.begin()});
    edit(std::move(p), "배정 사용자 필드 값 변경");
}
std::string Document::putSavedTaskView(SavedTaskView view) {
    requireEditable();
    auto p = *project_;
    if (view.id.empty())
        view.id = store_.newId();
    auto found =
        std::find_if(p.savedTaskViews.begin(), p.savedTaskViews.end(), [&](const auto &v) { return v.id == view.id; });
    if (found == p.savedTaskViews.end())
        p.savedTaskViews.push_back(view);
    else
        *found = view;
    edit(std::move(p), "작업 보기 저장", true);
    return view.id;
}
void Document::removeSavedTaskView(const std::string &id) {
    requireEditable();
    auto p = *project_;
    if (!std::erase_if(p.savedTaskViews, [&](const auto &v) { return v.id == id; }))
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {id});
    edit(std::move(p), "저장된 작업 보기 삭제", true);
}
std::string Document::putSavedResourceView(SavedResourceView view) {
    requireEditable();
    auto p = *project_;
    if (view.id.empty())
        view.id = store_.newId();
    auto it = std::find_if(p.savedResourceViews.begin(), p.savedResourceViews.end(),
                           [&](const auto &v) { return v.id == view.id; });
    if (it == p.savedResourceViews.end())
        p.savedResourceViews.push_back(view);
    else
        *it = view;
    edit(std::move(p), "자원 보기 저장", true);
    return view.id;
}
void Document::removeSavedResourceView(const std::string &id) {
    requireEditable();
    auto p = *project_;
    if (!std::erase_if(p.savedResourceViews, [&](const auto &v) { return v.id == id; }))
        throw AppError(ErrorCode::CLASSIFICATION_NOT_FOUND, {id});
    edit(std::move(p), "자원 보기 삭제", true);
}
} // namespace ppm
