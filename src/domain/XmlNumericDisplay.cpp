#include "domain/XmlNumericDisplay.h"
#include "domain/XmlFormatting.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
namespace ppm {
std::string xmlNumericDisplay(XmlNumericDisplayKind kind, std::int64_t scaled, int digits, int leading, int parentheses,
                              int grouping) {
    calculationCheckpoint();
    if (kind != XmlNumericDisplayKind::Number && kind != XmlNumericDisplayKind::Percent &&
        kind != XmlNumericDisplayKind::Currency)
        throw AppError(ErrorCode::FORMULA_INVALID, {"숫자 표시 종류 오류"});
    constexpr std::int64_t limit = 9000000000000000000LL;
    if (scaled < -limit || scaled > limit || digits < -1 || digits > 6)
        throw AppError(ErrorCode::FORMULA_INVALID, {"숫자 표시 범위 또는 소수 자릿수(-1/0~6) 오류"});
    const auto flag = [](int value, bool fallback) {
        if (value < -2 || value > 0)
            throw AppError(ErrorCode::FORMULA_INVALID, {"표시 옵션은 -2(기본), -1(사용), 0(해제)입니다."});
        return value == -2 ? fallback : value == -1;
    };
    const bool zero = flag(leading, true), parens = flag(parentheses, kind == XmlNumericDisplayKind::Currency),
               groups = flag(grouping, true);
    if (digits == -1)
        digits = 2;
    std::string mask = groups ? "#,##" : "";
    mask += zero ? '0' : '#';
    if (digits)
        mask += '.' + std::string(std::size_t(digits), '0');
    if (kind == XmlNumericDisplayKind::Currency)
        mask = '$' + mask;
    if (kind == XmlNumericDisplayKind::Percent)
        mask += '%';
    const auto negative = parens ? '(' + mask + ')' : '-' + mask;
    XmlFormatInput input;
    input.kind = XmlFormatKind::Number;
    input.scaled = scaled;
    return *xmlFormat(input, mask + ';' + negative, 1033);
}
} // namespace ppm
