#pragma once
#include "domain/Project.h"
#include <functional>
namespace ppm {
class XmlFormulaFrame {
public:
    XmlFormulaFrame();
    ~XmlFormulaFrame();
    XmlFormulaFrame(const XmlFormulaFrame &) = delete;
    XmlFormulaFrame &operator=(const XmlFormulaFrame &) = delete;
};
struct XmlFormulaResult {
    std::optional<std::string> value;
    std::string issue;
    bool missing = false;
};
struct XmlFormulaAlias {
    std::string name, key;
    FieldScope scope = FieldScope::Task;
    bool bound = false;
};
using XmlSummaryResolver = std::function<XmlFormulaResult(const std::string &)>;
bool hasXmlFormulas(const PlanInput &);
bool xmlAvailabilityFormula(const XmlFormula &);
bool xmlResourceLoadFormula(const XmlFormula &);
bool hasXmlResourceLoadFunctions(const PlanInput &, bool activeOnly = false);
bool hasXmlUnitReferences(const PlanInput &);
bool hasXmlAvailabilityFunctions(const PlanInput &, bool activeOnly = false);
bool xmlAdditionalFormula(const XmlFormula &);
bool hasXmlAdditionalFunctions(const PlanInput &, bool activeOnly = false);
bool xmlTimeZoneFormula(const XmlFormula &);
bool hasXmlTimeZoneFunctions(const PlanInput &, bool activeOnly = false);
bool xmlFormattingFormula(const XmlFormula &);
bool hasXmlFormattingFunctions(const PlanInput &, bool activeOnly = false);
bool xmlExtendedDateFormula(const XmlFormula &);
bool xmlNewDateSyntax(const XmlFormula &);
bool hasXmlExtendedDateFunctions(const PlanInput &, bool activeOnly = false);
bool xmlTimeFormula(const XmlFormula &);
bool hasXmlTimeFunctions(const PlanInput &, bool activeOnly = false);
bool xmlProjectConversionFormula(const XmlFormula &);
bool hasXmlProjectConversionFunctions(const PlanInput &, bool activeOnly = false);
bool xmlConversionFormula(const XmlFormula &);
bool hasXmlConversionFunctions(const PlanInput &, bool activeOnly = false);
bool xmlStringFormula(const XmlFormula &);
bool hasXmlStringFunctions(const PlanInput &, bool activeOnly = false);
bool xmlUnicodeFormula(const XmlFormula &);
bool hasXmlUnicodeFunctions(const PlanInput &, bool activeOnly = false);
bool xmlDateNameFormula(const XmlFormula &);
bool hasXmlDateNameFunctions(const PlanInput &, bool activeOnly = false);
bool xmlDateDisplayFormula(const XmlFormula &);
bool hasXmlDateDisplayFunctions(const PlanInput &, bool activeOnly = false);
bool xmlNumericDisplayFormula(const XmlFormula &);
bool hasXmlNumericDisplayFunctions(const PlanInput &, bool activeOnly = false);
bool xmlColorFormula(const XmlFormula &);
bool xmlPartitionFormula(const XmlFormula &);
bool xmlDepreciationFormula(const XmlFormula &);
bool hasXmlDepreciationFunctions(const PlanInput &, bool activeOnly = false);
bool hasXmlPartitionFunctions(const PlanInput &, bool activeOnly = false);
bool hasXmlColorFunctions(const PlanInput &, bool activeOnly = false);
bool xmlMathFormula(const XmlFormula &);
bool hasXmlMathFunctions(const PlanInput &, bool activeOnly = false);
bool xmlNumberTextFormula(const XmlFormula &);
bool hasXmlNumberTextFunctions(const PlanInput &, bool activeOnly = false);
XmlFormulaResult xmlScheduleReference(const PlanInput &, FieldScope, const std::string &, const Task * = nullptr,
                                      const Resource * = nullptr, const Assignment * = nullptr);
std::string xmlFormulaKey(const CustomField &);
std::string xmlFormulaInputKey(const PlanInput &, const Task *, const Resource *, const Assignment *);
XmlFormula bindXmlFormula(const PlanInput &, FieldScope, const std::string &,
                          const std::vector<XmlFormulaAlias> &aliases = {});
void validateXmlFormulas(const PlanInput &);
std::string xmlFormulaProblem(const PlanInput &, FieldScope, const std::string &);
XmlFormulaResult xmlFormulaValue(const PlanInput &, FieldScope, const std::string &, const Task * = nullptr,
                                 const Resource * = nullptr, const Assignment * = nullptr,
                                 const XmlSummaryResolver & = {});
std::string rewriteXmlFormula(const XmlFormula &, const std::function<std::string(const XmlFormulaReference &)> &);
} // namespace ppm
