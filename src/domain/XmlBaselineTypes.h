#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace ppm {
struct XmlBaselineTime {
    int type = 0;
    std::string uid;
    std::optional<std::string> start, finish, value;
    std::optional<int> unit;
    bool operator==(const XmlBaselineTime &) const = default;
};
struct XmlBaselineRow {
    int number = 0;
    std::string kind, id, resourceId, xmlUid;
    std::optional<std::string> start, finish;
    std::optional<std::int64_t> duration, work, cost; // fixed-six minutes; integer minor currency
    std::map<std::string, std::string> flags;
    std::map<std::string, std::int64_t> money;
    std::vector<XmlBaselineTime> timephased;
    bool timephasedOutside = false; // Standard parent-level block; resource baselines require this location.
    bool operator==(const XmlBaselineRow &) const = default;
};
struct XmlBaselineOrigin {
    std::string sourceName, sourceSha256, reporter, reason, recordedAt;
    // Frozen project calendar at explicit adoption, independent of subsequent plan edits.
    std::vector<int> weekdays;
    std::vector<std::pair<int, int>> periods;
    std::vector<std::string> holidays;
    bool operator==(const XmlBaselineOrigin &) const = default;
};
struct XmlBaselineSet : XmlBaselineOrigin {
    std::vector<XmlBaselineRow> rows;
    // Empty for schema75 imports. Merged sets retain an origin/calendar for every adopted number.
    std::map<int, XmlBaselineOrigin> origins;
    bool operator==(const XmlBaselineSet &) const = default;
};
struct XmlBaselineLink {
    std::string kind, sourceId, targetId;
    bool exclude = false;
    bool operator==(const XmlBaselineLink &) const = default;
};
struct XmlBaselineMergeSubmission {
    XmlBaselineSet incoming;
    std::vector<XmlBaselineLink> links;
    std::string currency;
    bool replaceExisting = false, confirmed = false;
    bool operator==(const XmlBaselineMergeSubmission &) const = default;
};
struct XmlBaselineMerge : XmlBaselineMergeSubmission {
    std::string id, sourceDocumentId;
    std::uint64_t recordedRevision = 0;
    std::optional<XmlBaselineSet> before;
    XmlBaselineSet after;
    bool operator==(const XmlBaselineMerge &) const = default;
};
} // namespace ppm
