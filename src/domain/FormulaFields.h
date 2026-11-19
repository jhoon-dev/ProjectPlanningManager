#pragma once
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <map>
#include <memory>
namespace ppm {
struct FormulaValue {
    std::optional<std::int64_t> number;
    std::string issue;
    bool missing = false;
    std::optional<std::string> scalar = {};
};
using FormulaValues = std::map<std::string, FormulaValue>;
bool hasFormulaFields(const PlanInput &);
void validateFormulaFields(const PlanInput &);
std::shared_ptr<const FormulaValues> formulaValues(const PlanInput &, FieldScope, const Task *task = nullptr,
                                                   const Resource *resource = nullptr,
                                                   const Assignment *assignment = nullptr);
std::optional<std::string> resolvedAttributeValue(const PlanInput &, const Task &, const std::string &);
std::optional<std::string> resolvedAttributeValue(const PlanInput &, const Resource &, const std::string &);
std::optional<std::string> resolvedProjectAttributeValue(const PlanInput &, const std::string &);
FormulaValue nativeFormulaValue(const PlanInput &, FieldScope, const std::string &, const Task *, const Resource *);
std::string formulaValueText(const FormulaValue &);
std::optional<std::string> formulaRawValue(const FormulaValue &);
std::vector<std::pair<FieldScope, std::string>> nativeFormulaReferences(const PlanInput &, FieldScope,
                                                                        const std::string &);
std::vector<std::pair<std::string, std::string>> formulaReferenceChoices(const PlanInput &, FieldScope);
} // namespace ppm
