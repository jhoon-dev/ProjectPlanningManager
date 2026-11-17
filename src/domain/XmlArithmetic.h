#pragma once
#include "domain/Project.h"
namespace jobs {
bool xmlArithmeticFormula(const XmlFormula &);
bool hasXmlArithmeticOperators(const PlanInput &, bool activeOnly = false);
// Numeric operands and results are fixed-six. No duration tag is inferred.
std::int64_t xmlArithmeticNumber(const std::string &op, std::int64_t left, std::int64_t right);
} // namespace jobs
