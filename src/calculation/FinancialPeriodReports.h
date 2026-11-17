#pragma once
#include "calculation/Reports.h"
namespace jobs {
std::vector<FinancialPeriodTask> financialPeriodSnapshot(const Project &, const FinancialPeriodDefinition &,
                                                         const std::string &formula);
ProjectReport buildFinancialPeriodReport(const Project &, ReportOptions, const std::string &periodId,
                                         const std::string &closeEventId = {});
void appendFinancialPeriodHistory(ProjectReport &, const Project &);
} // namespace jobs
