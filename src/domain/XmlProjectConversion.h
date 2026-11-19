#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace ppm {
// ISO civil instants and fixed Project display profiles, independent of OS settings.
std::optional<std::string> xmlProjectDateValue(const std::string &);
std::string xmlProjectDateFormat(const std::string &normalized, int format = 255);
std::int64_t xmlProjectDurationValue(const std::string &);
std::string xmlProjectDurationFormat(std::int64_t scaledMinutes, int format = 3);
} // namespace ppm
