#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace ppm {
struct ActivityStep {
    std::string id, name;
    int weight = 1;
    bool operator==(const ActivityStep &) const = default;
};
// Support intervals do not create executable work, resource demand or a second cost budget.
struct ActivityDefinition {
    std::string id, name, kind = "steps", taskId, startTaskId, finishTaskId, wbsId, calendarId;
    bool physicalFromSteps = true;
    std::vector<ActivityStep> steps;
    bool operator==(const ActivityDefinition &) const = default;
};
struct StepObservation {
    ActivityStep step;
    int basisPoints = 0;
    bool operator==(const StepObservation &) const = default;
};
struct StepReport {
    std::string id, taskId, statusAt, recordedAt, reporter, reason, sourceDocumentId;
    std::uint64_t recordedRevision = 0;
    std::vector<StepObservation> steps;
    bool operator==(const StepReport &) const = default;
};
} // namespace ppm
