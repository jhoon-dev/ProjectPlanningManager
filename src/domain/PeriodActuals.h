#pragma once
#include "domain/Project.h"
#include <map>
#include <tuple>
namespace ppm {
using PeriodActualKey = std::tuple<std::string, std::string, std::string>; // Task, resource (empty = task cost), day.
PeriodActualKey periodActualKey(const PeriodActualEntry &);
std::map<PeriodActualKey, const PeriodActualEntry *> periodActualsAsOf(const Project &, const std::string &statusAt);
struct ActualCostAllocation {
    std::optional<std::int64_t> cumulative, unallocated;
    std::int64_t allocated = 0;
    std::size_t costReports = 0;
};
std::map<std::string, ActualCostAllocation> actualCostAllocations(const Project &, const std::string &statusAt);
void validatePeriodActuals(const Project &);
// At most six decimal hours, or exact integer %·minutes with a 'u' suffix.
std::int64_t parseActualHours(const std::string &);
} // namespace ppm
