#pragma once
#include "domain/Project.h"
#include "domain/Governance.h"

namespace jobs {
inline constexpr int maxSharedResourceUnits = 10000000;
struct ProjectReference {
    std::string documentId, path, name;
    bool operator==(const ProjectReference &) const = default;
};
struct SharedResource {
    std::string id, name, type = "labor";
    std::optional<int> maxUnits = 100;
    std::vector<CapacityDay> exceptions = {};
    TaskCalendar calendar = {};
    std::optional<std::string> availableFrom = {}, availableTo = {};
    std::vector<AvailabilityPeriod> availability = {};
    bool operator==(const SharedResource &) const = default;
};
struct ResourceBinding {
    std::string projectId, resourceId, sharedId;
    bool operator==(const ResourceBinding &) const = default;
};
struct ProjectLink {
    std::string id, predecessorProjectId, predecessorTaskId, successorProjectId, successorTaskId;
    DependencyType type = DependencyType::FS;
    std::int64_t lagMinutes = 0;
    bool operator==(const ProjectLink &) const = default;
};
struct IntegratedOptions {
    bool forecast = true, levelShared = false;
    std::string priority = "task-order";
    int horizonWorkDays = 260;
    bool operator==(const IntegratedOptions &) const = default;
};
struct Workspace {
    std::string id;
    std::vector<ProjectReference> projects;
    std::vector<SharedResource> resources;
    std::vector<ResourceBinding> bindings;
    std::vector<ProjectLink> links;
    IntegratedOptions integrated;
    Governance governance;
    bool operator==(const Workspace &) const = default;
};
void validateWorkspace(const Workspace &, bool requirePaths = false);
bool hasSharedResourceAvailability(const SharedResource &);
Resource sharedResourceCapacity(const SharedResource &);
} // namespace jobs
