#pragma once
#include "domain/Project.h"
namespace ppm {
void validateActivities(const PlanInput &);
void validateStepReports(const Project &);
int weightedStepProgress(const std::vector<StepObservation> &);
const StepReport *stepsAsOf(const Project &, const std::string &taskId, const std::string &statusAt);
std::optional<int> stepsPhysicalAsOf(const Project &, const std::string &taskId, const std::string &statusAt);
bool hasActivityExtensions(const PlanInput &);
} // namespace ppm
