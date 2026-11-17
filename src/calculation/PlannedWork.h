#pragma once
#include "calculation/Scheduler.h"
namespace jobs {
enum class PlannedWorkPeriod { Day, Week, Month };
struct PlannedWorkAmount {
    std::int64_t fromMinute = 0, toMinute = 0, workUnitMinutes = 0;
    bool operator==(const PlannedWorkAmount &) const = default;
};
struct PlannedWorkBucket {
    PlannedWorkAmount amount;
    std::string start, finish;
    std::int64_t availableMinutes = 0;
};
struct PlannedWorkResult {
    std::string documentId, taskId, resourceId;
    std::uint64_t revision = 0;
    TaskSchedule dates;
    std::vector<PlannedWorkBucket> buckets;
    std::vector<AssignmentPeriod> periods;
};
// Logical effort plan before adoption. Existing adoptions/facts/archives are never modified.
PlannedWorkResult describePlannedWork(const Project &, const std::string &taskId, const std::string &resourceId,
                                      PlannedWorkPeriod, std::shared_ptr<CalculationControl> = {});
// Convert selected work amounts into integer rate overrides, preserving all other offsets.
// Targets are exact at these source dates/calendars; stored rates replay after schedule changes.
PlannedWorkResult convertPlannedWork(const Project &, const std::string &taskId, const std::string &resourceId,
                                     const std::vector<PlannedWorkAmount> &, std::shared_ptr<CalculationControl> = {});
// Four decimal hours round-trip every integer hundredth-resource-minute through half-up quantization.
std::string plannedWorkHours(std::int64_t);
std::int64_t parsePlannedWorkHours(const std::string &);
} // namespace jobs
