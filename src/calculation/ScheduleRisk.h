#pragma once
#include "calculation/Scheduler.h"
#include "domain/Governance.h"
#include <map>
namespace jobs {
struct RiskOptions {
    std::uint64_t seed = 1;
    int iterations = 1000;
    std::optional<std::string> deadline;
};
struct RiskResult {
    std::string documentId, model = "triangular-rank-group-splitmix64-v1", baselineFinish;
    std::uint64_t revision = 0, seed = 0;
    int iterations = 0;
    CalculationStatus status = CalculationStatus::Incomplete;
    std::map<int, std::string> finishQuantiles;
    std::map<std::string, int> criticalCounts;
    int deadlineHits = 0;
    // Difference between first/second-half nearest-rank medians in project working minutes.
    std::int64_t medianDriftMinutes = 0;
};
RiskResult analyzeScheduleRisk(const Project &, const std::vector<ScheduleRisk> &, const RiskOptions &,
                               std::shared_ptr<CalculationControl> control = {});
struct PortfolioSummary {
    std::string portfolio, currency;
    std::size_t projects = 0, loaded = 0, validSchedules = 0, openRisks = 0;
    std::int64_t budgetMinor = 0;
    std::optional<std::string> finish;
};
std::vector<PortfolioSummary> summarizePortfolio(const Workspace &,
                                                 const std::vector<std::shared_ptr<const Project>> &);
} // namespace jobs
