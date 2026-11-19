#pragma once
#include <cstdint>
#include <string>
namespace ppm {
// Fixed-six input/output; transcendental results use the platform long-double math library.
std::int64_t xmlMathNumber(const std::string &function, std::int64_t scaled);
} // namespace ppm
