#pragma once
#include "calculation/MaterialInventory.h"
namespace ppm {
std::vector<Diagnostic> materialAutoReadiness(const Project &);
// Serial reservations protect all previously accepted consumption, including future consumption.
class MaterialReservations {
    const Project &source_;
    std::optional<Project> inventory_;
    bool forecast_ = false;
    const Project &project() const {
        return inventory_ ? *inventory_ : source_;
    }
    bool active_ = false;
    ScheduleResult accepted_;
    std::map<std::string, std::vector<const MaterialUse *>> uses_;
    std::map<std::string, std::vector<std::string>> supply_;
    std::set<std::string> waited_;
    std::uint64_t work_ = 0, trials_ = 0;

public:
    MaterialReservations(const Project &, bool enabled, bool forecast);
    bool fits(const TaskSchedule &);
    std::optional<std::string> nextStart(const TaskSchedule &) const;
    void commit(const TaskSchedule &);
    std::optional<Diagnostic> delay(const TaskSchedule &) const;
};
} // namespace ppm
