#pragma once
#include "domain/Project.h"
namespace jobs {
std::vector<XerScheduleLink> suggestXerScheduleLinks(const PlanInput &, const PlanInput &);
PlanInput adoptXerSchedule(const PlanInput &, const XerScheduleSubmission &, const std::string &adoptionId);
void validateXerScheduleAdoptions(const Project &);
} // namespace jobs
