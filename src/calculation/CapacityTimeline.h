#pragma once
#include "calculation/WorkCalendar.h"
#include <map>
namespace ppm {
class CapacityTimeline {
    std::map<std::int64_t, std::optional<int>> values_;

public:
    CapacityTimeline(const Resource &, const WorkCalendar &);
    std::optional<int> at(std::int64_t) const;
    std::int64_t next(std::int64_t, std::int64_t finish) const;
};
} // namespace ppm
