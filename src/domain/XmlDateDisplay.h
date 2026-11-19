#pragma once
#include <string>
namespace ppm {
// Fixed Gregorian/en-US display. Fractional seconds are omitted, as in native Format.
std::string xmlFormatDateTime(const std::string &, int namedFormat = 0);
} // namespace ppm
