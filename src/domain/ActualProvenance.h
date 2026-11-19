#pragma once
#include "domain/PeriodActualTypes.h"
#include <string_view>
#include <map>
namespace ppm {
struct Project;
inline constexpr std::string_view xmlProrataPrefix = "[XML-PRORATA sha256=";
inline bool proportionalActual(const PeriodActualEntry &e) {
    return e.reason.starts_with(xmlProrataPrefix);
}
inline bool proportionalWork(const PeriodActualEntry &e) {
    const auto end = e.reason.find(" parts=");
    return proportionalActual(e) && end != std::string::npos && e.reason.find(" work=estimated ") < end;
}
inline bool proportionalCost(const PeriodActualEntry &e) {
    const auto end = e.reason.find(" parts=");
    return proportionalActual(e) && end != std::string::npos && e.reason.find(" cost=estimated ") < end;
}
struct ActualDerivation {
    std::string workSourceId, costSourceId;
    bool any() const {
        return !workSourceId.empty() || !costSourceId.empty();
    }
};
// Unchanged amounts retain their allocation origin through later corrections.
// Prior observations are selected as of each entry's status, skipping future facts.
std::map<std::string, ActualDerivation> actualDerivations(const Project &);
} // namespace ppm
