#include "domain/XmlDepreciation.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <array>
namespace ppm {
namespace {
constexpr std::int64_t scale = 1000000, limit = 9000000000000000000LL;
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"SLN/SYD: " + message});
}
// At most 57 decimal digits are needed for the exact cross products below.
// Eight base-1e9 limbs keep the calculation portable, including MSVC/macOS.
struct Wide {
    static constexpr std::uint64_t base = 1000000000;
    std::array<std::uint32_t, 8> digits{};
    explicit Wide(std::uint64_t n) {
        for (auto &digit : digits) {
            digit = std::uint32_t(n % base);
            n /= base;
        }
    }
    Wide times(std::uint64_t n) const {
        const Wide other(n);
        Wide out(0);
        for (std::size_t i = 0; i < digits.size(); ++i) {
            std::uint64_t carry = 0;
            for (std::size_t j = 0; j + i < digits.size(); ++j) {
                const auto value = std::uint64_t(digits[i]) * other.digits[j] + out.digits[i + j] + carry;
                out.digits[i + j] = std::uint32_t(value % base);
                carry = value / base;
            }
            if (carry)
                invalid("내부 정수 범위 초과");
        }
        return out;
    }
    int compare(const Wide &other) const {
        for (std::size_t i = digits.size(); i-- > 0;)
            if (digits[i] != other.digits[i])
                return digits[i] < other.digits[i] ? -1 : 1;
        return 0;
    }
};
} // namespace
std::int64_t xmlDepreciation(const std::string &function, std::int64_t cost, std::int64_t salvage, std::int64_t life,
                             std::int64_t period) {
    calculationCheckpoint();
    if (function != "sln" && function != "syd")
        invalid("지원하지 않는 함수");
    for (auto value : {cost, salvage, life, period})
        if (value < 0 || value > limit)
            invalid("인수는 0 이상의 native 수치여야 합니다.");
    if (!life || (function == "syd" && (!period || period > life)))
        invalid("life>0이며 SYD는 0<period<=life입니다.");
    const auto difference = cost - salvage;
    const bool negative = difference < 0;
    const auto magnitude = std::uint64_t(negative ? -difference : difference);
    auto numerator = Wide(magnitude).times(scale);
    auto denominator = Wide(std::uint64_t(life));
    if (function == "syd") {
        numerator = numerator.times(std::uint64_t(life - period + scale)).times(2);
        denominator = denominator.times(std::uint64_t(life + scale));
    }
    std::uint64_t low = 0, high = std::uint64_t(limit);
    unsigned iterations = 0;
    while (low < high) {
        if (++iterations % 16 == 0)
            calculationCheckpoint();
        const auto middle = low + (high - low + 1) / 2;
        if (denominator.times(middle).compare(numerator) <= 0)
            low = middle;
        else
            high = middle - 1;
    }
    // Exact halfway cases round away from zero, as native numeric arithmetic does.
    if (numerator.times(2).compare(denominator.times(low * 2 + 1)) >= 0)
        ++low;
    if (low > std::uint64_t(limit))
        invalid("반올림 결과가 native 수치 범위를 초과합니다.");
    calculationCheckpoint();
    return negative ? -std::int64_t(low) : std::int64_t(low);
}
} // namespace ppm
