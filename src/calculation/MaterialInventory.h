#pragma once
#include "calculation/Scheduler.h"
#include <map>
#include <set>
namespace jobs {
struct MaterialInventoryPoint {
    std::string materialId, name, unit, at, stockAt, issue;
    std::optional<std::int64_t> opening, received, consumed, remaining, available, shortage;
};
class MaterialInventoryQuery {
    struct Demand {
        std::int64_t quantity = 0, duration = 0, openingConsumed = 0;
        std::vector<std::pair<std::int64_t, std::int64_t>> spans;
        std::optional<std::int64_t> instant;
        std::int64_t cumulative(std::int64_t) const;
    };
    struct Entry {
        Material material;
        std::int64_t base = 0;
        std::map<std::int64_t, std::int64_t> receipts;
        std::vector<Demand> demands;
        std::set<std::int64_t> checkpoints;
    };
    std::map<std::string, Entry> entries_;
    std::shared_ptr<CalculationControl> control_;
    std::uint64_t shortageWork_ = 0;
    MaterialInventoryQuery(const Project &, const ScheduleResult &, const std::shared_ptr<CalculationControl> &,
                           const std::set<std::string> *scheduled, bool validated, bool forecast = false);
    friend class MaterialReservations;
    friend class MaterialFloatCheck;

public:
    MaterialInventoryQuery(const Project &, const ScheduleResult &, const std::shared_ptr<CalculationControl> & = {});
    MaterialInventoryPoint at(const std::string &materialId, const std::string &at) const;
    std::vector<MaterialInventoryPoint> allAt(const std::string &) const;
    std::optional<std::string> firstShortage(const std::string &) const;
};
std::vector<Diagnostic> materialInventoryDiagnostics(const Project &, const ScheduleResult &);
} // namespace jobs
