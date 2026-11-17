#include "domain/XmlUnitFields.h"
#include "domain/CalculationControl.h"
#include "domain/ResourceAvailability.h"
#include <algorithm>
namespace jobs {
bool xmlUnitReference(const std::string &key) {
    return key == "builtin:units" || key == "builtin:maxunits";
}
std::optional<XmlFormulaReference> bindXmlUnitReference(FieldScope scope, const std::string &token) {
    std::string name;
    for (char c : token)
        if (c != ' ')
            name += c >= 'A' && c <= 'Z' ? char(c + 32) : c;
    auto target = scope;
    bool qualified = false;
    for (const auto &[prefix, value] : {std::pair{"project.", FieldScope::Project},
                                        {"task.", FieldScope::Task},
                                        {"resource.", FieldScope::Resource},
                                        {"assignment.", FieldScope::Assignment}})
        if (name.starts_with(prefix)) {
            target = value;
            name.erase(0, std::char_traits<char>::length(prefix));
            qualified = true;
            break;
        }
    const auto key = name == "units" || name == "assignmentunits" ? "builtin:units"
                     : name == "maxunits"                         ? "builtin:maxunits"
                                                                  : "";
    if (std::string(key).empty())
        return {};
    if (name == "maxunits" && !qualified && scope == FieldScope::Assignment)
        target = FieldScope::Resource;
    return XmlFormulaReference{token, key, target};
}
bool validXmlUnitReference(const XmlFormulaReference &r) {
    return r.key == "builtin:units" || (r.key == "builtin:maxunits" && r.scope == FieldScope::Resource);
}
std::optional<int> resourceUnitsOn(const Resource &r, const std::string &date) {
    if (!validDate(date))
        throw AppError(ErrorCode::FORMULA_INVALID, {"가용률 날짜를 확인하세요."});
    auto units = resourceUnitsAt(r, date + " 00:00");
    if (units && (*units < 0 || *units > 100000))
        throw AppError(ErrorCode::RESOURCE_INPUT_INVALID);
    return units;
}
} // namespace jobs
