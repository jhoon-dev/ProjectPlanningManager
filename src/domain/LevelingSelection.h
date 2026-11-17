#pragma once
#include <cstdint>
#include <string>
namespace jobs {
struct LevelingSelection {
    std::string priority = "task-order", policy, adoptedAt, reason, sourceDocumentId;
    int horizonWorkDays = 260;
    std::uint64_t sourceRevision = 0;
    bool operator==(const LevelingSelection &) const = default;
};
} // namespace jobs
