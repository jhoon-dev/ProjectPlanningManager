#pragma once
#include "domain/Classification.h"
#include "domain/XmlRollUp.h"
#include "domain/CalculationControl.h"
namespace ppm {
struct ResourceGroupValue {
    std::string token, label;
};
struct ResourceViewRow {
    std::size_t resourceIndex = 0;
    std::vector<ResourceGroupValue> groups;
};
std::vector<RollUpGroup> resourceRollUpGroups(const std::vector<ResourceViewRow> &);
std::vector<std::string> resourceViewKeys(const PlanInput &);
std::string resourceViewName(const PlanInput &, const std::string &);
std::optional<std::string> resourceViewValue(const PlanInput &, const Resource &, const std::string &);
std::string resourceViewText(const PlanInput &, const std::string &, const std::string &);
void validateResourceView(const PlanInput &, const ResourceViewSettings &);
void validateSavedResourceViews(const Project &);
std::vector<ResourceViewRow> projectResources(const Project &, const ResourceViewSettings &,
                                              std::shared_ptr<CalculationControl> = {});
} // namespace ppm
