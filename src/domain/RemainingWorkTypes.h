#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace jobs {
struct DatedWorkValue {
    std::string date;
    std::int64_t workUnitMinutes = 0;
    bool operator==(const DatedWorkValue &) const = default;
};
struct RemainingWorkPlan {
    std::string taskId, resourceId, statusAt;
    std::vector<DatedWorkValue> days;
    bool operator==(const RemainingWorkPlan &) const = default;
};
} // namespace jobs
