#pragma once
#include <cstdint>
#include <string>
namespace ppm {
// BMP character positions, binary comparison or explicit ASCII case folding.
std::string xmlReplace(const std::string &, const std::string &find, const std::string &replacement,
                       std::int64_t start = 1, std::int64_t count = -1, int compare = 0);
std::int64_t xmlInStrRev(const std::string &, const std::string &find, std::int64_t start = -1, int compare = 0);
std::string xmlStrReverse(const std::string &);
} // namespace ppm
