#pragma once
#include "domain/Project.h"
#include <functional>
#include <map>
namespace ppm {
inline constexpr const char *remainingMaterialPolicy = "serial-material-remaining-v1";
void validateMaterialActuals(const Project &);
void appendMaterialActuals(Project &, MaterialActualSubmission, const std::function<std::string()> &);
std::map<std::string, const MaterialActualEntry *> materialActualsAsOf(const Project &, const std::string &);
struct ActualMaterialStock {
    std::string materialId, unit, at;
    std::optional<std::int64_t> opening, remaining;
    std::int64_t received = 0, consumed = 0;
    bool confirmed = false;
};
std::vector<ActualMaterialStock> actualMaterialStock(const Project &, const std::string &);
// An ephemeral inventory input. Actual movements never become new planned consumption/cost.
Project remainingMaterialInventoryInput(const Project &);
} // namespace ppm
