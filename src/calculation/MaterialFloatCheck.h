#pragma once
#include "calculation/MaterialInventory.h"
namespace ppm {
// Exact inventory certification of complete or partial nonnegative-consumption plans.
class MaterialFloatCheck {
    const Project &source_;
    std::optional<Project> inventory_;
    ScheduleResult header_;
    std::uint64_t calls_ = 0, work_ = 0;
    const Project &input() const {
        return inventory_ ? *inventory_ : source_;
    }

public:
    MaterialFloatCheck(const Project &, const ScheduleResult &);
    bool fits(const std::vector<TaskSchedule> &);
    const std::vector<MaterialUse> &uses() const {
        return input().resourcePlan.uses;
    }
    std::uint64_t calls() const {
        return calls_;
    }
};
} // namespace ppm
