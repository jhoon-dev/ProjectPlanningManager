#include "domain/XmlArithmetic.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <cmath>
namespace ppm {
namespace {
constexpr std::int64_t scale = 1000000, limit = 9000000000000000000LL;
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML 산술 연산: " + message});
}
std::int64_t integer(std::int64_t n) {
    const auto magnitude = n < 0 ? -n : n;
    auto q = magnitude / scale;
    const auto r = magnitude % scale;
    if (r > scale / 2 || (r == scale / 2 && q % 2))
        ++q;
    const auto result = n < 0 ? -q : q;
    if (result < -2147483648LL || result > 2147483647LL)
        invalid("정수 나눗셈 피연산자는 반올림 후32비트 범위여야 합니다.");
    return result;
}
} // namespace
bool xmlArithmeticFormula(const XmlFormula &f) {
    bool quote = false, date = false, ref = false;
    for (std::size_t i = 0; i < f.source.size(); ++i) {
        const auto c = f.source[i];
        if (ref) {
            if (c == '\\' && i + 1 < f.source.size())
                ++i;
            else if (c == ']')
                ref = false;
        } else if (quote) {
            if (c == '"') {
                if (i + 1 < f.source.size() && f.source[i + 1] == '"')
                    ++i;
                else
                    quote = false;
            }
        } else if (date) {
            if (c == '#')
                date = false;
        } else if (c == '"' || c == '#' || c == '[') {
            quote = c == '"';
            date = c == '#';
            ref = c == '[';
        } else if (c == '^' || c == '\\')
            return true;
    }
    return false;
}
bool hasXmlArithmeticOperators(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlArithmeticFormula(*f.xmlFormula))
                return true;
    return false;
}
std::int64_t xmlArithmeticNumber(const std::string &op, std::int64_t left, std::int64_t right) {
    calculationCheckpoint();
    if (left < -limit || left > limit || right < -limit || right > limit)
        invalid("입력 수치 범위 초과");
    if (op == "\\") {
        const auto a = integer(left), b = integer(right);
        if (!b)
            invalid("반올림한 제수는0일 수 없습니다.");
        const auto quotient = a / b;
        if (quotient < -2147483648LL || quotient > 2147483647LL)
            invalid("정수 몫의32비트 범위 초과");
        return quotient * scale;
    }
    if (op != "^")
        invalid("지원하지 않는 연산자");
    if (left < 0 && right % scale)
        invalid("음수 밑에는 정수 지수만 허용합니다.");
    if (!left && right < 0)
        invalid("0의 음수 거듭제곱은 정의되지 않습니다.");
    if (!right)
        return scale;
    if (!left)
        return 0;
    if (right == scale)
        return left;
    if (left == scale || left == -scale)
        return left < 0 && (right / scale) % 2 ? -scale : scale;
    // Evaluate the magnitude separately so large integral exponents keep exact sign parity.
    const auto base = static_cast<long double>(left < 0 ? -left : left) / scale;
    const auto exponent = static_cast<long double>(right) / scale;
    auto result = std::pow(base, exponent);
    if (!std::isfinite(result) || result > static_cast<long double>(limit) / scale)
        invalid("거듭제곱 결과가 유한한 native 수치 범위를 초과합니다.");
    if (left < 0 && (right / scale) % 2)
        result = -result;
    const auto rounded = std::round(result * scale);
    if (std::fabs(rounded) > static_cast<long double>(limit))
        invalid("반올림 결과의 수치 범위 초과");
    calculationCheckpoint();
    return static_cast<std::int64_t>(rounded);
}
} // namespace ppm
