#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace jobs {
enum class XmlFormatKind { Null, Number, Text, Boolean, Date };
struct XmlFormatInput {
    XmlFormatKind kind = XmlFormatKind::Null;
    std::int64_t scaled = 0;
    std::string text;
};
// Fixed Gregorian locale profiles; never consult the operating system.
std::optional<std::string> xmlFormat(const XmlFormatInput &, const std::string &mask = "", int lcid = 1033,
                                     int firstDay = 1, int firstWeek = 1);
std::string xmlStrConv(const std::string &, int conversion, int lcid = 1033);
} // namespace jobs
