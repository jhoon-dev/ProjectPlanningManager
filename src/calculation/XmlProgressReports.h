#pragma once
#include "calculation/Reports.h"
#include <set>
namespace ppm {
void appendXmlProgressAudit(ProjectReport &, const Project &, const std::set<std::string> &);
} // namespace ppm
