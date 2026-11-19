#pragma once
#include "domain/Project.h"
namespace ppm {
inline constexpr const char *datedRemainingPolicy = "serial-dated-remaining-v1";
inline constexpr const char *datedEffortPolicy = "serial-dated-effort-v1";
bool hasDatedEffort(const Project &);
std::string datedRemainingEngine(const Project &);
void validateRemainingWork(const Project &);
std::string levelingPolicyFor(const Project &);
bool supportsLevelingPolicy(const Project &, const std::string &);
} // namespace ppm
