#pragma once
#include "calculation/Reports.h"
#include <set>
namespace ppm {
std::vector<ReportTable> periodActualTables(const Project &, const std::set<std::string> &taskIds,
                                            const std::optional<std::string> &from = {},
                                            const std::optional<std::string> &to = {});
}
