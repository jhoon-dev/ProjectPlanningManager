#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace jobs {
struct FinancialPeriodValue {
    std::string taskId;
    std::optional<std::int64_t> budget, earned;
    bool operator==(const FinancialPeriodValue &) const = default;
};
struct FinancialPeriodDefinition {
    std::string id, name, baselineId, fromDate, toDate;
    std::vector<FinancialPeriodValue> values;
    bool operator==(const FinancialPeriodDefinition &) const = default;
};
struct FinancialPeriodBalance {
    std::optional<std::int64_t> bac, pv, ev, ac;
    bool estimateSupported = true;
    std::string acReportId, evReportId;
    bool operator==(const FinancialPeriodBalance &) const = default;
};
struct FinancialPeriodTask {
    std::string taskId;
    FinancialPeriodBalance opening, closing;
    std::optional<std::int64_t> dated;
    bool operator==(const FinancialPeriodTask &) const = default;
};
struct FinancialPeriodSubmission {
    std::string action = "define"; // define, close, reopen; every transition is append-only.
    FinancialPeriodDefinition period;
    std::string formula = "budget-remaining-v1", reporter, reason, recordedAt;
    bool confirmed = false;
    bool operator==(const FinancialPeriodSubmission &) const = default;
};
struct FinancialPeriodEvent : FinancialPeriodSubmission {
    std::string id, sourceDocumentId, currency;
    std::uint64_t recordedRevision = 0, knowledgeRevision = 0;
    std::optional<std::string> statusAt;
    std::vector<FinancialPeriodTask> tasks; // Only close events freeze native boundaries and dated AC.
    bool operator==(const FinancialPeriodEvent &) const = default;
};
} // namespace jobs
