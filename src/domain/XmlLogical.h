#pragma once
#include "domain/Project.h"
namespace jobs {
bool xmlLogicalFormula(const XmlFormula &);
bool hasXmlLogicalOperators(const PlanInput &, bool activeOnly = false);
std::int32_t xmlLogicalInteger(std::int64_t scaled);
// Non-Null numeric operands/results use fixed-six; Boolean/Null typing is handled by the evaluator.
std::int64_t xmlLogicalNumber(const std::string &op, std::int64_t left, std::int64_t right);
} // namespace jobs
