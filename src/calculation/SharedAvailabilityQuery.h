#pragma once
#include "calculation/ResourceAvailabilityQuery.h"
#include "domain/Workspace.h"
namespace ppm {
struct SharedAvailabilitySource {
    std::string projectId, name, issue;
    std::uint64_t revision = 0;
    CalculationStatus status = CalculationStatus::Incomplete;
    bool forecast = false;
    bool available = false;
    std::optional<std::string> statusAt;
};
struct SharedAvailabilityContribution {
    std::string projectId, issue;
    std::optional<std::uint64_t> revision;
    std::optional<std::int64_t> demand;
    std::int64_t knownDemand = 0;
};
struct SharedAvailabilityPoint {
    ResourceAvailabilityPoint value;
    std::int64_t knownDemand = 0;
    std::vector<SharedAvailabilityContribution> contributions;
};
struct SharedAvailabilityResult {
    std::shared_ptr<const Workspace> configuration;
    std::string workspaceId, at;
    bool forecast = false;
    CalculationStatus status = CalculationStatus::Incomplete;
    std::vector<SharedAvailabilitySource> sources;
    std::vector<SharedAvailabilityPoint> points;
    std::size_t occupancyCount = 0, unmappedResources = 0;
};
class SharedAvailabilityQuery {
    struct Pool {
        Resource capacity;
        TaskCalendar calendar;
        std::map<std::string, std::vector<std::string>> bindings;
    };
    std::string workspaceId_;
    std::shared_ptr<const Workspace> configuration_;
    std::shared_ptr<CalculationControl> control_;
    bool forecast_;
    bool hasBindings_ = false;
    std::map<std::string, Pool> pools_;
    std::map<std::string, SharedAvailabilitySource> sources_;
    std::map<std::string, std::shared_ptr<const ResourceAvailabilityQuery>> queries_;
    std::map<std::pair<std::string, std::string>, std::string> invalidBindings_;
    std::size_t occupancyCount_ = 0, unmappedResources_ = 0;

public:
    SharedAvailabilityQuery(const Workspace &, const std::vector<std::shared_ptr<const Project>> &, bool forecast,
                            const std::shared_ptr<CalculationControl> & = {},
                            const std::map<std::string, ScheduleResult> *supplied = nullptr);
    SharedAvailabilityPoint at(const std::string &sharedId, const std::string &civilMinute) const;
    SharedAvailabilityResult allAt(const std::string &) const;
};
} // namespace ppm
