#pragma once
#include "domain/Project.h"
namespace jobs {
inline constexpr const char *materialArrivalPolicy = "serial-material-arrivals-v1";
inline constexpr const char *materialFloatEngine = "jobs-adopted-material-leveling-1.0";
void validateResourcePlan(const PlanInput &);
bool hasResourcePlan(const PlanInput &);
bool hasMaterialInventory(const PlanInput &);
bool advancedPriority(const std::string &);
std::string formatMaterialQuantity(std::int64_t milli);
std::size_t resourcePlanBytes(const ResourcePlan &);
std::optional<std::int64_t> materialCost(const Material &, std::int64_t quantityMilli);
struct PlannedCharge {
    std::optional<std::int64_t> amount;
    std::string accrual;
};
std::vector<PlannedCharge> plannedCharges(const PlanInput &, const std::string &taskId);
std::vector<Diagnostic> materialReadiness(const PlanInput &);
} // namespace jobs
