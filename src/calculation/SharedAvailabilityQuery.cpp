#include "calculation/SharedAvailabilityQuery.h"
#include <algorithm>
namespace jobs {
SharedAvailabilityQuery::SharedAvailabilityQuery(const Workspace &workspace,
                                                 const std::vector<std::shared_ptr<const Project>> &snapshots,
                                                 bool forecast, const std::shared_ptr<CalculationControl> &control,
                                                 const std::map<std::string, ScheduleResult> *supplied)
    : workspaceId_(workspace.id), configuration_(std::make_shared<const Workspace>(workspace)), control_(control),
      forecast_(forecast), hasBindings_(!workspace.bindings.empty()) {
    EvaluationScope evaluation(control);
    calculationCheckpoint();
    validateWorkspace(workspace);
    std::map<std::string, std::shared_ptr<const Project>> projects;
    for (const auto &p : snapshots)
        if (!p || !projects.emplace(p->documentId, p).second)
            throw AppError(ErrorCode::WORKSPACE_DUPLICATE_PROJECT);
    for (const auto &r : workspace.resources) {
        pools_.emplace(r.id, Pool{sharedResourceCapacity(r), r.calendar, {}});
    }
    std::map<std::string, std::set<std::string>> bound;
    for (const auto &b : workspace.bindings) {
        pools_.at(b.sharedId).bindings[b.projectId].push_back(b.resourceId);
        bound[b.projectId].insert(b.resourceId);
    }
    for (const auto &ref : workspace.projects) {
        EvaluationScope projectScope(control, std::make_shared<EvaluationCache>());
        calculationCheckpoint();
        SharedAvailabilitySource source;
        source.projectId = ref.documentId;
        source.name = ref.name;
        auto p = projects.find(ref.documentId);
        if (p == projects.end()) {
            source.issue = "프로젝트 파일이 열려 있지 않습니다.";
            sources_[ref.documentId] = std::move(source);
            continue;
        }
        const auto &input = *p->second;
        source.available = true;
        source.name = input.name;
        source.revision = input.revision;
        if (supplied && !supplied->contains(ref.documentId))
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        auto schedule = supplied ? supplied->at(ref.documentId) : (forecast ? calculate(input) : calculatePlan(input));
        if ((supplied || schedule.status == CalculationStatus::Valid) &&
            (schedule.documentId != input.documentId || schedule.inputRevision != input.revision ||
             schedule.profile != input.calculationProfile ||
             schedule.isForecast != (forecast && input.statusAt.has_value()) ||
             (schedule.isForecast && schedule.statusAt != input.statusAt)))
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        source.status = schedule.status;
        source.forecast = forecast && input.statusAt.has_value();
        source.statusAt = source.forecast ? input.statusAt : std::optional<std::string>{};
        if (schedule.status == CalculationStatus::Cancelled)
            throw CalculationCancelled{};
        std::map<std::string, const Resource *> locals;
        for (const auto &r : input.resources) {
            locals[r.id] = &r;
            if (!bound[ref.documentId].contains(r.id))
                ++unmappedResources_;
        }
        std::set<std::string> valid;
        for (const auto &b : workspace.bindings)
            if (b.projectId == ref.documentId) {
                if (!locals.contains(b.resourceId) ||
                    locals.at(b.resourceId)->type != pools_.at(b.sharedId).capacity.type)
                    invalidBindings_[{ref.documentId, b.resourceId}] =
                        "연결한 로컬 자원이 없거나 공유 자원 종류와 다릅니다.";
                else
                    valid.insert(b.resourceId);
            }
        if (schedule.status != CalculationStatus::Valid)
            source.issue = "프로젝트 일정이 불완전하거나 충돌 상태입니다.";
        else {
            auto query =
                std::make_shared<const ResourceAvailabilityQuery>(input, schedule, 500000 - occupancyCount_, &valid);
            occupancyCount_ += query->occupancyCount();
            queries_[ref.documentId] = std::move(query);
        }
        sources_[ref.documentId] = std::move(source);
    }
}
SharedAvailabilityPoint SharedAvailabilityQuery::at(const std::string &id, const std::string &stamp) const {
    EvaluationScope evaluation(control_);
    calculationCheckpoint();
    if (!validStamp(stamp) || !pools_.contains(id))
        throw AppError(ErrorCode::WORKSPACE_INVALID);
    const auto &pool = pools_.at(id);
    SharedAvailabilityPoint out;
    bool complete = true;
    for (const auto &[project, resources] : pool.bindings) {
        const auto &source = sources_.at(project);
        SharedAvailabilityContribution contribution;
        contribution.projectId = project;
        if (source.available)
            contribution.revision = source.revision;
        bool known = true;
        if (!queries_.contains(project)) {
            known = false;
            contribution.issue = source.issue;
        } else
            for (const auto &resource : resources) {
                calculationCheckpoint();
                if (const auto invalid = invalidBindings_.find({project, resource});
                    invalid != invalidBindings_.end()) {
                    known = false;
                    contribution.issue = invalid->second;
                    continue;
                }
                const auto point = queries_.at(project)->at(resource, stamp);
                if (point.demand)
                    contribution.knownDemand += *point.demand;
                else {
                    known = false;
                    contribution.issue = point.issue;
                }
            }
        if (known)
            contribution.demand = contribution.knownDemand;
        out.knownDemand += contribution.knownDemand;
        complete = complete && known;
        out.contributions.push_back(std::move(contribution));
    }
    out.value = resourceAvailabilityPoint(
        pool.capacity, pool.calendar, stamp,
        complete ? std::optional<std::int64_t>{out.knownDemand} : std::optional<std::int64_t>{},
        "연결된 프로젝트의 전체 수요를 확인할 수 없습니다. 확인된 수요는 별도로 표시합니다.");
    return out;
}
SharedAvailabilityResult SharedAvailabilityQuery::allAt(const std::string &stamp) const {
    EvaluationScope evaluation(control_);
    calculationCheckpoint();
    SharedAvailabilityResult out;
    out.configuration = configuration_;
    out.workspaceId = workspaceId_;
    out.at = stamp;
    out.forecast = forecast_;
    out.occupancyCount = occupancyCount_;
    out.unmappedResources = unmappedResources_;
    out.status = CalculationStatus::Valid;
    if (!validStamp(stamp))
        throw AppError(ErrorCode::CIVIL_TIMESTAMP_INVALID);
    for (const auto &[id, source] : sources_) {
        (void)id;
        out.sources.push_back(source);
        if (source.status != CalculationStatus::Valid)
            out.status = CalculationStatus::Incomplete;
    }
    for (const auto &[id, pool] : pools_) {
        (void)pool;
        auto point = at(id, stamp);
        if (!point.value.remaining)
            out.status = CalculationStatus::Incomplete;
        else if (*point.value.remaining < 0 && out.status == CalculationStatus::Valid)
            out.status = CalculationStatus::Conflict;
        out.points.push_back(std::move(point));
    }
    if (sources_.empty() || pools_.empty() || !hasBindings_)
        out.status = CalculationStatus::Incomplete;
    return out;
}
} // namespace jobs
