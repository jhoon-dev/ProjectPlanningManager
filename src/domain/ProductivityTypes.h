#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace jobs {
struct ProductivityRate {
    // Thousandths of physical output per resource-hour, independent of prices/capacity.
    std::string unit, effectiveDate, reference, reporter;
    std::int64_t quantityMilliPerHour = 1000;
    bool operator==(const ProductivityRate &) const = default;
};
struct ProductivityEstimate {
    ProductivityRate rate;
    std::int64_t quantityMilli = 0, workUnitMinutes = 0;
    std::string reason, recordedAt;
    bool confirmed = false;
    std::string application = "duration-rates-v1";
    std::string reporter;
    bool operator==(const ProductivityEstimate &) const = default;
};
} // namespace jobs
