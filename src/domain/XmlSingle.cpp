#include "domain/XmlSingle.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <bit>
namespace jobs {
std::int64_t xmlSingleNumber(std::int64_t scaled) {
    calculationCheckpoint();
    constexpr std::uint64_t scale = 1000000, limit = 9000000000000000000ULL;
    const auto invalid = [] {
        throw AppError(ErrorCode::FORMULA_INVALID, {"CSng의 입력/단정밀도 결과가 native 수치 범위를 초과합니다."});
    };
    // Avoid negating INT64_MIN until the supported native range has been checked.
    if (scaled < -std::int64_t(limit) || scaled > std::int64_t(limit))
        invalid();
    const auto n = std::uint64_t(scaled < 0 ? -scaled : scaled);
    if (!n)
        return 0;
    int exponent;
    if (n >= scale)
        exponent = int(std::bit_width(n / scale)) - 1;
    else {
        exponent = -1;
        while ((n << -exponent) < scale)
            --exponent;
    }
    // The shifted numerator is below 2^24*scale, or the shifted denominator below 2^20*scale.
    const int shift = 23 - exponent;
    const auto numerator = shift >= 0 ? n << shift : n;
    const auto denominator = shift >= 0 ? scale : scale << -shift;
    auto mantissa = numerator / denominator;
    const auto remainder = numerator % denominator;
    if (remainder > denominator / 2 || (remainder == denominator / 2 && (mantissa & 1)))
        ++mantissa;
    if (mantissa == (1ULL << 24)) {
        mantissa >>= 1;
        ++exponent;
    }
    auto result = mantissa * scale;
    if (exponent >= 23) {
        const auto bits = exponent - 23;
        if (result > (limit >> bits))
            invalid();
        result <<= bits;
    } else {
        const auto bits = 23U - static_cast<unsigned>(exponent);
        if (bits >= 64)
            throw AppError(ErrorCode::FORMULA_INVALID, {"CSng의 단정밀도 지수가 native 수치 범위를 초과합니다."});
        const auto divisor = 1ULL << bits;
        const auto fraction = result % divisor;
        result /= divisor;
        if (fraction >= divisor / 2)
            ++result;
    }
    calculationCheckpoint();
    return scaled < 0 ? -std::int64_t(result) : std::int64_t(result);
}
} // namespace jobs
