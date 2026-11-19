#pragma once
#include "domain/Project.h"
#include <unordered_map>

namespace ppm {
inline constexpr std::int64_t fieldNumberScale = 1000000;
std::int64_t parseFieldNumber(const std::string &);
std::string formatFieldNumber(std::int64_t);
struct FieldDuration {
    std::int64_t minutes = 0; // fixed point, fieldNumberScale per minute
    bool elapsed = false, estimated = false;
};
FieldDuration parseFieldDuration(const std::string &);
std::string formatFieldDuration(const FieldDuration &);
std::int64_t parseFieldCost(const std::string &, const std::string &currency);
std::string formatFieldCost(std::int64_t, const std::string &currency);
bool dateTimeCustomField(CustomFieldType);
std::string normalizeFieldDateTime(const std::string &);
std::int64_t packFieldDateTime(const std::string &);
std::string unpackFieldDateTime(std::int64_t);
bool hasDateTimeFields(const PlanInput &);
XmlLookup sharedXmlLookup(XmlLookup);
bool numericCustomField(CustomFieldType);
std::int64_t customFieldNumber(const PlanInput &, const CustomField &, const std::string &);
std::string fieldTypeToken(CustomFieldType);
CustomFieldType fieldTypeFromToken(const std::string &);
const std::vector<CustomField> &customFields(const PlanInput &, FieldScope);
std::vector<CustomField> &customFields(PlanInput &, FieldScope);
const CustomField *customField(const PlanInput &, const std::string &key, FieldScope = FieldScope::Task);
const ActivityCodeType *codeType(const PlanInput &, const std::string &key);
std::vector<std::string> attributeKeys(const PlanInput &, FieldScope = FieldScope::Task);
std::string attributeName(const PlanInput &, const std::string &key, FieldScope = FieldScope::Task);
std::string attributeUnit(const PlanInput &, const std::string &key, FieldScope = FieldScope::Task);
std::optional<std::string> attributeValue(const std::vector<TaskAttribute> &, const std::string &key);
std::optional<std::string> attributeValue(const Task &, const std::string &key);
std::string attributeText(const PlanInput &, const std::string &key, const std::string &value,
                          FieldScope = FieldScope::Task);
using AttributeLabels = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;
AttributeLabels attributeLabels(const PlanInput &, FieldScope = FieldScope::Task);
std::string attributeText(const AttributeLabels &, const std::string &key, const std::string &value);
std::string normalizeAttributeValue(const PlanInput &, const std::string &key, const std::string &value,
                                    FieldScope = FieldScope::Task, bool allowComputedLiteral = false);
void validateClassification(const PlanInput &);
void validateTaskView(const PlanInput &, const TaskViewSettings &);
void validateSavedTaskViews(const Project &);
bool classificationReferenced(const Project &, const std::string &key, FieldScope = FieldScope::Task);
bool hasClassification(const PlanInput &);
bool hasObjectFields(const PlanInput &);
bool hasExtendedXmlFields(const PlanInput &);
bool hasXmlLookups(const PlanInput &);
bool multipleChoice(const CustomField &);
std::vector<std::string> choiceSelection(const CustomField &, const std::string &);
std::string choiceValue(const CustomField &, const std::vector<std::string> &);
std::string normalizeLookupScalar(CustomFieldType, const std::string &, const std::string &currency = "KRW");
std::string assignmentAttributeId(const Assignment &);
int nativeSchemaFor(const Project &);
} // namespace ppm
