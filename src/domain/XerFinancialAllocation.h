#pragma once
#include "domain/Project.h"
#include "domain/PeriodActuals.h"
namespace jobs {
// Complete desired native allocation. Missing metrics remain unreported.
std::map<PeriodActualKey, PeriodActualEntry> allocateXerFinancials(const XerFinancialSnapshot &,
                                                                   const std::vector<XerFinancialAllocation> &);
std::vector<XerFinancialAllocation> initialXerFinancialAllocations(const XerFinancialSnapshot &);
} // namespace jobs
