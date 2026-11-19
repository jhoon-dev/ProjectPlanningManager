#include "domain/XmlNumberText.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <charconv>
namespace ppm {
namespace {
using N = std::int64_t;
constexpr N scale = 1000000, limit = 9000000000000000000LL;
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML 숫자 문자열 함수: " + s});
}
bool digit(char c) {
    return c >= '0' && c <= '9';
}
void suffix(char c) {
    if (c == '!' || c == '@' || c == '$' || c == '^')
        invalid("Val의 !/@/$/^ 형식 접미사는지원하지 않습니다.");
}
N integerSuffix(N value, char type) {
    if ((type == '%' && (value < -32768 || value > 32767)) ||
        (type == '&' && (value < -2147483648LL || value > 2147483647LL)))
        invalid("Val의 정수 형식 접미사 범위 초과");
    return value;
}
std::string increment(std::string s) {
    for (auto i = s.size(); i > 0;) {
        if (s[--i] != '9') {
            ++s[i];
            return s;
        }
        s[i] = '0';
    }
    return '1' + s;
}
N decimal(std::string digits, int fraction, int exponent, bool negative) {
    const auto first = digits.find_first_not_of('0');
    if (first == std::string::npos)
        return 0;
    digits.erase(0, first);
    const int point = int(digits.size()) - fraction + exponent;
    // Check exact magnitude before rounding to native precision.
    if (point > 13)
        invalid("Val 값은±9,000,000,000,000까지입니다.");
    if (point == 13) {
        auto whole = digits.substr(0, 13);
        whole.append(13 - whole.size(), '0');
        if (whole > "9000000000000" ||
            (whole == "9000000000000" && digits.size() > 13 && digits.find_first_not_of('0', 13) != std::string::npos))
            invalid("Val 값은native 범위를 초과합니다.");
    }
    const int keep = point + 6;
    std::string scaled;
    if (keep < 0)
        return 0;
    if (keep == 0)
        scaled = digits[0] >= '5' ? "1" : "0";
    else {
        scaled = digits.substr(0, std::size_t(keep));
        if (scaled.size() < std::size_t(keep))
            scaled.append(std::size_t(keep) - scaled.size(), '0');
        else if (digits.size() > std::size_t(keep) && digits[std::size_t(keep)] >= '5')
            scaled = increment(scaled);
    }
    N result = 0;
    const auto parsed = std::from_chars(scaled.data(), scaled.data() + scaled.size(), result);
    if (parsed.ec != std::errc{} || result > limit)
        invalid("Val 수치 범위 초과");
    return negative ? -result : result;
}
} // namespace
std::string xmlStrNumber(N n) {
    calculationCheckpoint();
    if (n < -limit || n > limit)
        invalid("Str 수치 범위 초과");
    const auto magnitude = n < 0 ? -n : n;
    const std::string sign = n < 0 ? "-" : " ";
    if (magnitude && magnitude < 100) {
        auto digits = std::to_string(magnitude);
        const auto exponent = digits.size() == 1 ? "E-06" : "E-05";
        const auto first = digits.substr(0, 1);
        digits.erase(0, 1);
        while (!digits.empty() && digits.back() == '0')
            digits.pop_back();
        return sign + first + (digits.empty() ? "" : "." + digits) + exponent;
    }
    auto whole = std::to_string(magnitude / scale);
    if (magnitude % scale) {
        auto fraction = std::to_string(magnitude % scale + scale).substr(1);
        while (fraction.back() == '0')
            fraction.pop_back();
        whole = (magnitude < scale ? "" : whole) + '.' + fraction;
    }
    return sign + whole;
}
std::string xmlRadixNumber(N n, int radix) {
    calculationCheckpoint();
    if ((radix != 8 && radix != 16) || n < -2147483648LL || n > 2147483647LL)
        invalid("Hex/Oct는반올림 후32비트 정수 범위입니다.");
    auto bits = static_cast<std::uint32_t>(n);
    std::string result;
    do {
        result += "0123456789ABCDEF"[bits % radix];
        bits /= radix;
    } while (bits);
    std::reverse(result.begin(), result.end());
    return result;
}
N xmlValNumber(const std::string &raw) {
    calculationCheckpoint();
    if (raw.size() > 4096)
        invalid("Val 문자열은4,096 bytes까지입니다.");
    std::string s;
    for (auto c : raw)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            s += c;
    std::size_t at = 0;
    if (s.size() >= 2 && s[0] == '&' && (s[1] == 'H' || s[1] == 'h' || s[1] == 'O' || s[1] == 'o')) {
        const int radix = s[1] == 'H' || s[1] == 'h' ? 16 : 8;
        at = 2;
        std::uint64_t value = 0;
        while (at < s.size()) {
            const auto c = s[at];
            const int d = digit(c)               ? c - '0'
                          : c >= 'A' && c <= 'F' ? c - 'A' + 10
                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                 : -1;
            if (d < 0 || d >= radix)
                break;
            if (value > (0xffffffffULL - std::uint64_t(d)) / radix)
                invalid("Val 진수 접두부는32비트까지입니다.");
            value = value * radix + std::uint64_t(d);
            ++at;
        }
        if (at == 2)
            return 0;
        const auto type = at < s.size() ? s[at] : '\0';
        suffix(type);
        N result = N(value);
        if (type != '&' && value <= 65535) {
            if (value >= 32768)
                result -= 65536;
        } else if (value >= 2147483648ULL)
            result -= 4294967296LL;
        return integerSuffix(result, type) * scale;
    }
    bool negative = false, point = false;
    if (at < s.size() && (s[at] == '+' || s[at] == '-'))
        negative = s[at++] == '-';
    std::string digits;
    while (at < s.size() && digit(s[at]))
        digits += s[at++];
    int fraction = 0;
    if (at < s.size() && s[at] == '.') {
        point = true;
        ++at;
        while (at < s.size() && digit(s[at])) {
            digits += s[at++];
            ++fraction;
        }
    }
    if (digits.empty())
        return 0;
    int exponent = 0;
    bool scientific = false;
    if (at < s.size() && (s[at] == 'e' || s[at] == 'E' || s[at] == 'd' || s[at] == 'D')) {
        auto end = at + 1;
        bool neg = false;
        if (end < s.size() && (s[end] == '+' || s[end] == '-'))
            neg = s[end++] == '-';
        const auto start = end;
        while (end < s.size() && digit(s[end])) {
            if (exponent > (10000 - (s[end] - '0')) / 10)
                invalid("Val 지수는±10000까지입니다.");
            exponent = exponent * 10 + s[end++] - '0';
        }
        if (end > start) {
            at = end;
            scientific = true;
            if (neg)
                exponent = -exponent;
        }
    }
    const auto type = at < s.size() ? s[at] : '\0';
    suffix(type);
    const auto result = decimal(digits, fraction, exponent, negative);
    if (type == '%' || type == '&') {
        if (point || scientific || result % scale)
            invalid("Val의 %/& 접미사는지수·소수점 없는 정수 접두부만 지원합니다.");
        return integerSuffix(result / scale, type) * scale;
    }
    return result;
}
} // namespace ppm
