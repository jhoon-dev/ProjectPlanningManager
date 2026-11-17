#pragma once
#include "domain/Project.h"
namespace jobs {
bool xmlUnitReference(const std::string &);
std::optional<XmlFormulaReference> bindXmlUnitReference(FieldScope, const std::string &token);
bool validXmlUnitReference(const XmlFormulaReference &);
std::optional<int> resourceUnitsOn(const Resource &, const std::string &date);
} // namespace jobs
