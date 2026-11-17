#include "calculation/IntegratedWorkspace.h"
#include "calculation/MaterialInventory.h"
#include "calculation/LevelingFloat.h"
#include "calculation/MaterialReservations.h"
#include "calculation/WorkCalendar.h"
#include "domain/ResourcePlan.h"
#include <algorithm>
#include <map>
#include <set>
namespace jobs {
namespace {
using Key = std::pair<std::string, std::string>;
[[noreturn]] void invalid(const std::string &reason) {
    throw AppError(ErrorCode::WORKSPACE_INVALID, {reason});
}
TaskCalendar calendar(const Project &p, const std::string &id, bool resource = false) {
    TaskCalendar c;
    c.holidays = p.holidays;
    if (resource && !isEffort(p)) {
        c.weekdays = {1, 2, 3, 4, 5, 6, 7};
        c.periods = {{0, 1440}};
        c.holidays.clear();
    } else if (!id.empty()) {
        const auto found =
            std::find_if(p.calendars.begin(), p.calendars.end(), [&](const auto &v) { return v.id == id; });
        if (found == p.calendars.end())
            invalid("달력 참조");
        c = *found;
    }
    c.id.clear();
    c.name.clear();
    return c;
}
void mergeAssignment(Assignment &target, const Assignment &other) {
    const auto base = target.units + other.units;
    if (base > 100000)
        invalid("동일 작업의 공유 배정 합계가 100,000%를 초과합니다.");
    std::set<std::int64_t> boundaries;
    for (const auto *a : {static_cast<const Assignment *>(&target), &other})
        for (const auto &p : a->periods) {
            boundaries.insert(p.fromMinute);
            boundaries.insert(p.toMinute);
        }
    const auto unitsAt = [](const Assignment &a, std::int64_t minute) {
        for (const auto &p : a.periods)
            if (p.fromMinute <= minute && minute < p.toMinute)
                return p.units;
        return a.units;
    };
    std::vector<AssignmentPeriod> periods;
    for (auto i = boundaries.begin(); i != boundaries.end(); ++i) {
        calculationCheckpoint();
        auto next = std::next(i);
        if (next == boundaries.end())
            break;
        const auto units = unitsAt(target, *i) + unitsAt(other, *i);
        if (units > 100000)
            invalid("동일 작업의 공유 기간 배정 합계가 100,000%를 초과합니다.");
        if (units == base)
            continue;
        if (!periods.empty() && periods.back().toMinute == *i && periods.back().units == units)
            periods.back().toMinute = *next;
        else
            periods.push_back({*i, *next, units});
    }
    target.units = base;
    target.periods = std::move(periods);
}
} // namespace
IntegratedResult calculateIntegratedWorkspace(const Workspace &w,
                                              const std::vector<std::shared_ptr<const Project>> &snapshots,
                                              const std::shared_ptr<CalculationControl> &control) {
    EvaluationScope evaluation(control);
    calculationCheckpoint();
    validateWorkspace(w);
    IntegratedResult out;
    out.leveled = w.integrated.levelShared;
    out.policy =
        std::string("native-integrated-v1/") + (out.leveled ? "shared-leveling/" : "logical/") + w.integrated.priority;
    try {
        if (w.projects.empty())
            invalid("프로젝트를 추가하세요.");
        std::map<std::string, std::shared_ptr<const Project>> sources;
        for (const auto &p : snapshots)
            if (!p || !sources.emplace(p->documentId, p).second)
                invalid("프로젝트 snapshot 중복");
        Project flat;
        flat.documentId = "integrated-" + w.id;
        flat.name = "통합 일정";
        flat.calculationProfile = effortProfile;
        std::optional<std::string> status;
        bool first = true;
        std::size_t taskCount = w.projects.size(), assignmentCount = 0;
        for (const auto &ref : w.projects) {
            if (!sources.contains(ref.documentId))
                throw AppError(ErrorCode::WORKSPACE_PROJECT_NOT_OPEN, {ref.name});
            const auto &p = *sources.at(ref.documentId);
            taskCount += p.tasks.size();
            assignmentCount += p.assignments.size();
            if (taskCount > 10000 || assignmentCount > 100000)
                throw AppError(ErrorCode::WORKSPACE_LIMIT);
            validate(p);
            if (first) {
                flat.startDate = p.startDate;
                status = p.statusAt;
                first = false;
            } else {
                flat.startDate = std::min(flat.startDate, p.startDate);
                if (w.integrated.forecast && status != p.statusAt)
                    invalid("통합 예측은 모든 프로젝트의 동일한 상태일이 필요합니다.");
            }
            flat.revision = std::max(flat.revision, p.revision);
            for (const auto &d : p.resourcePlan.demands)
                if (d.resourceId.empty())
                    invalid(p.name + " · 역할을 구체 자원에 연결하세요.");
            if (out.leveled) {
                const auto stock = w.integrated.forecast && p.statusAt && p.resourcePlan.autoDelayRemainingForReceipts
                                       ? materialAutoReadiness(p)
                                       : materialReadiness(p);
                if (!stock.empty()) {
                    auto issue = stock.front();
                    issue.projectId = p.documentId;
                    throw AppError(std::move(issue));
                }
            }
            if (p.planLeveling || p.remainingLeveling)
                out.diagnostics.push_back(
                    {"INTEGRATED_POLICY",
                     {p.name + " · 개별 평준화 채택은 원본에 보존하며 통합 계산에서 다시 평준화합니다."},
                     {},
                     "INTEGRATED_POLICY"});
        }
        flat.statusAt = w.integrated.forecast ? status : std::nullopt;
        if (taskCount == w.projects.size())
            invalid("통합할 작업을 추가하세요.");
        out.forecast = flat.statusAt.has_value();
        std::map<Key, std::string> taskIds, resourceIds;
        std::map<std::string, ProjectTaskRef> reverse;
        std::map<std::string, const SharedResource *> pools;
        for (const auto &r : w.resources)
            pools.emplace(r.id, &r);
        std::map<Key, std::string> bindings;
        for (const auto &b : w.bindings)
            bindings.emplace(Key{b.projectId, b.resourceId}, b.sharedId);
        std::map<std::string, std::string> poolIds;
        std::map<std::string, Project> projected;
        std::map<std::string, ScheduleResult> original;
        auto addCalendar = [&](TaskCalendar c) {
            c.id = "cal-" + std::to_string(flat.calendars.size());
            c.name = c.id;
            flat.calendars.push_back(c);
            return c.id;
        };
        std::map<Key, std::size_t> assignmentIndexes;
        std::map<Key, std::size_t> assignmentSources;
        std::size_t remainingDays = 0;
        for (std::size_t index = 0; index < w.projects.size(); ++index) {
            const auto &p = *sources.at(w.projects[index].documentId);
            auto local = p;
            local.calculationProfile = effortProfile;
            local.planLeveling.reset();
            local.remainingLeveling.reset();
            local.levelingDecisions.clear();
            original.emplace(p.documentId, calculate(p));
            for (std::size_t i = 0; i < p.tasks.size(); ++i) {
                const auto id = "task-" + std::to_string(index) + "-" + std::to_string(i);
                taskIds.emplace(Key{p.documentId, p.tasks[i].id}, id);
                reverse.emplace(id, ProjectTaskRef{p.documentId, p.tasks[i].id});
            }
            for (std::size_t i = 0; i < p.resources.size(); ++i) {
                calculationCheckpoint();
                const auto &r = p.resources[i];
                const Key key{p.documentId, r.id};
                auto resolved = calendar(p, r.calendarId, true);
                std::string id;
                if (out.leveled && bindings.contains(key)) {
                    const auto &shared = *pools.at(bindings.at(key));
                    if (shared.type != r.type)
                        invalid("공유 자원 종류가 로컬 자원과 다릅니다.");
                    if (isEffort(p) && resolved != shared.calendar)
                        invalid("공유 평준화는 로컬/공유 자원 달력이 일치해야 합니다: " + p.name + " / " + r.name);
                    resolved = shared.calendar;
                    if (!poolIds.contains(shared.id)) {
                        Resource pool = sharedResourceCapacity(shared);
                        pool.id = "pool-" + std::to_string(poolIds.size());
                        pool.name = shared.name;
                        pool.type = shared.type;
                        pool.maxUnits = shared.maxUnits;
                        pool.exceptions = shared.exceptions;
                        pool.calendarId = addCalendar(resolved);
                        if ((pool.maxUnits && *pool.maxUnits > 100000) ||
                            std::any_of(pool.exceptions.begin(), pool.exceptions.end(),
                                        [](const auto &e) { return e.units && *e.units > 100000; }) ||
                            std::any_of(pool.availability.begin(), pool.availability.end(),
                                        [](const auto &e) { return e.units && *e.units > 100000; }))
                            invalid("통합 평준화의 공유 가용량 상한은 100,000%입니다.");
                        poolIds.emplace(shared.id, pool.id);
                        flat.resources.push_back(pool);
                    }
                    id = poolIds.at(shared.id);
                } else {
                    Resource copy = r;
                    id = "resource-" + std::to_string(index) + "-" + std::to_string(i);
                    copy.id = id;
                    copy.calendarId = addCalendar(resolved);
                    copy.attributes.clear();
                    copy.rates.clear();
                    flat.resources.push_back(std::move(copy));
                }
                resourceIds.emplace(key, id);
                // Preserve the same effective execution calendars when evaluating the projected pooled load.
                resolved.id = "integrated-resource-" + std::to_string(i);
                while (std::any_of(local.calendars.begin(), local.calendars.end(),
                                   [&](const auto &c) { return c.id == resolved.id; }))
                    resolved.id += "x";
                resolved.name = resolved.id;
                local.calendars.push_back(resolved);
                local.resources[i].calendarId = resolved.id;
            }
            projected.emplace(p.documentId, std::move(local));
        }
        for (std::size_t index = 0; index < w.projects.size(); ++index) {
            const auto &p = *sources.at(w.projects[index].documentId);
            if (p.tasks.empty())
                continue;
            Task gate;
            gate.id = "release-" + std::to_string(index);
            gate.name = p.name + " 시작 기준";
            gate.durationMinutes = 0;
            gate.calendarId = addCalendar(calendar(p, {}));
            gate.constraintType = ConstraintType::SNET;
            gate.constraintAt = p.startDate + " 09:00";
            const bool needsGate = std::any_of(p.tasks.begin(), p.tasks.end(), [&](const auto &t) {
                const auto *fact = out.forecast ? progressAsOf(p, t.id) : nullptr;
                return !fact || !fact->actualStart;
            });
            if (needsGate)
                flat.tasks.push_back(gate);
            for (const auto &t : p.tasks) {
                calculationCheckpoint();
                Task copy = t;
                copy.id = taskIds.at({p.documentId, t.id});
                copy.wbsId.clear();
                copy.attributes.clear();
                copy.calendarId = addCalendar(calendar(p, t.calendarId));
                for (auto &prev : copy.predecessors) {
                    if (!taskIds.contains({p.documentId, prev}))
                        invalid("로컬 작업 참조 누락");
                    prev = taskIds.at({p.documentId, prev});
                }
                for (auto &d : copy.dependencyRules)
                    d.predecessorId = taskIds.at({p.documentId, d.predecessorId});
                const auto *fact = out.forecast ? progressAsOf(p, t.id) : nullptr;
                if (!fact || !fact->actualStart) {
                    copy.predecessors.push_back(gate.id);
                    copy.dependencyRules.push_back({gate.id, DependencyType::SS, 0});
                }
                flat.tasks.push_back(std::move(copy));
            }
            for (const auto &a : p.assignments) {
                auto copy = a;
                copy.taskId = taskIds.at({p.documentId, a.taskId});
                copy.resourceId = resourceIds.at({p.documentId, a.resourceId});
                const Key key{copy.taskId, copy.resourceId};
                ++assignmentSources[key];
                if (assignmentIndexes.contains(key))
                    mergeAssignment(flat.assignments[assignmentIndexes.at(key)], copy);
                else {
                    assignmentIndexes.emplace(key, flat.assignments.size());
                    flat.assignments.push_back(std::move(copy));
                }
            }
            if (out.leveled &&
                (out.forecast ? p.resourcePlan.autoDelayRemainingForReceipts : p.resourcePlan.autoDelayForReceipts)) {
                if (flat.resourcePlan.materials.size() + p.resourcePlan.materials.size() > 10000 ||
                    flat.resourcePlan.uses.size() + p.resourcePlan.uses.size() > 100000 ||
                    flat.resourcePlan.receipts.size() + p.resourcePlan.receipts.size() > 100000 ||
                    flat.materialActuals.size() + (out.forecast ? p.materialActuals.size() : 0) > 100000)
                    throw AppError(ErrorCode::WORKSPACE_LIMIT);
                flat.resourcePlan.autoDelayForReceipts = !out.forecast;
                flat.resourcePlan.autoDelayRemainingForReceipts = out.forecast;
                std::map<std::string, std::string> materials, uses, receipts;
                for (const auto &m : p.resourcePlan.materials) {
                    calculationCheckpoint();
                    auto copy = m;
                    copy.id = "material-" + std::to_string(flat.resourcePlan.materials.size());
                    materials[m.id] = copy.id;
                    copy.stockAt = m.stockAt.value_or(p.startDate + " 09:00");
                    flat.resourcePlan.materials.push_back(std::move(copy));
                }
                for (const auto &u : p.resourcePlan.uses) {
                    calculationCheckpoint();
                    auto copy = u;
                    copy.id = "material-use-" + std::to_string(flat.resourcePlan.uses.size());
                    uses[u.id] = copy.id;
                    copy.taskId = taskIds.at({p.documentId, u.taskId});
                    copy.materialId = materials.at(u.materialId);
                    flat.resourcePlan.uses.push_back(std::move(copy));
                }
                for (const auto &r : p.resourcePlan.receipts) {
                    calculationCheckpoint();
                    auto copy = r;
                    copy.id = "material-receipt-" + std::to_string(flat.resourcePlan.receipts.size());
                    receipts[r.id] = copy.id;
                    copy.materialId = materials.at(r.materialId);
                    flat.resourcePlan.receipts.push_back(std::move(copy));
                }
                if (out.forecast) {
                    std::map<std::string, std::string> entries;
                    for (std::size_t i = 0; i < p.materialActuals.size(); ++i)
                        entries[p.materialActuals[i].id] =
                            "material-actual-" + std::to_string(index) + "-" + std::to_string(i);
                    for (auto e : p.materialActuals) {
                        calculationCheckpoint();
                        e.id = entries.at(e.id);
                        e.transactionId = entries.at(e.transactionId);
                        if (!e.previousEntryId.empty())
                            e.previousEntryId = entries.at(e.previousEntryId);
                        e.materialId = materials.at(e.materialId);
                        if (!e.taskId.empty())
                            e.taskId = taskIds.at({p.documentId, e.taskId});
                        if (!e.useId.empty())
                            e.useId = uses.at(e.useId);
                        if (!e.receiptId.empty())
                            e.receiptId = receipts.at(e.receiptId);
                        flat.materialActuals.push_back(std::move(e));
                    }
                    const auto inventory = remainingMaterialInventoryInput(p);
                    std::map<std::string, std::int64_t> demand, incoming;
                    std::set<std::string> used;
                    for (const auto &u : p.resourcePlan.uses)
                        used.insert(u.materialId);
                    for (const auto &u : inventory.resourcePlan.uses)
                        demand[u.materialId] += u.quantityMilli;
                    for (const auto &r : inventory.resourcePlan.receipts)
                        incoming[r.materialId] += r.quantityMilli;
                    for (const auto &stock : actualMaterialStock(p, *p.statusAt))
                        if (used.contains(stock.materialId)) {
                            const auto &m =
                                *std::find_if(p.resourcePlan.materials.begin(), p.resourcePlan.materials.end(),
                                              [&](const auto &v) { return v.id == stock.materialId; });
                            out.materials.push_back({p.documentId, m.id, m.name, p.revision, p.materialActuals.size(),
                                                     stock, demand[m.id], incoming[m.id]});
                        }
                }
            }
            if (out.forecast) {
                std::map<std::string, std::string> reports;
                for (std::size_t i = 0; i < p.progress.size(); ++i)
                    reports[p.progress[i].id] = "progress-" + std::to_string(index) + "-" + std::to_string(i);
                for (auto e : p.progress) {
                    e.taskId = taskIds.at({p.documentId, e.taskId});
                    if (!e.previousEntryId.empty())
                        e.previousEntryId = reports.at(e.previousEntryId);
                    e.id = reports.at(e.id);
                    flat.progress.push_back(std::move(e));
                }
                if (flat.remainingWork.size() + p.remainingWork.size() > 10000)
                    throw AppError(ErrorCode::WORKSPACE_LIMIT);
                for (auto v : p.remainingWork) {
                    calculationCheckpoint();
                    remainingDays += v.days.size();
                    if (remainingDays > 100000)
                        throw AppError(ErrorCode::WORKSPACE_LIMIT);
                    v.taskId = taskIds.at({p.documentId, v.taskId});
                    v.resourceId = resourceIds.at({p.documentId, v.resourceId});
                    // Partial replacement of a merged assignment would change its unreported demand.
                    if (assignmentSources.at({v.taskId, v.resourceId}) != 1)
                        invalid(p.name + " · 날짜별 잔여 공수가 있는 동일 작업의 여러 자원을 같은 공유 자원으로 "
                                         "합칠 수 없습니다. 연결을 분리하세요.");
                    flat.remainingWork.push_back(std::move(v));
                }
            }
        }
        // Per-project audit revisions share one ephemeral graph. Keep each correction chain ordered.
        calculationCheckpoint();
        std::stable_sort(flat.materialActuals.begin(), flat.materialActuals.end(),
                         [](const auto &a, const auto &b) { return a.recordedRevision < b.recordedRevision; });
        calculationCheckpoint();
        for (const auto &l : w.links) {
            const Key from{l.predecessorProjectId, l.predecessorTaskId}, to{l.successorProjectId, l.successorTaskId};
            if (!taskIds.contains(from) || !taskIds.contains(to))
                invalid("프로젝트 간 링크의 작업이 없거나 변경되었습니다: " + l.id);
            auto &target = *std::find_if(flat.tasks.begin(), flat.tasks.end(),
                                         [&](const auto &t) { return t.id == taskIds.at(to); });
            target.predecessors.push_back(taskIds.at(from));
            if (l.type != DependencyType::FS || l.lagMinutes)
                target.dependencyRules.push_back({taskIds.at(from), l.type, l.lagMinutes});
        }
        if (flat.tasks.size() > 10000 || flat.assignments.size() > 100000 || flat.calendars.size() > 20000)
            throw AppError(ErrorCode::WORKSPACE_LIMIT);
        auto translate = [&](Diagnostic d, auto &&self) -> Diagnostic {
            for (auto &id : d.taskIds)
                if (reverse.contains(id)) {
                    const auto &r = reverse.at(id);
                    id = r.projectId + " / " + r.taskId;
                }
            for (auto &cause : d.causes)
                cause = self(cause, self);
            return d;
        };
        const auto full = calculatePlan(flat);
        if (full.status != CalculationStatus::Valid) {
            out.status = full.status;
            out.materials.clear();
            for (const auto &d : full.diagnostics)
                out.diagnostics.push_back(translate(d, translate));
            return out;
        }
        if (out.forecast)
            for (auto &t : flat.tasks)
                if (reverse.contains(t.id)) {
                    const auto &ref = reverse.at(t.id);
                    const auto &p = *sources.at(ref.projectId);
                    const auto *fact = progressAsOf(p, ref.taskId);
                    if (!fact || !fact->actualStart || !p.progressPolicy)
                        continue;
                    std::set<std::string> ignored;
                    for (const auto &d : dependencies(t))
                        if ((p.progressPolicy->policy == overrideProgressPolicy && !fact->actualFinish) ||
                            (p.progressPolicy->policy == actualDatesProgressPolicy &&
                             (fact->actualFinish || !dependencyToFinish(d.type))))
                            ignored.insert(d.predecessorId);
                    std::erase_if(t.predecessors, [&](const auto &id) { return ignored.contains(id); });
                    std::erase_if(t.dependencyRules, [&](const auto &d) { return ignored.contains(d.predecessorId); });
                    if (!ignored.empty())
                        out.diagnostics.push_back({"INTEGRATED_POLICY",
                                                   {p.name + " · 실제 끝점의 관계 " + std::to_string(ignored.size()) +
                                                    "개는 채택 정책에 따라 이력으로 보존"},
                                                   {ref.taskId},
                                                   "INTEGRATED_POLICY"});
                }
        if (flat.resourcePlan.autoDelayRemainingForReceipts)
            out.policy += "/material-remaining-v1";
        else if (flat.resourcePlan.autoDelayForReceipts)
            out.policy += "/material-arrivals-v1";
        const bool unprojectedMaterial =
            out.leveled && std::any_of(w.projects.begin(), w.projects.end(), [&](const auto &ref) {
                const auto &p = *sources.at(ref.documentId);
                return hasMaterialInventory(p) && !(out.forecast ? p.resourcePlan.autoDelayRemainingForReceipts
                                                                 : p.resourcePlan.autoDelayForReceipts);
            });
        const auto requestedFloatVersion = levelingFloatVersion();
        LevelingFloatScope floatScope(unprojectedMaterial ? 0 : requestedFloatVersion);
        ScheduleResult schedule;
        if (out.leveled) {
            auto result = levelResources(flat, w.integrated.priority, w.integrated.horizonWorkDays);
            schedule = std::move(result.leveled);
            for (auto d : result.diagnostics)
                out.diagnostics.push_back(translate(std::move(d), translate));
        } else
            schedule = calculate(flat);
        out.status = schedule.status;
        if (unprojectedMaterial && !out.forecast && requestedFloatVersion >= 9 &&
            schedule.status == CalculationStatus::Valid) {
            // Manual material plans participate in float without enabling automatic arrival delay.
            // The forward calculation has already finished; only its private certificate input expands.
            try {
                auto complete = flat;
                for (const auto &ref : w.projects) {
                    calculationCheckpoint();
                    const auto &p = *sources.at(ref.documentId);
                    if (!hasMaterialInventory(p) || p.resourcePlan.autoDelayForReceipts)
                        continue;
                    if (complete.resourcePlan.materials.size() + p.resourcePlan.materials.size() > 10000 ||
                        complete.resourcePlan.uses.size() + p.resourcePlan.uses.size() > 100000 ||
                        complete.resourcePlan.receipts.size() + p.resourcePlan.receipts.size() > 100000)
                        throw AppError(ErrorCode::WORKSPACE_LIMIT);
                    std::map<std::string, std::string> materials;
                    for (auto m : p.resourcePlan.materials) {
                        calculationCheckpoint();
                        const auto id = m.id;
                        m.id = "material-" + std::to_string(complete.resourcePlan.materials.size());
                        materials[id] = m.id;
                        m.stockAt = m.stockAt.value_or(p.startDate + " 09:00");
                        complete.resourcePlan.materials.push_back(std::move(m));
                    }
                    for (auto u : p.resourcePlan.uses) {
                        calculationCheckpoint();
                        u.id = "material-use-" + std::to_string(complete.resourcePlan.uses.size());
                        u.taskId = taskIds.at({p.documentId, u.taskId});
                        u.materialId = materials.at(u.materialId);
                        complete.resourcePlan.uses.push_back(std::move(u));
                    }
                    for (auto r : p.resourcePlan.receipts) {
                        calculationCheckpoint();
                        r.id = "material-receipt-" + std::to_string(complete.resourcePlan.receipts.size());
                        r.materialId = materials.at(r.materialId);
                        complete.resourcePlan.receipts.push_back(std::move(r));
                    }
                }
                LevelingFloatScope materialScope(requestedFloatVersion);
                calculateLevelingFloat(complete, schedule);
            } catch (const CalculationCancelled &) {
                throw;
            } catch (const std::exception &e) {
                schedule.levelingFloatReason = "통합 자재 여유 입력 투영: " + std::string(e.what());
                schedule.diagnostics.push_back(
                    {"LEVELING_FLOAT_UNAVAILABLE", {schedule.levelingFloatReason}, {}, "LEVELING_FLOAT_UNAVAILABLE"});
            }
        } else if (unprojectedMaterial) {
            schedule.levelingFloatReason = "자동 대기 옵션이 없는 문서의 자재 입력은 통합 여유에 투영되지 않았습니다.";
            schedule.diagnostics.push_back(
                {"LEVELING_FLOAT_UNAVAILABLE", {schedule.levelingFloatReason}, {}, "LEVELING_FLOAT_UNAVAILABLE"});
        }
        out.finish = schedule.projectFinish;
        for (const auto &d : schedule.diagnostics) {
            // Shared output publishes certified scalars; sparse witness dates use private graph IDs.
            if (schedule.calculationPolicy.ends_with("/resource-material-float-v1") &&
                (d.code == "LEVELING_REPARTITION_WITNESS" || d.code == "LEVELING_REPARTITION_WITNESS_PREVIEW"))
                continue;
            out.diagnostics.push_back(translate(d, translate));
        }
        if (schedule.levelingFloatCalculated && schedule.calculationPolicy.ends_with("/resource-material-float-v1"))
            out.diagnostics.push_back(
                {"INTEGRATED_POLICY",
                 {"작업별 총여유는 각각 검산한 최댓값입니다. 여러 작업의 최후 날짜를 한 배치로 합쳐 적용하지 마세요."},
                 {},
                 "INTEGRATED_POLICY"});
        if (out.status != CalculationStatus::Valid) {
            out.materials.clear();
            out.finish.reset();
            return out;
        }
        std::map<std::string, ScheduleResult> projectedSchedules;
        for (const auto &ref : w.projects) {
            auto r = schedule;
            r.documentId = ref.documentId;
            r.inputRevision = sources.at(ref.documentId)->revision;
            r.profile = effortProfile;
            r.tasks.clear();
            r.plannedTasks.clear();
            r.projectFinish.reset();
            WorkCalendar clock(projected.at(ref.documentId));
            for (const auto &task : schedule.tasks)
                if (reverse.contains(task.taskId) && reverse.at(task.taskId).projectId == ref.documentId) {
                    const auto &key = reverse.at(task.taskId);
                    auto local = task;
                    local.taskId = key.taskId;
                    local.drivingPredecessors.clear();
                    local.earlyStartMinutes = clock.offset(task.start);
                    local.earlyFinishMinutes = clock.offset(task.finish);
                    r.tasks.push_back(local);
                    if (!r.projectFinish || *r.projectFinish < task.finish)
                        r.projectFinish = task.finish;
                    IntegratedTask row;
                    row.ref = key;
                    row.projectName = ref.name;
                    row.revision = r.inputRevision;
                    row.schedule = local;
                    row.floatAvailable = hasScheduleFloat(schedule, task);
                    const auto &p = *sources.at(ref.documentId);
                    row.name = std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &t) {
                                   return t.id == key.taskId;
                               })->name;
                    for (const auto &d : task.drivingPredecessors)
                        if (reverse.contains(d))
                            row.driving.push_back(reverse.at(d));
                    for (const auto &old : original.at(ref.documentId).tasks)
                        if (old.taskId == key.taskId) {
                            row.originalStart = old.start;
                            row.originalFinish = old.finish;
                        }
                    out.tasks.push_back(std::move(row));
                }
            projectedSchedules.emplace(ref.documentId, std::move(r));
        }
        if (out.leveled)
            for (const auto &ref : w.projects) {
                const auto issues =
                    materialInventoryDiagnostics(projected.at(ref.documentId), projectedSchedules.at(ref.documentId));
                if (!issues.empty()) {
                    out.status = CalculationStatus::Conflict;
                    out.tasks.clear();
                    out.materials.clear();
                    out.finish.reset();
                    for (auto issue : issues) {
                        issue.projectId = ref.documentId;
                        out.diagnostics.push_back(std::move(issue));
                    }
                    return out;
                }
            }
        if (!w.bindings.empty()) {
            std::vector<std::shared_ptr<const Project>> inputs;
            for (auto &[id, p] : projected) {
                (void)id;
                inputs.push_back(std::make_shared<const Project>(std::move(p)));
            }
            out.shared = calculateSharedResources(w, inputs, out.forecast, control, &projectedSchedules);
            if (out.leveled && out.shared.status != CalculationStatus::Valid) {
                out.status = CalculationStatus::Conflict;
                out.tasks.clear();
                out.materials.clear();
                out.finish.reset();
                out.diagnostics.insert(out.diagnostics.end(), out.shared.diagnostics.begin(),
                                       out.shared.diagnostics.end());
            }
        }
        if (out.status == CalculationStatus::Valid && schedule.levelingFloatCalculated)
            out.floatPolicy = schedule.calculationPolicy;
        return out;
    } catch (const AppError &e) {
        out.status = CalculationStatus::Conflict;
        out.tasks.clear();
        out.materials.clear();
        out.finish.reset();
        out.diagnostics.push_back(e.diagnostic());
        return out;
    }
}
} // namespace jobs
