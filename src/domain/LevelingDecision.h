#pragma once
#include "domain/LevelingSelection.h"
#include <optional>
#include <vector>
namespace jobs {
struct Project;
struct LevelingDecision {
    std::string id, previousEntryId, recordedAt, sourceDocumentId, action, reason;
    std::uint64_t sourceRevision = 0, recordedRevision = 0;
    std::optional<std::string> beforeStatusAt, afterStatusAt;
    std::optional<LevelingSelection> beforePlan, beforeRemaining, afterPlan, afterRemaining;
    bool operator==(const LevelingDecision &) const = default;
};
// Called on an isolated proposal, after revision increment and before commit.
void appendLevelingDecision(Project &proposal, const Project &before, const std::string &action,
                            const std::string &reason);
void validateLevelingDecisions(const Project &);
} // namespace jobs
