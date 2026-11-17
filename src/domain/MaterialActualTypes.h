#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace jobs {
// A correction replaces the value of one stable transaction; zero voids a movement.
struct MaterialActualEntry {
    std::string id, transactionId, materialId, kind, at, taskId, useId, receiptId;
    std::int64_t quantityMilli = 0;
    std::string statusAt, recordedAt, reporter, reason, previousEntryId, sourceDocumentId;
    std::uint64_t recordedRevision = 0;
    // The inventory basis cannot silently change underneath observations.
    std::string unit, openingAt;
    std::int64_t openingMilli = 0;
    bool operator==(const MaterialActualEntry &) const = default;
};
struct MaterialActualSubmission {
    std::vector<MaterialActualEntry> entries;
    std::string reporter, reason, recordedAt;
};
} // namespace jobs
