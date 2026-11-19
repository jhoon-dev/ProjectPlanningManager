#pragma once
#include <cstdint>
#include <string>
namespace ppm {
inline constexpr auto retainedProgressPolicy = "native-retained-remaining-v1";
inline constexpr auto overrideProgressPolicy = "native-progress-override-v1";
inline constexpr auto actualDatesProgressPolicy = "native-actual-dates-v1";
struct ProgressPolicySelection {
    std::string policy, adoptedBy, adoptedAt, reason, sourceDocumentId;
    std::uint64_t sourceRevision = 0;
    bool operator==(const ProgressPolicySelection &) const = default;
};
void validateProgressPolicySelection(const ProgressPolicySelection &);
} // namespace ppm
