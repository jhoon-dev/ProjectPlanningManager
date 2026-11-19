#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace ppm {
struct Workspace;
struct OrganizationNode {
    std::string id, name, parentId, kind = "OBS";
    bool operator==(const OrganizationNode &) const = default;
};
struct WorkspaceMember {
    std::string id, name, organizationId;
    bool operator==(const WorkspaceMember &) const = default;
};
struct ProjectGrant {
    std::string memberId, projectId, role = "viewer";
    bool operator==(const ProjectGrant &) const = default;
};
struct PortfolioEntry {
    std::string projectId, epsId, ownerId, portfolio, currency = "KRW";
    std::int64_t budgetMinor = 0;
    int priority = 0;
    bool operator==(const PortfolioEntry &) const = default;
};
struct CollaborationEvent {
    std::string id, projectId, actorId, kind, text, createdAt, targetId;
    std::uint64_t projectRevision = 0;
    bool operator==(const CollaborationEvent &) const = default;
};
struct ScheduleRisk {
    std::string id, projectId, taskId, name, ownerId, state = "open", group;
    // Triangular duration, integer working minutes. Probability in basis points.
    std::int64_t optimistic = 0, likely = 0, pessimistic = 0;
    int probability = 10000;
    bool operator==(const ScheduleRisk &) const = default;
};
struct GovernanceAudit {
    std::string id, actorId, action, createdAt, detail;
    bool operator==(const GovernanceAudit &) const = default;
};
struct Governance {
    std::vector<OrganizationNode> organizations;
    std::vector<WorkspaceMember> members;
    std::vector<ProjectGrant> grants;
    std::vector<PortfolioEntry> portfolio;
    std::vector<CollaborationEvent> events;
    std::vector<ScheduleRisk> risks;
    std::vector<GovernanceAudit> audit;
    bool operator==(const Governance &) const = default;
};
enum class WorkspaceAction { View, Comment, Edit, Approve, Administer };
bool permits(const Workspace &, const std::string &actorId, const std::string &projectId, WorkspaceAction);
void validateGovernance(const Workspace &);
// Append-only collaboration records; revision is an observed source revision, never an approval of later edits.
void appendCollaboration(Workspace &, CollaborationEvent);
} // namespace ppm
