#include "domain/XmlProjectConversion.h"
#include "domain/Classification.h"
#include "domain/XmlDateFunctions.h"
#include "domain/XmlFormatting.h"
#include "domain/CalculationControl.h"
#include <array>
namespace ppm {
namespace {
using N = std::int64_t;
constexpr N scale = 1000000, limit = 9000000000000000000LL;
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"Project 변환 함수: " + s});
}
std::string trim(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
std::string lower(std::string s) {
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    return s;
}
struct Unit {
    N minutes;
    const char *label;
};
Unit unit(int format) {
    if (format >= 35 && format <= 44)
        format -= 32;
    switch (format) {
    case 3:
        return {1, "m"};
    case 4:
        return {1, "em"};
    case 5:
        return {60, "h"};
    case 6:
        return {60, "eh"};
    case 7:
        return {480, "d"};
    case 8:
        return {1440, "ed"};
    case 9:
        return {2400, "w"};
    case 10:
        return {10080, "ew"};
    case 11:
        return {9600, "mo"};
    case 12:
        return {43200, "emo"};
    default:
        invalid("기간 형식은3~12 또는35~44입니다.");
    }
}
std::string number(N n) {
    const auto v = n < 0 ? -n : n;
    auto out = std::string(n < 0 ? "-" : "") + std::to_string(v / scale);
    if (v % scale) {
        auto f = std::to_string(v % scale + scale).substr(1);
        while (f.back() == '0')
            f.pop_back();
        out += '.' + f;
    }
    return out;
}
} // namespace
std::optional<std::string> xmlProjectDateValue(const std::string &input) {
    calculationCheckpoint();
    const auto s = trim(input);
    if (lower(s) == "na")
        return {};
    if (s.size() > 32)
        invalid("날짜는명시 ISO 민간 시각만 지원합니다.");
    return normalizeFieldDateTime(s.size() == 10 ? s + "T00:00:00" : s);
}
std::string xmlProjectDateFormat(const std::string &input, int format) {
    calculationCheckpoint();
    if (format == 255)
        format = 0;
    if (format < 0 || format > 20)
        invalid("날짜 형식은0~20 또는255입니다.");
    const auto normalized = xmlProjectDateValue(input);
    if (!normalized)
        invalid("NA에는날짜 표시 함수를 직접 적용할 수 없습니다.");
    const auto &s = *normalized;
    const auto display = [&](const std::string &mask) {
        XmlFormatInput v;
        v.kind = XmlFormatKind::Date;
        v.text = s;
        return *xmlFormat(v, mask, 1033, 1, 1);
    };
    static const std::array<const char *, 21> masks = {"m/d/yy h:nn AM/PM",
                                                       "m/d/yy",
                                                       "mmmm d, yyyy h:nn AM/PM",
                                                       "mmmm d, yyyy",
                                                       "mmm d h:nn AM/PM",
                                                       "mmm d, \"'\"yy",
                                                       "mmmm d",
                                                       "mmm d",
                                                       "ddd m/d/yy h:nn AM/PM",
                                                       "ddd m/d/yy",
                                                       "ddd mmm d, \"'\"yy",
                                                       "ddd h:nn AM/PM",
                                                       "m/d",
                                                       "d",
                                                       "h:nn AM/PM",
                                                       "ddd mmm d",
                                                       "ddd m/d",
                                                       "ddd d",
                                                       "",
                                                       "",
                                                       "m/d/yyyy"};
    if (format == 18 || format == 19) {
        const auto week = xmlDatePart("ww", s, 1, 1);
        auto out = std::string("W") + (week < 10 ? "0" : "") + std::to_string(week) + '/' +
                   std::to_string(xmlDatePart("w", s, 1, 1));
        if (format == 19)
            out += '/' + s.substr(2, 2) + ' ' + display("h:nn AM/PM");
        return out;
    }
    return display(masks[std::size_t(format)]);
}
N xmlProjectDurationValue(const std::string &input) {
    calculationCheckpoint();
    auto s = lower(trim(input));
    if (s.size() > 64)
        invalid("기간 문자열이 너무 깁니다.");
    if (!s.empty() && s.back() == '?')
        s = trim(s.substr(0, s.size() - 1));
    N factor = 1;
    for (int i = 12; i >= 3; --i) {
        const auto u = unit(i);
        const std::string label = u.label;
        if (s.ends_with(label)) {
            factor = u.minutes;
            s = trim(s.substr(0, s.size() - label.size()));
            break;
        }
    }
    std::size_t at = 0;
    bool negative = !s.empty() && s.front() == '-';
    if (!s.empty() && (s.front() == '-' || s.front() == '+'))
        ++at;
    const auto begin = at;
    N whole = 0, fraction = 0, place = scale;
    while (at < s.size() && s[at] >= '0' && s[at] <= '9') {
        const auto d = s[at++] - '0';
        if (whole > (limit / scale - d) / 10)
            invalid("기간 수치 범위 초과");
        whole = whole * 10 + d;
    }
    if (at == begin)
        invalid("기간에는정수 부분이 필요합니다.");
    if (at < s.size() && s[at] == '.') {
        const auto start = ++at;
        while (at < s.size() && s[at] >= '0' && s[at] <= '9' && place > 1) {
            place /= 10;
            fraction += (s[at++] - '0') * place;
        }
        if (at == start)
            invalid("기간 소수점 형식 오류");
    }
    if (at != s.size() || whole > (limit - fraction) / scale)
        invalid("기간은소수6자리 점 소수와 명시 단위입니다.");
    auto magnitude = whole * scale + fraction;
    if (magnitude > limit / factor)
        invalid("분 환산 뒤 native 기간 범위 초과");
    magnitude *= factor;
    return negative ? -magnitude : magnitude;
}
std::string xmlProjectDurationFormat(N n, int format) {
    calculationCheckpoint();
    const auto u = unit(format);
    if (n < -limit || n > limit)
        invalid("기간 수치 범위 초과");
    auto q = n / u.minutes, r = n % u.minutes;
    if (r < 0)
        r = -r;
    if (r * 2 >= u.minutes)
        q += n < 0 ? -1 : 1;
    return number(q) + ' ' + u.label + (format >= 35 ? "?" : "");
}
} // namespace ppm
