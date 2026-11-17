#pragma once
#include "calculation/Reports.h"
#include <array>
#include <map>
namespace jobs {
enum class UsagePeriod { Day, Week, Month };
enum class UsageHierarchy { Tasks, Resources };
struct UsageOptions {
    std::string from, to, baselineId, wbsId;
    UsagePeriod period = UsagePeriod::Day;
};
struct UsageBucket {
    std::string from, to;
};
struct UsageValue {
    std::optional<std::int64_t> labor = 0, equipment = 0, cost = 0;
    std::array<bool, 3> reportOverflow{};
};
struct UsageRow {
    std::string taskId, resourceId, name;
    std::optional<std::size_t> parent;
    bool leaf = false, fixed = false, baselineOnly = false;
    std::array<std::vector<UsageValue>, 3> values; // Plan, frozen baseline, remaining.
    std::vector<UsageValue> reportedActual;
    std::vector<std::pair<std::size_t, std::size_t>> actualCounts; // Work reports, cost reports.
    Money cumulativeActual, unallocatedActual;
};
struct UsageResult {
    std::string documentId, name, currency, baselineDescription, levelingDescription, progressPolicyDescription;
    std::uint64_t revision = 0;
    std::optional<std::string> statusAt;
    UsageOptions options;
    std::vector<UsageBucket> buckets;
    std::vector<UsageRow> leaves;
    std::vector<std::string> taskOrder, resourceOrder;
    std::map<std::string, std::string> taskNames, resourceNames;
    std::map<std::string, Money> actuals;
    std::map<std::string, Money> unallocatedActuals;
    std::vector<ReportTable> actualTables;
    std::array<bool, 3> available{};
    std::vector<Diagnostic> diagnostics;
    std::size_t unassignedTasks = 0;
};
UsageResult buildUsage(const Project &, const UsageOptions &, const std::shared_ptr<CalculationControl> &control = {});
std::vector<UsageRow> usageRows(const UsageResult &, UsageHierarchy);
UsageValue usageTotal(const std::vector<UsageValue> &);
UsageValue usageReportedTotal(const std::vector<UsageValue> &);
ProjectReport usageReport(const UsageResult &, UsageHierarchy, const std::string &reportId,
                          const std::string &generatedAt, bool unsaved);
} // namespace jobs
