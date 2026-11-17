#include "domain/ResourcePlan.h"
#include "domain/ResourceAvailability.h"
#include <tuple>
#include "domain/Workspace.h"
#include <set>

namespace jobs {
namespace {
bool text(const std::string &value, std::size_t maximum, bool empty = false) {
    return (empty || !value.empty()) && value.size() <= maximum &&
           value.find_first_of(std::string("\0\r\n", 3)) == std::string::npos;
}
} // namespace
bool hasSharedResourceAvailability(const SharedResource &r) {
    return r.availableFrom || r.availableTo || !r.availability.empty();
}
Resource sharedResourceCapacity(const SharedResource &r) {
    Resource capacity;
    capacity.id = r.id;
    capacity.name = r.name;
    capacity.type = r.type;
    capacity.maxUnits = r.maxUnits;
    capacity.exceptions = r.exceptions;
    capacity.availableFrom = r.availableFrom;
    capacity.availableTo = r.availableTo;
    capacity.availability = r.availability;
    return capacity;
}
void validateWorkspace(const Workspace &workspace, bool requirePaths) {
    validateGovernance(workspace);
    if (!text(workspace.id, 200))
        throw AppError(ErrorCode::WORKSPACE_INVALID, {"id"});
    if (workspace.projects.size() > 100 || workspace.resources.size() > 1000 || workspace.bindings.size() > 10000)
        throw AppError(ErrorCode::WORKSPACE_LIMIT);
    std::set<std::string> projects, paths, resources;
    for (const auto &ref : workspace.projects) {
        if (!text(ref.documentId, 200) || !text(ref.path, 4096, !requirePaths) ||
            ref.name.find('\0') != std::string::npos)
            throw AppError(ErrorCode::WORKSPACE_INVALID, {"project reference"});
        if (!projects.insert(ref.documentId).second)
            throw AppError(ErrorCode::WORKSPACE_DUPLICATE_PROJECT, {ref.documentId});
        if (!ref.path.empty() && !paths.insert(ref.path).second)
            throw AppError(ErrorCode::WORKSPACE_DUPLICATE_FILE, {ref.path});
    }
    for (const auto &shared : workspace.resources) {
        if (!text(shared.id, 200) || !resources.insert(shared.id).second || !shared.calendar.id.empty() ||
            !shared.calendar.name.empty() || shared.name.find('\0') != std::string::npos)
            throw AppError(ErrorCode::WORKSPACE_INVALID, {"shared resource identity"});
        if (shared.exceptions.size() > 10000 || shared.calendar.periods.size() > 100 ||
            shared.calendar.holidays.size() > 10000)
            throw AppError(ErrorCode::WORKSPACE_LIMIT);
        PlanInput plan;
        plan.name = "validation";
        plan.startDate = "2026-01-01";
        plan.calculationProfile = effortProfile;
        auto calendar = shared.calendar;
        calendar.id = "shared-calendar";
        calendar.name = shared.name;
        plan.calendars.push_back(calendar);
        Resource resource;
        resource.id = shared.id;
        resource.name = shared.name;
        resource.type = shared.type;
        resource.calendarId = calendar.id;
        plan.resources.push_back(resource);
        try {
            // A shared pool combines projects and has a higher ceiling than a local Resource.
            const auto validUnits = [](const std::optional<int> &value) {
                return !value || (*value >= 0 && *value <= maxSharedResourceUnits);
            };
            if (!validUnits(shared.maxUnits))
                throw AppError(ErrorCode::RESOURCE_INPUT_INVALID);
            validateResourceAvailability(sharedResourceCapacity(shared), maxSharedResourceUnits);
            std::set<std::string> dates;
            for (const auto &day : shared.exceptions)
                if (!validDate(day.date) || !dates.insert(day.date).second || !validUnits(day.units))
                    throw AppError(ErrorCode::RESOURCE_EXCEPTION_INVALID);
            validatePlan(plan);
        } catch (const AppError &error) {
            auto diagnostic = error.diagnostic();
            diagnostic.resourceIds = {shared.id};
            diagnostic.calendarIds = {shared.id};
            throw AppError(std::move(diagnostic));
        }
    }
    if (workspace.links.size() > 10000 || workspace.integrated.horizonWorkDays < 1 ||
        workspace.integrated.horizonWorkDays > 5200 ||
        (workspace.integrated.priority != "task-order" && workspace.integrated.priority != "shortest-first" &&
         workspace.integrated.priority != "longest-first" && !advancedPriority(workspace.integrated.priority)))
        throw AppError(ErrorCode::WORKSPACE_INVALID, {"통합 일정 옵션/링크 한도"});
    std::set<std::string> linkIds;
    std::set<std::tuple<std::string, std::string, std::string, std::string>> endpoints;
    for (const auto &link : workspace.links) {
        if (!text(link.id, 200) || !linkIds.insert(link.id).second || !projects.contains(link.predecessorProjectId) ||
            !projects.contains(link.successorProjectId) || link.predecessorProjectId == link.successorProjectId ||
            !text(link.predecessorTaskId, 1024) || !text(link.successorTaskId, 1024) || !isValid(link.type) ||
            link.lagMinutes < -5000000 || link.lagMinutes > 5000000 ||
            !endpoints
                 .emplace(link.predecessorProjectId, link.predecessorTaskId, link.successorProjectId,
                          link.successorTaskId)
                 .second)
            throw AppError(ErrorCode::WORKSPACE_INVALID, {"프로젝트 간 링크 ID·참조·중복·지연"});
    }
    std::set<std::pair<std::string, std::string>> locals;
    for (const auto &binding : workspace.bindings) {
        if (!projects.contains(binding.projectId) || !resources.contains(binding.sharedId) ||
            !text(binding.resourceId, 200) || !locals.emplace(binding.projectId, binding.resourceId).second)
            throw AppError(ErrorCode::WORKSPACE_BINDING_INVALID, {binding.projectId + "/" + binding.resourceId});
    }
}
} // namespace jobs
