#pragma once
#include "calculation/Reports.h"
namespace ppm {
void appendExternalEvmTables(ProjectReport &, const Project &,
                             const std::optional<std::vector<std::string>> &scope = {});
} // namespace ppm
