#pragma once
#include "calculation/Reports.h"
#include "domain/ResourceView.h"
namespace ppm {
ProjectReport resourceViewReport(const Project &, const ResourceViewSettings &, const ReportOptions &);
}
