#include "domain/XmlMath.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <cmath>
namespace ppm {
namespace {
constexpr std::int64_t scale = 1000000, limit = 9000000000000000000LL;
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML 수학 함수: " + message});
}
} // namespace
std::int64_t xmlMathNumber(const std::string &function, std::int64_t scaled) {
    calculationCheckpoint();
    if (scaled < -limit || scaled > limit)
        invalid("입력 수치 범위 초과");
    // Keep the fractional input when a large integer part would swallow its low bits.
    // Both integer pieces are exactly representable even with a 53-bit long double.
    const auto whole = static_cast<long double>(scaled / scale);
    const auto remainder = static_cast<long double>(scaled % scale);
    const auto fraction = remainder / scale;
    const auto x = whole + fraction;
    const auto tail = ((whole - x) + fraction) + std::fma(-fraction, scale, remainder) / scale;
    long double result;
    if (function == "atn")
        result = std::atan(x) + std::atan(tail / (1 + x * x + x * tail));
    else if (function == "sin")
        result = std::sin(x) * std::cos(tail) + std::cos(x) * std::sin(tail);
    else if (function == "cos")
        result = std::cos(x) * std::cos(tail) - std::sin(x) * std::sin(tail);
    else if (function == "tan") {
        const auto high = std::tan(x), low = std::tan(tail);
        result = (high + low) / (1 - high * low);
    } else if (function == "log") {
        if (scaled <= 0)
            invalid("Log의 인수는0보다 커야 합니다.");
        result = std::log(x) + std::log1p(tail / x);
    } else if (function == "exp") {
        // No floating overflow/underflow is needed to decide these fixed-six results.
        if (scaled >= 30000000)
            invalid("Exp 결과는native 수치 범위를 초과합니다.");
        if (scaled <= -16000000)
            return 0;
        result = std::exp(x) * std::exp(tail);
    } else
        invalid("지원하지 않는 함수");
    if (!std::isfinite(result) || std::fabs(result) > static_cast<long double>(limit) / scale)
        invalid("계산 결과가 유한한 native 수치 범위를 초과합니다.");
    // Check before casting: round away from zero at half a native unit, never saturate/wrap.
    const auto rounded = std::round(result * scale);
    if (std::fabs(rounded) > static_cast<long double>(limit))
        invalid("반올림 결과의 수치 범위 초과");
    calculationCheckpoint();
    return static_cast<std::int64_t>(rounded);
}
} // namespace ppm
