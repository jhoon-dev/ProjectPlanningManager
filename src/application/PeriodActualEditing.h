#pragma once
#include "domain/Project.h"
#include <functional>
namespace ppm {
// Appends to a private proposal; callers validate and publish once.
void appendPeriodActuals(Project &, PeriodActualSubmission, const std::function<std::string()> &newId,
                         bool allowACOnly = false);
} // namespace ppm
