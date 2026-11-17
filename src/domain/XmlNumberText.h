#pragma once
#include <cstdint>
#include <string>
namespace jobs {
// Fixed-six numbers; locale-free text and signed 32-bit radix conversions.
std::string xmlStrNumber(std::int64_t scaled);
std::string xmlRadixNumber(std::int64_t rounded, int radix);
std::int64_t xmlValNumber(const std::string &);
} // namespace jobs
