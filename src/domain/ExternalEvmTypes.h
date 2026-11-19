#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace ppm {
struct ExternalEvmRow {
    std::size_t periodIndex = 0;
    std::optional<std::int64_t> openingEv, openingPv;
    bool operator==(const ExternalEvmRow &) const = default;
};
struct ExternalEvmSubmission {
    std::string baselineId, financialRevisionId, sourceSha256, sourceProjectionSha256;
    std::vector<std::string> taskIds;
    std::vector<ExternalEvmRow> rows;
    bool useEv = true, usePv = true, withdrawn = false, confirmed = false;
    std::string reporter, reason, recordedAt;
    bool operator==(const ExternalEvmSubmission &) const = default;
};
struct ExternalEvmAdoption : ExternalEvmSubmission {
    std::string id, currency, sourceStatusAt, sourceDocumentId;
    std::optional<std::string> nativeStatusAt;
    std::uint64_t recordedRevision = 0;
    bool operator==(const ExternalEvmAdoption &) const = default;
};
} // namespace ppm
