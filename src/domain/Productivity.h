#pragma once
#include "domain/Project.h"
namespace jobs {
void validateProductivityRate(const ProductivityRate &);
std::int64_t productivityWork(std::int64_t quantityMilli, const ProductivityRate &);
void validateResourcePlanning(const PlanInput &);
bool hasResourcePlanning(const PlanInput &);
} // namespace jobs
