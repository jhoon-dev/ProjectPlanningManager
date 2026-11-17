#pragma once
#include "domain/Project.h"
#include <map>
namespace jobs {
std::map<std::string, const FinancialPeriodEvent *> financialPeriods(const Project &);
const FinancialPeriodEvent &financialPeriod(const Project &, const std::string &);
void validateFinancialPeriods(const Project &);
// Apply the active closes to newly appended facts, including backdated allocations.
void guardFinancialPeriodEdits(const Project &before, const Project &after);
} // namespace jobs
