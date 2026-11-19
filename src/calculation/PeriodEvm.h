#pragma once
#include "calculation/Reports.h"
namespace ppm {
struct EvmBoundary {
    std::string stamp;
    bool inclusive = false, observed = true;
};
struct PeriodEvmPoint {
    Evm value;
    std::string acReportId, evReportId;
};
struct PeriodEvmCurve {
    std::string taskId, name, wbsId, credit;
    std::vector<PeriodEvmPoint> points;
};
Evm nativeEvmIndicators(Evm, const std::string &formula);
// Shared native budget/credit policy. Budgets are built once, source reports indexed by task/status.
std::vector<PeriodEvmCurve> nativeEvmCurves(const Project &, const std::string &baseline, const std::string &formula,
                                            const std::vector<EvmBoundary> &,
                                            const std::optional<std::vector<std::string>> &scope = {});
ProjectReport buildPeriodEvmReport(const Project &, const ReportOptions &, const std::string &bucket = "month");
} // namespace ppm
