#include "application/Document.h"
#include <algorithm>
#include <map>
namespace jobs {
void Document::setResourcePlan(ResourcePlan plan) {
    requireEditable();
    if (plan == project_->resourcePlan)
        return;
    auto p = *project_;
    std::map<std::pair<std::string, std::string>, Assignment> previous;
    for (const auto &a : p.assignments)
        previous[{a.taskId, a.resourceId}] = a;
    for (const auto &d : p.resourcePlan.demands)
        if (!d.resourceId.empty())
            std::erase_if(p.assignments,
                          [&](const auto &a) { return a.taskId == d.taskId && a.resourceId == d.resourceId; });
    for (const auto &d : plan.demands)
        if (!d.resourceId.empty()) {
            if (std::any_of(p.assignments.begin(), p.assignments.end(),
                            [&](const auto &a) { return a.taskId == d.taskId && a.resourceId == d.resourceId; }))
                throw AppError(ErrorCode::ASSIGNMENT_INPUT_INVALID);
            auto assignment = previous.contains({d.taskId, d.resourceId}) ? previous.at({d.taskId, d.resourceId})
                                                                          : Assignment{d.taskId, d.resourceId, d.units};
            assignment.units = d.units;
            p.assignments.push_back(std::move(assignment));
        }
    if (!plan.demands.empty())
        p.calculationProfile = effortProfile;
    p.resourcePlan = std::move(plan);
    edit(std::move(p), "자재·비용·역할 계획");
}
} // namespace jobs
