#pragma once
#include "calculation/Reports.h"
#include <set>
namespace jobs {
void appendXmlProgressAudit(ProjectReport &, const Project &, const std::set<std::string> &);
} // namespace jobs
