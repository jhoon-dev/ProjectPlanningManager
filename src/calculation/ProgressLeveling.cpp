#include "calculation/ProgressPolicies.h"
#include "calculation/Resources.h"
#include "calculation/LevelingFloat.h"
#include <algorithm>
namespace ppm {
LevelingResult levelProgressPolicy(const Project &p, const std::string &priority, int horizon) {
    EvaluationScope evaluation;
    LevelingResult out;
    out.priority = priority;
    out.horizonWorkDays = horizon;
    // Validate the complete original graph before masking any incoming relationships.
    const auto plan = calculatePlan(p);
    out.original = plan.status == CalculationStatus::Valid ? calculate(p) : plan;
    out.before = resourceLoad(p, out.original);
    out.leveled = out.original;
    if (out.original.status != CalculationStatus::Valid)
        return out;
    auto active = progressPolicyGraph(p);
    LevelingFloatScope withoutDerivedMetrics(false);
    auto leveled = levelResources(active, priority, horizon);
    out.leveled = std::move(leveled.leveled);
    out.diagnostics = std::move(leveled.diagnostics);
    if (out.leveled.isForecast)
        out.leveled.plannedTasks = plan.tasks;
    const auto obsolete = [](const auto &d) {
        return d.code == "RESOURCE_NOT_CHECKED" || d.code == "FORECAST_POLICY" || d.code == "OUT_OF_SEQUENCE" ||
               d.code == "FORECAST_FLOAT_POLICY" || d.code == "PROGRESS_OVERRIDE_PREVIEW" ||
               d.code == "PROGRESS_POLICY_SELECTED";
    };
    std::erase_if(out.leveled.diagnostics, obsolete);
    std::erase_if(out.diagnostics, obsolete);
    for (const auto &d : out.original.diagnostics)
        if (d.code == "PROGRESS_POLICY_SELECTED" || d.code == "OUT_OF_SEQUENCE")
            out.leveled.diagnostics.push_back(d);
    out.leveled.calculationPolicy = "progress-leveling-v1/" + p.progressPolicy->policy +
                                    (p.statusAt ? "/remaining/" : "/dormant/") + out.leveled.calculationPolicy;
    out.leveled.resourceLeveled = true;
    out.leveled.forecastFloatCalculated = false;
    for (auto &row : out.leveled.tasks) {
        calculationCheckpoint();
        row.totalFloatMinutes = row.freeFloatMinutes = 0;
        row.critical = false;
        row.drivingPredecessors.clear();
        row.lateStartMinutes = row.earlyStartMinutes;
        row.lateFinishMinutes = row.earlyFinishMinutes;
    }
    out.leveled.diagnostics.push_back(
        {"PROGRESS_LEVELING_COMBINED", {p.progressPolicy->policy}, {}, "PROGRESS_LEVELING_COMBINED"});
    out.after = resourceLoad(p, out.leveled);
    return out;
}
} // namespace ppm
