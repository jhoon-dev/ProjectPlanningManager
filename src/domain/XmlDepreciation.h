#pragma once
#include <cstdint>
#include <string>
namespace ppm {
// Inputs and result use the native fixed-six scale; no intermediate rounding.
std::int64_t xmlDepreciation(const std::string &function, std::int64_t cost, std::int64_t salvage, std::int64_t life,
                             std::int64_t period = 1000000);
} // namespace ppm
