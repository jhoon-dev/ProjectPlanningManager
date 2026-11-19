#pragma once
#include "calculation/Reports.h"
#include <set>
namespace ppm {
std::vector<ReportTable> formulaFieldTables(const Project &, const std::set<std::string> &taskScope,
                                            const std::optional<std::set<std::string>> &resourceScope = std::nullopt,
                                            bool includeProject = true);
}
