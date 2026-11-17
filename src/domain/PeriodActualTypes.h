#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace jobs {
struct PeriodActualEntry {
    std::string id, taskId, resourceId, resourceType, date;
    std::string statusAt, recordedAt, reporter, reason, previousEntryId, sourceDocumentId, currency, costEntryId;
    std::uint64_t recordedRevision = 0;
    std::optional<std::int64_t> workUnitMinutes, actualMinor;
    bool operator==(const PeriodActualEntry &) const = default;
};
struct PeriodTaskTotal {
    std::string taskId;
    std::optional<std::int64_t> actualMinor;
};
struct PeriodActualSubmission {
    std::vector<PeriodActualEntry> entries;
    std::vector<PeriodTaskTotal> totals; // Explicit cumulative AC including dated allocations, never added to them.
    std::string reporter, reason, recordedAt;
};
} // namespace jobs
