#include "calculation/Resources.h"
#include "domain/ResourcePlan.h"
#include "calculation/LevelingFloat.h"
#include <algorithm>
namespace jobs {
namespace {
std::string selectedEngine(const PlanInput &p) {
    if (levelingFloatVersion() >= 9 && hasMaterialInventory(p))
        return materialFloatEngine;
    return levelingFloatVersion() == 0   ? "jobs-adopted-leveling-1.0"
           : levelingFloatVersion() == 1 ? "jobs-adopted-leveling-1.1"
           : levelingFloatVersion() == 2 ? "jobs-adopted-leveling-1.2"
           : levelingFloatVersion() == 3 ? "jobs-adopted-leveling-1.3"
           : levelingFloatVersion() == 4 ? "jobs-adopted-leveling-1.4"
           : levelingFloatVersion() == 5 ? "jobs-adopted-leveling-1.5"
           : levelingFloatVersion() == 6 ? "jobs-adopted-leveling-1.6"
           : levelingFloatVersion() == 7 ? "jobs-adopted-leveling-1.7"
                                         : "jobs-adopted-leveling-1.8";
}
} // namespace
ScheduleResult selectedLevelingSchedule(const Project &p, bool remaining) {
    EvaluationScope evaluation;
    ScheduleResult out;
    out.documentId = p.documentId;
    out.inputRevision = p.revision;
    out.profile = p.calculationProfile;
    out.engineVersion = remaining ? selectedEngine(p) : engineFor(p);
    out.isForecast = remaining;
    out.statusAt = remaining ? p.statusAt : std::nullopt;
    out.resourceLeveled = true;
    try {
        validate(p);
        const auto &selected = remaining ? p.remainingLeveling : p.planLeveling;
        if (!selected)
            throw AppError(ErrorCode::LEVEL_OPTIONS_INVALID);
        auto logical = p;
        logical.levelingDecisions.clear(); // Private calculation graph, not a persisted document transition.
        logical.planLeveling.reset();
        logical.remainingLeveling.reset();
        if (!remaining) {
            logical.statusAt.reset();
            logical.progress.clear();
        }
        auto r = levelResources(logical, selected->priority, selected->horizonWorkDays, selected->policy);
        out = std::move(r.leveled);
        out.resourceLeveled = true;
        out.forecastFloatCalculated = false;
        out.engineVersion = levelingFloatVersion() >= 9 && hasMaterialInventory(p) ? materialFloatEngine
                            : hasWorkAssignmentPeriods(p)                          ? "jobs-adopted-work-periods-1.0"
                            : hasAssignmentPeriods(p)                              ? "jobs-adopted-periods-1.0"
                                                                                   : selectedEngine(p);
        out.calculationPolicy = "adopted-leveling-v1/" + out.calculationPolicy;
        if (out.status == CalculationStatus::Valid && r.after.status != CalculationStatus::Valid) {
            out.status = r.after.status;
            out.tasks.clear();
            out.projectFinish.reset();
        }
        out.diagnostics.insert(out.diagnostics.end(), r.diagnostics.begin(), r.diagnostics.end());
        std::erase_if(out.diagnostics,
                      [](const auto &d) { return d.code == "RESOURCE_NOT_CHECKED" || d.code == "PDM_POLICY"; });
        out.diagnostics.push_back({"LEVELING_SELECTED", {}, {}, "LEVELING_SELECTED"});
        if (!out.levelingFloatCalculated)
            for (auto &row : out.tasks) {
                row.totalFloatMinutes = row.freeFloatMinutes = 0;
                row.critical = false;
                row.drivingPredecessors.clear();
                row.lateStartMinutes = row.earlyStartMinutes;
                row.lateFinishMinutes = row.earlyFinishMinutes;
            }
    } catch (const std::exception &e) {
        out.status = CalculationStatus::Conflict;
        out.tasks.clear();
        out.projectFinish.reset();
        out.diagnostics.push_back(diagnosticFromException(e));
    }
    return out;
}
} // namespace jobs
