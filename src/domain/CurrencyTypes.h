#pragma once
#include <cstdint>
#include <string>
namespace jobs {
struct CurrencyRate {
    // Major units: one source unit = numerator / denominator target units.
    std::string fromCurrency, targetCurrency, effectiveDate, reference;
    std::string rounding = "half-away-from-zero-v1";
    std::int64_t numerator = 1, denominator = 1;
    bool operator==(const CurrencyRate &) const = default;
};
} // namespace jobs
