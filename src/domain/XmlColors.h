#pragma once
#include <cstdint>
namespace jobs {
// VBA color integers store red in the least-significant byte.
std::int64_t xmlRgbColor(std::int64_t red, std::int64_t green, std::int64_t blue);
std::int64_t xmlQbColor(std::int64_t color);
} // namespace jobs
