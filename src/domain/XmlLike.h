#pragma once
#include "domain/Project.h"
namespace ppm {
bool xmlLikeFormula(const XmlFormula &);
bool hasXmlLikeOperator(const PlanInput &, bool activeOnly = false);
// Case-sensitive BMP code-point matching. Validates the entire pattern before matching.
bool xmlLikeMatch(const std::string &text, const std::string &pattern);
} // namespace ppm
