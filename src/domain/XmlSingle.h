#pragma once
#include <cstdint>
namespace ppm {
// Exact round-to-nearest-even binary32, then native fixed-six half-away rounding.
// Does not depend on the process floating-point rounding mode.
std::int64_t xmlSingleNumber(std::int64_t scaled);
} // namespace ppm
