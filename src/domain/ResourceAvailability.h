#pragma once
#include "domain/Project.h"
namespace jobs {
bool hasResourceAvailability(const Resource &);
void validateResourceAvailability(const Resource &, int maxUnits = 100000);
std::optional<int> resourceUnitsAt(const Resource &, const std::string &civilMinute);
std::vector<std::string> resourceCapacityChanges(const Resource &);
std::vector<AvailabilityPeriod> resourceAvailabilityIntervals(const Resource &);
} // namespace jobs
