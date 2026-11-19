#include "calculation/CapacityTimeline.h"
#include "domain/ResourceAvailability.h"
#include "domain/CalculationControl.h"
#include <algorithm>
namespace ppm {
CapacityTimeline::CapacityTimeline(const Resource &r, const WorkCalendar &clock) {
    const auto first = clock.boundary(0, false), last = clock.boundary(clock.maxOffset() - 1, false);
    const auto periods = resourceAvailabilityIntervals(r);
    const auto valueAt = [&](const std::string &at) {
        const auto it = std::upper_bound(periods.begin(), periods.end(), at, [](const auto &stamp, const auto &period) {
            return period.from && stamp < *period.from;
        });
        return std::prev(it)->units;
    };
    values_[0] = valueAt(first);
    for (const auto &period : periods) {
        calculationCheckpoint();
        if (!period.from)
            continue;
        const auto &stamp = *period.from;
        if (stamp <= first || stamp > last)
            continue;
        const auto at = clock.startAtLeast(stamp);
        if (at < clock.maxOffset())
            values_[at] = valueAt(clock.boundary(at, false));
    }
}
std::optional<int> CapacityTimeline::at(std::int64_t minute) const {
    auto it = values_.upper_bound(minute);
    return it == values_.begin() ? std::optional<int>{} : std::prev(it)->second;
}
std::int64_t CapacityTimeline::next(std::int64_t at, std::int64_t finish) const {
    auto it = values_.upper_bound(at);
    return it == values_.end() ? finish : std::min(finish, it->first);
}
} // namespace ppm
