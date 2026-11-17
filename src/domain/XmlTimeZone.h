#pragma once
#include <string>
namespace jobs {
struct XmlZonedTime {
    std::string civil;
    int offsetSeconds = 0;
};
// Named zones use explicit 2026f: identifiers and immutable UTC coverage [1970,2101).
// Fixed UTC/±HH:MM zones use the existing native date range; never use the host timezone.
XmlZonedTime xmlTimeZoneAtUtc(const std::string &utc, const std::string &zone);
std::string xmlTimeZoneConvert(const std::string &civil, const std::string &from, const std::string &to,
                               const std::string &overlap = "reject");
std::string xmlTimeZoneText(const std::string &civil, const std::string &from, const std::string &to,
                            const std::string &overlap = "reject");
int xmlTimeZoneOffset(const std::string &civil, const std::string &zone, const std::string &overlap = "reject");
std::string xmlParseUtc(const std::string &isoOffset);
} // namespace jobs
