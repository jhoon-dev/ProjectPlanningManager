#pragma once
#include <cstdint>
#include <string>
namespace ppm {
enum class XmlNumericDisplayKind { Number, Percent, Currency };
// Fixed en-US decimal display; input is the native fixed-six numeric value.
std::string xmlNumericDisplay(XmlNumericDisplayKind, std::int64_t scaled, int digits = -1, int leading = -2,
                              int parentheses = -2, int grouping = -2);
} // namespace ppm
