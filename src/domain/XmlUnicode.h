#pragma once
#include <cstdint>
#include <string>
namespace jobs {
// BMP characters that can be represented in native text and XML 1.0.
std::int64_t xmlAscWide(const std::string &);
std::string xmlChrWide(std::int64_t);
} // namespace jobs
