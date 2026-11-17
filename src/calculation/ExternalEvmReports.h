#pragma once
#include "calculation/Reports.h"
namespace jobs {
void appendExternalEvmTables(ProjectReport &, const Project &,
                             const std::optional<std::vector<std::string>> &scope = {});
} // namespace jobs
