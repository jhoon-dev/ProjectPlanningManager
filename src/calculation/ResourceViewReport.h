#pragma once
#include "calculation/Reports.h"
#include "domain/ResourceView.h"
namespace jobs {
ProjectReport resourceViewReport(const Project &, const ResourceViewSettings &, const ReportOptions &);
}
