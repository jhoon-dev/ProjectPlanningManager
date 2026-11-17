#include "domain/Governance.h"
#include "domain/Workspace.h"
#include <algorithm>
#include <map>
#include <set>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::WORKSPACE_INVALID, {message});
}
bool text(const std::string &s, bool empty = false, std::size_t maximum = 200) {
    return (empty || !s.empty()) && s.size() <= maximum && s.find('\0') == std::string::npos &&
           s.find_first_of("\r\n") == std::string::npos;
}
} // namespace
bool permits(const Workspace &w, const std::string &actor, const std::string &project, WorkspaceAction action) {
    if (w.governance == Governance{})
        return true;
    bool allowed = false;
    for (const auto &g : w.governance.grants) {
        if (g.memberId != actor || (!g.projectId.empty() && g.projectId != project))
            continue;
        if (g.role == "owner" && g.projectId.empty())
            return true;
        if (action == WorkspaceAction::View || action == WorkspaceAction::Comment)
            allowed = true;
        if (action == WorkspaceAction::Edit && g.role == "editor")
            allowed = true;
        if (action == WorkspaceAction::Approve && g.role == "reviewer")
            allowed = true;
    }
    return allowed;
}
void validateGovernance(const Workspace &w) {
    const auto &g = w.governance;
    if (g == Governance{})
        return;
    if (g.organizations.size() > 1000 || g.members.size() > 1000 || g.grants.size() > 10000 ||
        g.portfolio.size() > 100 || g.events.size() > 10000 || g.risks.size() > 1000 || g.audit.size() > 10000)
        throw AppError(ErrorCode::WORKSPACE_LIMIT);
    std::set<std::string> projects, members;
    std::map<std::string, OrganizationNode> org;
    for (const auto &p : w.projects)
        projects.insert(p.documentId);
    for (const auto &o : g.organizations) {
        if (!text(o.id) || !text(o.name) || (o.kind != "OBS" && o.kind != "EPS") || !org.emplace(o.id, o).second)
            invalid("조직 ID·이름·유형·중복");
    }
    for (const auto &o : g.organizations) {
        std::set<std::string> visited{o.id};
        auto parent = o.parentId;
        while (!parent.empty()) {
            if (!org.contains(parent) || org.at(parent).kind != o.kind || !visited.insert(parent).second)
                invalid("조직 계층 참조·유형·순환");
            parent = org.at(parent).parentId;
        }
    }
    for (const auto &m : g.members)
        if (!text(m.id) || !text(m.name) || !members.insert(m.id).second ||
            (!m.organizationId.empty() && (!org.contains(m.organizationId) || org.at(m.organizationId).kind != "OBS")))
            invalid("구성원·OBS 참조");
    bool owner = false;
    std::set<std::pair<std::string, std::string>> grants;
    for (const auto &r : g.grants) {
        if (!members.contains(r.memberId) || (!r.projectId.empty() && !projects.contains(r.projectId)) ||
            !grants.emplace(r.memberId, r.projectId).second ||
            (r.role != "owner" && r.role != "editor" && r.role != "reviewer" && r.role != "viewer") ||
            (r.role == "owner" && !r.projectId.empty()))
            invalid("역할·프로젝트·구성원 참조");
        owner |= r.role == "owner";
    }
    if (!owner)
        invalid("작업공간 소유자가 한 명 이상 필요합니다.");
    std::set<std::string> portfolio, risks, events, resolved, audits;
    for (const auto &a : g.audit)
        if (!text(a.id) || !audits.insert(a.id).second || !text(a.actorId) || !text(a.action) ||
            !validStamp(a.createdAt) || !text(a.detail, false, 4096))
            invalid("감사 기록 ID·작성자·시각·내용");
    for (const auto &p : g.portfolio)
        if (!projects.contains(p.projectId) || !portfolio.insert(p.projectId).second ||
            (!p.epsId.empty() && (!org.contains(p.epsId) || org.at(p.epsId).kind != "EPS")) ||
            (!p.ownerId.empty() && !members.contains(p.ownerId)) || !text(p.portfolio) ||
            (p.currency != "KRW" && p.currency != "USD" && p.currency != "EUR" && p.currency != "JPY") ||
            p.budgetMinor < 0 || p.budgetMinor > maxMoney || p.priority < 0 || p.priority > 100)
            invalid("포트폴리오·예산·통화·책임자");
    std::set<std::pair<std::string, std::string>> openRiskTasks;
    for (const auto &r : g.risks)
        if (!text(r.id) || !risks.insert(r.id).second || !projects.contains(r.projectId) ||
            !text(r.taskId, false, 1024) || !text(r.name) || (!r.ownerId.empty() && !members.contains(r.ownerId)) ||
            (r.state != "open" && r.state != "closed") || !text(r.group, true) || r.optimistic < 0 ||
            r.optimistic > r.likely || r.likely > r.pessimistic || r.pessimistic > 5000000 || r.probability < 0 ||
            r.probability > 10000 || (r.state == "open" && !openRiskTasks.emplace(r.projectId, r.taskId).second))
            invalid("위험 분포·확률·상태·참조");
    for (const auto &e : g.events) {
        if (!text(e.id) || !events.insert(e.id).second || !projects.contains(e.projectId) ||
            !members.contains(e.actorId) || !text(e.text, false, 4096) || !validStamp(e.createdAt) ||
            e.projectRevision > 9007199254740991ULL ||
            (e.kind != "comment" && e.kind != "request" && e.kind != "approve" && e.kind != "reject" &&
             e.kind != "resolve"))
            invalid("협업 기록·작성자·시각·종류");
        if (e.kind == "approve" || e.kind == "reject" || e.kind == "resolve") {
            const auto target = std::find_if(g.events.begin(), g.events.end(), [&](const auto &v) {
                return v.id == e.targetId && v.projectId == e.projectId && v.kind == "request";
            });
            if (target == g.events.end() || &*target >= &e || target->projectRevision != e.projectRevision ||
                !resolved.insert(e.targetId).second)
                invalid("승인 대상·revision");
        } else if (!e.targetId.empty())
            invalid("협업 대상");
    }
}
void appendCollaboration(Workspace &w, CollaborationEvent event) {
    const auto action = event.kind == "approve" || event.kind == "reject" || event.kind == "resolve"
                            ? WorkspaceAction::Approve
                            : WorkspaceAction::Comment;
    if (!permits(w, event.actorId, event.projectId, action))
        invalid("이 역할에는 협업 기록 권한이 없습니다.");
    if (!event.targetId.empty())
        for (const auto &e : w.governance.events)
            if (e.targetId == event.targetId)
                invalid("이미 처리한 요청입니다.");
    auto proposed = w;
    proposed.governance.events.push_back(std::move(event));
    validateWorkspace(proposed);
    w = std::move(proposed);
}
} // namespace jobs
