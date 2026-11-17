#pragma once
#include "calculation/Resources.h"
#include <map>
namespace jobs {
struct ResourceAvailabilityPoint {
    std::string resourceId, at, issue;
    std::optional<int> capacity;
    std::optional<std::int64_t> demand, remaining, freeUnits, shortage;
    bool working = false;
};
ResourceAvailabilityPoint resourceAvailabilityPoint(const Resource &, const TaskCalendar &, const std::string &,
                                                    std::optional<std::int64_t>, const std::string &issue = {});
// Immutable whole-project demand snapshot; civil intervals have an exclusive finish.
class ResourceAvailabilityQuery {
    struct Entry {
        Resource resource;
        std::shared_ptr<const TaskCalendar> calendar;
        std::map<std::string, std::int64_t> demand;
    };
    std::map<std::string, Entry> resources_;
    std::optional<std::string> forecastStatus_;
    std::size_t occupancyCount_ = 0;

public:
    ResourceAvailabilityQuery(const Project &, const ScheduleResult &, std::size_t limit = 500000,
                              const std::set<std::string> *includedResources = nullptr);
    ResourceAvailabilityPoint at(const std::string &resourceId, const std::string &civilMinute) const;
    std::size_t occupancyCount() const {
        return occupancyCount_;
    }
};
} // namespace jobs
