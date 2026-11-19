#include "domain/XmlFormatting.h"
#include "domain/CalculationControl.h"
#include "domain/Classification.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <sstream>
#include <vector>
namespace ppm {
namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"Format/StrConv: " + s});
}
using Points = std::vector<std::uint32_t>;
Points decode(const std::string &s) {
    Points out;
    for (std::size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i++]);
        std::uint32_t cp = c;
        const int n = c < 128 ? 0 : c >= 194 && c <= 223 ? 1 : c >= 224 && c <= 239 ? 2 : c >= 240 && c <= 244 ? 3 : -1;
        if (n < 0 || i + n > s.size())
            invalid("올바른 UTF-8 문자열이 필요합니다.");
        if (n)
            cp &= (1u << (6 - n)) - 1;
        for (int j = 0; j < n; ++j) {
            c = static_cast<unsigned char>(s[i++]);
            if ((c & 192) != 128)
                invalid("UTF-8 연속 바이트 오류");
            cp = (cp << 6) | (c & 63);
        }
        if ((n == 1 && cp < 128) || (n == 2 && cp < 2048) || (n == 3 && cp < 65536) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff))
            invalid("UTF-8 스칼라 오류");
        out.push_back(cp);
    }
    return out;
}
std::string encode(const Points &s) {
    std::string out;
    for (auto c : s) {
        if (c < 128)
            out += char(c);
        else if (c < 2048) {
            out += char(192 | (c >> 6));
            out += char(128 | (c & 63));
        } else if (c < 65536) {
            out += char(224 | (c >> 12));
            out += char(128 | ((c >> 6) & 63));
            out += char(128 | (c & 63));
        } else {
            out += char(240 | (c >> 18));
            out += char(128 | ((c >> 12) & 63));
            out += char(128 | ((c >> 6) & 63));
            out += char(128 | (c & 63));
        }
    }
    if (out.size() > 4096)
        invalid("결과는4,096 bytes까지입니다.");
    return out;
}
struct Unicode {
    std::array<std::map<std::uint32_t, Points>, 3> casing;
    std::map<std::uint32_t, std::uint32_t> wide, narrow;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> compose;
    std::map<std::uint32_t, Points> decompose;
    std::array<std::vector<std::pair<std::uint32_t, std::uint32_t>>, 2> properties;
    Unicode() {
        const char *raw =
#include "../../resources/text/unicode-16.0.0.inc"
            ;
        std::istringstream lines(raw);
        std::string line;
        while (std::getline(lines, line)) {
            std::istringstream row(line);
            char tag = 0;
            std::uint32_t a = 0, b = 0, c = 0;
            if (!(row >> tag >> std::hex >> a >> b))
                continue;
            if (tag == 'C') {
                Points mapping;
                while (row >> c)
                    mapping.push_back(c);
                casing.at(b)[a] = mapping;
            } else if (tag == 'W') {
                wide[a] = b;
                narrow[b] = a;
            } else if (tag == 'P') {
                row >> c;
                compose[{a, b}] = c;
                decompose[c] = {a, b};
            } else
                properties[tag == 'A' ? 0 : 1].push_back({a, b});
        }
    }
    bool property(std::uint32_t cp, int p) const {
        const auto &ranges = properties[p];
        const auto it =
            std::upper_bound(ranges.begin(), ranges.end(), cp, [](auto n, const auto &r) { return n < r.first; });
        return it != ranges.begin() && cp <= std::prev(it)->second;
    }
};
const Unicode &unicode() {
    static const Unicode u;
    return u;
}
bool wordSpace(std::uint32_t c) {
    return !c || (c >= 9 && c <= 13) || c == 32 || c == 0x3000;
}
Points casing(const Points &s, int mode) {
    const auto &u = unicode();
    Points out;
    bool first = true;
    for (std::size_t i = 0; i < s.size(); ++i) {
        auto cp = s[i];
        const auto m = mode == 3 ? (first ? 2 : 1) : mode - 1;
        // Unicode default Final_Sigma condition, ignoring Case_Ignorable scalars.
        bool sigma = false;
        if (cp == 0x3a3 && m == 1) {
            bool before = false, after = false;
            for (auto j = i; j > 0;) {
                auto c = s[--j];
                if (!u.property(c, 1)) {
                    before = u.property(c, 0);
                    break;
                }
            }
            for (auto j = i + 1; j < s.size(); ++j)
                if (!u.property(s[j], 1)) {
                    after = u.property(s[j], 0);
                    break;
                }
            sigma = before && !after;
        }
        const auto it = u.casing[m].find(cp);
        if (sigma)
            out.push_back(0x3c2);
        else if (it == u.casing[m].end())
            out.push_back(cp);
        else
            out.insert(out.end(), it->second.begin(), it->second.end());
        first = wordSpace(cp);
    }
    return out;
}
void profile(int lcid) {
    if (lcid != 1033 && lcid != 1041 && lcid != 1042)
        invalid("LCID는1033(en-US),1041(ja-JP),1042(ko-KR)만 지원합니다.");
}
std::string lower(std::string s) {
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return s;
}
struct Token {
    char symbol = 0;
    std::string text;
};
using Mask = std::vector<Token>;
std::vector<Mask> tokenize(const std::string &mask) {
    if (mask.size() > 256)
        invalid("표시 형식은256 UTF-8 bytes까지입니다.");
    (void)decode(mask);
    std::vector<Mask> sections(1);
    for (std::size_t i = 0; i < mask.size();) {
        auto c = mask[i++];
        if (c == ';') {
            if (sections.size() == 4)
                invalid("표시 형식은최대4개 구간입니다.");
            sections.emplace_back();
        } else if (c == '"') {
            const auto end = mask.find('"', i);
            if (end == std::string::npos)
                invalid("표시 형식의 따옴표 닫기 누락");
            sections.back().push_back({0, mask.substr(i, end - i)});
            i = end + 1;
        } else if (c == '\\') {
            if (i == mask.size())
                invalid("표시 형식의 이스케이프 문자 누락");
            const auto point = decode(mask.substr(i)).front();
            auto literal = encode({point});
            sections.back().push_back({0, literal});
            i += literal.size();
        } else if (static_cast<unsigned char>(c) >= 128) {
            --i;
            const auto literal = encode({decode(mask.substr(i)).front()});
            sections.back().push_back({0, literal});
            i += literal.size();
        } else if (c == '[' || c == ']' || c == '_' || c == '*' || static_cast<unsigned char>(c) < 32)
            invalid("조건·색상·채움·제어문자 표시 형식은 지원하지 않습니다.");
        else
            sections.back().push_back({c, std::string(1, c)});
    }
    return sections;
}
std::string literal(const Mask &mask) {
    std::string out;
    for (const auto &t : mask)
        out += t.text;
    return out;
}
// Decimal strings avoid overflow when percent/scaling moves the decimal point.
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
std::pair<std::string, std::string> rounded(std::string digits, int point, int precision) {
    if (point < 0) {
        digits.insert(0, std::size_t(-point), '0');
        point = 0;
    }
    const auto keep = std::size_t(point + precision);
    if (digits.size() < keep)
        digits.append(keep - digits.size(), '0');
    const bool up = digits.size() > keep && digits[keep] >= '5';
    digits.resize(keep);
    if (up)
        digits = increment(digits);
    if (digits.size() > keep)
        ++point;
    auto whole = digits.substr(0, std::size_t(point));
    auto fraction = digits.substr(std::size_t(point));
    auto nz = whole.find_first_not_of('0');
    whole = nz == std::string::npos ? "0" : whole.substr(nz);
    return {whole, fraction};
}
std::string numeric(std::int64_t n, Mask mask, bool autoMinus) {
    int decimal = -1, exponent = -1, first = -1, last = -1, percent = 0;
    for (int i = 0; i < int(mask.size()); ++i) {
        auto c = mask[i].symbol;
        if ((c == 'E' || c == 'e') && first >= 0 && i + 2 < int(mask.size()) &&
            (mask[i + 1].symbol == '+' || mask[i + 1].symbol == '-') && mask[i + 2].symbol == '0') {
            if (exponent >= 0)
                invalid("중복 지수 표지");
            exponent = i;
        }
        if (c == '.' && exponent < 0) {
            if (decimal >= 0)
                invalid("중복 소수점");
            decimal = i;
        }
        if ((c == '0' || c == '#') && exponent < 0) {
            if (first < 0)
                first = i;
            last = i;
        }
        percent += c == '%';
    }
    if (first < 0) {
        bool dateToken = false, otherLetter = false;
        for (const auto &t : mask) {
            const auto c = lower(t.text);
            if (t.symbol && c.size() == 1 && c[0] >= 'a' && c[0] <= 'z') {
                if (std::string("ymdhnsqwctap").find(c[0]) != std::string::npos)
                    dateToken = true;
                else
                    otherLetter = true;
            }
        }
        if (dateToken && !otherLetter)
            invalid("날짜 형식에는Date 값이 필요합니다.");
        return literal(mask);
    }
    const bool hasDecimal = decimal >= 0;
    if (decimal < 0)
        decimal = last + 1;
    if (decimal < first)
        invalid("소수점 앞에0 또는# 자리표시자가 필요합니다.");
    const auto integerEnd = decimal;
    int scaleCommas = 0;
    for (int i = integerEnd - 1; i >= first && mask[i].symbol == ','; --i)
        ++scaleCommas;
    const int groupingEnd = integerEnd - scaleCommas;
    // Commas may follow the last integer placeholder before suffix literals.
    if (integerEnd == last + 1 && exponent < 0)
        for (auto i = std::size_t(last + 1); i < mask.size() && mask[i].symbol == ','; ++i)
            ++scaleCommas;
    const bool grouping =
        std::any_of(mask.begin() + first, mask.begin() + std::max(first, std::min(groupingEnd, last + 1)),
                    [](const auto &t) { return t.symbol == ','; });
    int mandatoryWhole = 0, fractionCount = 0, mandatoryFraction = 0;
    for (int i = first; i <= last; ++i) {
        auto c = mask[i].symbol;
        if (c == '0' || c == '#') {
            if (i < decimal)
                mandatoryWhole += c == '0';
            else {
                ++fractionCount;
                if (c == '0')
                    mandatoryFraction = fractionCount;
            }
        } else if (c != '.' && c != ',')
            invalid("숫자 자리표시자 사이의 리터럴은 지원하지 않습니다.");
        else if (c == ',' && i > decimal)
            invalid("소수부 쉼표는 지원하지 않습니다.");
    }
    if (fractionCount > 6 || mandatoryWhole > 32 || percent > 8 || scaleCommas > 8)
        invalid("소수6자리·정수32자리·배율8개 한도");
    for (int i = 0; i < int(mask.size()); ++i) {
        const auto c = mask[i].symbol;
        if (c && (std::string("@&<>!").find(c) != std::string::npos ||
                  (i != exponent && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))))
            invalid("숫자 값에 날짜/문자열 토큰을 사용할 수 없습니다.");
        if (c == ',' && hasDecimal && i > decimal)
            invalid("소수부 쉼표는 지원하지 않습니다.");
    }
    auto digits = std::to_string(n < 0 ? -n : n);
    const int point = int(digits.size()) - 6 + 2 * percent - 3 * scaleCommas;
    int scientificPower = 0, exponentDigits = 0;
    bool exponentPlus = false;
    if (exponent >= 0) {
        if (exponent != last + 1 || mandatoryWhole != 1 || decimal != first + 1)
            invalid("지수 형식은0[.0/#]E[+/-]0~000입니다.");
        auto i = exponent + 1;
        if (i >= int(mask.size()) || (mask[i].symbol != '+' && mask[i].symbol != '-'))
            invalid("지수 부호 필요");
        exponentPlus = mask[i++].symbol == '+';
        while (i < int(mask.size()) && mask[i].symbol == '0') {
            ++exponentDigits;
            ++i;
        }
        if (!exponentDigits || exponentDigits > 3)
            invalid("지수는1~3자리입니다.");
        for (; i < int(mask.size()); ++i)
            if (mask[i].symbol && std::string("0#.,Ee+-").find(mask[i].symbol) != std::string::npos)
                invalid("지수 형식 오류");
        scientificPower = n ? point - 1 : 0;
    }
    auto [whole, fraction] = rounded(digits, exponent < 0 ? point : n ? 1 : point, fractionCount);
    if (exponent >= 0 && whole.size() > 1) {
        ++scientificPower;
        whole = "1";
        fraction.assign(fractionCount, '0');
    }
    while (fraction.size() > std::size_t(mandatoryFraction) && fraction.back() == '0')
        fraction.pop_back();
    if (whole == "0" && !mandatoryWhole)
        whole.clear();
    if (whole.size() < std::size_t(mandatoryWhole))
        whole.insert(0, std::size_t(mandatoryWhole) - whole.size(), '0');
    if (grouping)
        for (int i = int(whole.size()) - 3; i > 0; i -= 3)
            whole.insert(std::size_t(i), 1, ',');
    std::string out = autoMinus && n < 0 ? "-" : "";
    out += literal(Mask(mask.begin(), mask.begin() + first)) + whole;
    if (decimal <= last)
        out += '.' + fraction;
    int suffix = last + 1;
    if (decimal <= last && suffix < int(mask.size()) && mask[suffix].symbol == ',')
        invalid("소수부 뒤의 쉼표는 지원하지 않습니다.");
    while (suffix < int(mask.size()) && mask[suffix].symbol == ',')
        ++suffix;
    if (exponent >= 0) {
        auto e = std::to_string(scientificPower < 0 ? -scientificPower : scientificPower);
        if (e.size() < std::size_t(exponentDigits))
            e.insert(0, std::size_t(exponentDigits) - e.size(), '0');
        out += mask[exponent].text + (scientificPower < 0 ? "-" : exponentPlus ? "+" : "") + e;
        suffix = exponent + 2 + exponentDigits;
    }
    out += literal(Mask(mask.begin() + suffix, mask.end()));
    return out;
}
std::string pad(int n, int count = 2) {
    auto s = std::to_string(n);
    if (s.size() < std::size_t(count))
        s.insert(0, std::size_t(count) - s.size(), '0');
    return s;
}
const std::array<std::string, 12> months = {"January", "February", "March",     "April",   "May",      "June",
                                            "July",    "August",   "September", "October", "November", "December"};
const std::array<std::string, 7> days = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const std::array<std::string, 7> koreanDays = {"일", "월", "화", "수", "목", "금", "토"};
const std::array<std::string, 7> japaneseDays = {"日", "月", "火", "水", "木", "金", "土"};
struct Civil {
    int y, m, d, h, n, s;
    std::chrono::sys_days day;
};
Civil civil(const std::string &s) {
    // Inputs come from the formula engine's validated, normalized native Date value.
    if (s.size() < 19)
        invalid("정규 ISO 날짜 값이 필요합니다.");
    Civil d{std::stoi(s.substr(0, 4)),
            std::stoi(s.substr(5, 2)),
            std::stoi(s.substr(8, 2)),
            std::stoi(s.substr(11, 2)),
            std::stoi(s.substr(14, 2)),
            std::stoi(s.substr(17, 2)),
            {}};
    const auto ymd = std::chrono::year{d.y} / unsigned(d.m) / unsigned(d.d);
    if (!ymd.ok() || d.y < 1 || d.y > 9999 || d.h > 23 || d.n > 59 || d.s > 59)
        invalid("날짜 범위 오류");
    d.day = std::chrono::sys_days(ymd);
    return d;
}
std::string dateFormat(const Civil &d, const Mask &mask, int lcid, int firstDay, int firstWeek);
std::string namedDate(const Civil &d, const std::string &name, int lcid, int firstDay, int firstWeek) {
    const auto date = lcid == 1033   ? std::to_string(d.m) + '/' + std::to_string(d.d) + '/' + pad(d.y, 4)
                      : lcid == 1041 ? pad(d.y, 4) + '/' + pad(d.m) + '/' + pad(d.d)
                                     : pad(d.y, 4) + '-' + pad(d.m) + '-' + pad(d.d);
    const auto am = d.h < 12;
    const std::string period = lcid == 1033   ? (am ? "AM" : "PM")
                               : lcid == 1041 ? (am ? "午前" : "午後")
                                              : (am ? "오전" : "오후");
    const auto h12 = d.h % 12 ? d.h % 12 : 12;
    const auto time = lcid == 1033   ? std::to_string(h12) + ':' + pad(d.n) + ':' + pad(d.s) + ' ' + period
                      : lcid == 1041 ? std::to_string(d.h) + ':' + pad(d.n) + ':' + pad(d.s)
                                     : period + ' ' + std::to_string(h12) + ':' + pad(d.n) + ':' + pad(d.s);
    const auto weekday = std::chrono::weekday(d.day).c_encoding();
    if (name == "general date")
        return d.h || d.n || d.s ? date + ' ' + time : date;
    if (name == "short date")
        return date;
    if (name == "long time")
        return time;
    if (name == "short time")
        return pad(d.h) + ':' + pad(d.n);
    if (name == "medium time")
        return lcid == 1042 ? period + ' ' + pad(h12) + ':' + pad(d.n) : pad(h12) + ':' + pad(d.n) + ' ' + period;
    if (name == "medium date")
        return lcid == 1033   ? std::to_string(d.d) + '-' + months[d.m - 1].substr(0, 3) + '-' + pad(d.y % 100)
               : lcid == 1041 ? pad(d.y % 100) + '-' + pad(d.m) + '-' + pad(d.d)
                              : pad(d.y % 100) + "년 " + pad(d.m) + "월 " + pad(d.d) + "일";
    if (name == "long date")
        return lcid == 1033   ? days[weekday] + ", " + months[d.m - 1] + ' ' + std::to_string(d.d) + ", " + pad(d.y, 4)
               : lcid == 1041 ? pad(d.y, 4) + "年" + std::to_string(d.m) + "月" + std::to_string(d.d) + "日"
                              : pad(d.y, 4) + "년 " + std::to_string(d.m) + "월 " + std::to_string(d.d) + "일 " +
                                    koreanDays[weekday] + "요일";
    return dateFormat(d, tokenize(name).front(), lcid, firstDay, firstWeek);
}
std::string dateFormat(const Civil &d, const Mask &mask, int lcid, int firstDay, int firstWeek) {
    const auto matches = [&](std::size_t at, const std::string &token) {
        if (at + token.size() > mask.size())
            return false;
        for (std::size_t j = 0; j < token.size(); ++j)
            if (!mask[at + j].symbol || lower(mask[at + j].text) != token.substr(j, 1))
                return false;
        return true;
    };
    bool ampm = false;
    for (std::size_t i = 0; i < mask.size(); ++i)
        ampm = ampm || matches(i, "am/pm") || matches(i, "a/p") || matches(i, "ampm");
    const auto weekday = std::chrono::weekday(d.day).c_encoding();
    const auto startWeek = [&](int y) {
        const auto jan = std::chrono::sys_days(std::chrono::year(y) / 1 / 1);
        const auto offset = (int(std::chrono::weekday(jan).c_encoding()) - (firstDay - 1) + 7) % 7;
        auto start = jan - std::chrono::days(offset);
        if ((firstWeek == 2 && offset > 3) || (firstWeek == 3 && offset))
            start += std::chrono::days(7);
        return start;
    };
    std::string out;
    char previous = 0;
    for (std::size_t i = 0; i < mask.size();) {
        const auto c = mask[i].symbol;
        if (!c) {
            out += mask[i++].text;
            continue;
        }
        std::string period;
        int length = 0;
        if (matches(i, "am/pm")) {
            period = d.h < 12 ? "AM" : "PM";
            if (c == 'a')
                period = lower(period);
            length = 5;
        } else if (matches(i, "a/p")) {
            period = d.h < 12 ? "A" : "P";
            if (c == 'a')
                period = lower(period);
            length = 3;
        } else if (matches(i, "ampm")) {
            period = lcid == 1033   ? (d.h < 12 ? "AM" : "PM")
                     : lcid == 1041 ? (d.h < 12 ? "午前" : "午後")
                                    : (d.h < 12 ? "오전" : "오후");
            length = 4;
        }
        if (length) {
            // Each ASCII format character must be an unescaped token.
            for (int j = 0; j < length; ++j)
                if (i + j >= mask.size() || !mask[i + j].symbol)
                    invalid("AM/PM 토큰 오류");
            out += period;
            i += length;
            previous = 'a';
            continue;
        }
        auto symbol = char(c >= 'A' && c <= 'Z' ? c + 32 : c);
        auto end = i + 1;
        while (end < mask.size() && lower(mask[end].text) == std::string(1, symbol) && mask[end].symbol)
            ++end;
        const auto count = int(end - i);
        const auto display = [&](int value) { return count == 1 ? std::to_string(value) : pad(value, count); };
        if (symbol == 'y' && (count == 1 || count == 2 || count == 4))
            out += count == 4 ? pad(d.y, 4)
                   : count == 2
                       ? pad(d.y % 100)
                       : std::to_string((d.day - std::chrono::sys_days(std::chrono::year(d.y) / 1 / 1)).count() + 1);
        else if (symbol == 'm' && count <= 4) {
            if (count <= 2)
                out += display(previous == 'h' ? d.n : d.m);
            else if (lcid == 1033)
                out += count == 3 ? months[d.m - 1].substr(0, 3) : months[d.m - 1];
            else
                out += std::to_string(d.m) + (lcid == 1041 ? "月" : "월");
        } else if (symbol == 'd' && count <= 6) {
            if (count <= 2)
                out += display(d.d);
            else if (count <= 4)
                out += lcid == 1033   ? (count == 3 ? days[weekday].substr(0, 3) : days[weekday])
                       : lcid == 1041 ? japaneseDays[weekday] + (count == 4 ? "曜日" : "")
                                      : koreanDays[weekday] + (count == 4 ? "요일" : "");
            else
                out += namedDate(d, count == 5 ? "short date" : "long date", lcid, firstDay, firstWeek);
        } else if (symbol == 'h' && count <= 2)
            out += display(ampm ? (d.h % 12 ? d.h % 12 : 12) : d.h);
        else if ((symbol == 'n' || symbol == 's') && count <= 2)
            out += display(symbol == 'n' ? d.n : d.s);
        else if (symbol == 'q' && count == 1)
            out += std::to_string((d.m - 1) / 3 + 1);
        else if (symbol == 'w' && count <= 2) {
            auto start = startWeek(d.y);
            if (d.day < start)
                start = startWeek(d.y - 1);
            out += count == 1 ? std::to_string((int(weekday) - (firstDay - 1) + 7) % 7 + 1)
                              : std::to_string((d.day - start).count() / 7 + 1);
        } else if (symbol == 'c' && count == 1)
            out += namedDate(d, "general date", lcid, firstDay, firstWeek);
        else if (symbol == 't' && count == 5)
            out += namedDate(d, "long time", lcid, firstDay, firstWeek);
        else if (symbol == '/')
            out += lcid == 1042 ? "-" : "/";
        else if ((symbol >= 'a' && symbol <= 'z') || std::string("0#@&<>!%").find(symbol) != std::string::npos)
            invalid("미지원 날짜 토큰입니다. 리터럴은따옴표/역슬래시로 감싸세요.");
        else {
            out += literal(Mask(mask.begin() + i, mask.begin() + end));
            i = end;
            continue;
        }
        previous = symbol;
        i = end;
    }
    return out;
}
std::string stringFormat(const std::string &s, const Mask &mask) {
    auto points = decode(s);
    const bool left = std::any_of(mask.begin(), mask.end(), [](const auto &t) { return t.symbol == '!'; });
    int count = 0, mode = 0;
    for (const auto &t : mask) {
        count += t.symbol == '@' || t.symbol == '&';
        if (t.symbol == '<' || t.symbol == '>') {
            if (mode && mode != (t.symbol == '>' ? 1 : 2))
                invalid("문자열 대소문자 방향 충돌");
            mode = t.symbol == '>' ? 1 : 2;
        }
    }
    Points out;
    std::size_t at = 0;
    auto remaining = count;
    for (const auto &t : mask) {
        if (t.symbol == '@' || t.symbol == '&') {
            if (!left && remaining > int(points.size() - at)) {
                if (t.symbol == '@')
                    out.push_back(32);
            } else if (at < points.size())
                out.push_back(points[at++]);
            else if (t.symbol == '@')
                out.push_back(32);
            --remaining;
            if (!remaining && !left) {
                out.insert(out.end(), points.begin() + at, points.end());
                at = points.size();
            }
        } else if (t.symbol != '<' && t.symbol != '>' && t.symbol != '!') {
            if (t.symbol && std::string("0#.").find(t.symbol) != std::string::npos)
                invalid("문자열/숫자 형식 혼합은 지원하지 않습니다.");
            const auto lit = decode(t.text);
            out.insert(out.end(), lit.begin(), lit.end());
        }
    }
    if (!count && (mode || left)) {
        out.insert(out.end(), points.begin(), points.end());
        at = points.size();
    }
    if (left && at < points.size())
        invalid("! 문자열 형식의 자리표시자보다 입력이 깁니다.");
    return encode(mode ? casing(out, mode) : out);
}
} // namespace
std::string xmlStrConv(const std::string &s, int flags, int lcid) {
    calculationCheckpoint();
    profile(lcid);
    if (s.size() > 4096 || flags < 0 || flags > 63 || ((flags & 12) == 12) || ((flags & 48) == 48))
        invalid("문자 변환 플래그/문자열 한도 오류; 코드페이지64/128은 지원하지 않습니다.");
    if ((flags & 12) && lcid == 1033)
        invalid("전각/반각은LCID1041/1042가 필요합니다.");
    if ((flags & 48) && lcid != 1041)
        invalid("가나 변환은LCID1041이 필요합니다.");
    auto points = decode(s);
    if (flags & 3)
        points = casing(points, flags & 3);
    const auto &u = unicode();
    if (flags & 4) {
        Points wide;
        for (auto cp : points) {
            if (const auto it = u.wide.find(cp); it != u.wide.end())
                cp = it->second;
            const auto compose = wide.empty() ? u.compose.end() : u.compose.find({wide.back(), cp});
            if (compose != u.compose.end())
                wide.back() = compose->second;
            else
                wide.push_back(cp);
        }
        points = std::move(wide);
    }
    if (flags & 48)
        for (auto &cp : points) {
            if ((flags & 16) && ((cp >= 0x3041 && cp <= 0x3096) || (cp >= 0x309d && cp <= 0x309e)))
                cp += 0x60;
            else if ((flags & 32) && ((cp >= 0x30a1 && cp <= 0x30f6) || (cp >= 0x30fd && cp <= 0x30fe)))
                cp -= 0x60;
        }
    if (flags & 8) {
        Points narrow;
        for (auto cp : points) {
            if (const auto it = u.narrow.find(cp); it != u.narrow.end())
                narrow.push_back(it->second);
            else if (const auto d = u.decompose.find(cp);
                     d != u.decompose.end() && u.narrow.contains(d->second[0]) && u.narrow.contains(d->second[1])) {
                narrow.push_back(u.narrow.at(d->second[0]));
                narrow.push_back(u.narrow.at(d->second[1]));
            } else
                narrow.push_back(cp);
        }
        points = std::move(narrow);
    }
    return encode(points);
}
std::optional<std::string> xmlFormat(const XmlFormatInput &v, const std::string &mask, int lcid, int firstDay,
                                     int firstWeek) {
    calculationCheckpoint();
    profile(lcid);
    if (firstDay < 0 || firstDay > 7 || firstWeek < 0 || firstWeek > 3)
        invalid("주 시작일0~7·첫 주0~3 범위 오류");
    if (!firstDay)
        firstDay = 1;
    if (!firstWeek)
        firstWeek = 1;
    auto sections = tokenize(mask);
    const auto name = lower(mask);
    if (v.kind == XmlFormatKind::Null) {
        if (sections.size() == 4)
            return literal(sections[3]);
        if (sections.size() == 2 && std::any_of(sections[0].begin(), sections[0].end(), [](const auto &t) {
                return t.symbol && std::string("@&<>!").find(t.symbol) != std::string::npos;
            }))
            return stringFormat("", sections[1]);
        return {};
    }
    if (v.kind == XmlFormatKind::Text) {
        if (sections.size() > 2)
            invalid("문자열 형식은최대2개 구간입니다.");
        if (mask.empty())
            return encode(decode(v.text));
        if (v.text.empty())
            return sections.size() > 1 ? stringFormat("", sections[1]) : "";
        return stringFormat(v.text, sections[0]);
    }
    if (v.kind == XmlFormatKind::Date) {
        if (sections.size() != 1)
            invalid("날짜 형식의 여러 구간은 지원하지 않습니다.");
        const auto d = civil(v.text);
        const bool named = name == "general date" || name == "long date" || name == "medium date" ||
                           name == "short date" || name == "long time" || name == "medium time" || name == "short time";
        return encode(decode(mask.empty() || named
                                 ? namedDate(d, mask.empty() ? "general date" : name, lcid, firstDay, firstWeek)
                                 : dateFormat(d, sections[0], lcid, firstDay, firstWeek)));
    }
    const auto n = v.scaled;
    if (n < -9000000000000000000LL || n > 9000000000000000000LL)
        invalid("native 숫자 범위 초과");
    if (name == "general date" || name == "long date" || name == "medium date" || name == "short date" ||
        name == "long time" || name == "medium time" || name == "short time")
        invalid("날짜 표시 형식에는Date 값이 필요합니다.");
    if (name == "yes/no" || name == "true/false" || name == "on/off")
        return name == "yes/no" ? (n ? "Yes" : "No") : name == "on/off" ? (n ? "On" : "Off") : (n ? "True" : "False");
    if (mask.empty() || name == "general number") {
        if (mask.empty() && v.kind == XmlFormatKind::Boolean)
            return n ? "True" : "False";
        auto result = numeric(n, tokenize("0.######")[0], true);
        if (result.ends_with('.'))
            result.pop_back();
        return result;
    }
    if (name == "fixed" || name == "standard" || name == "percent" || name == "scientific" || name == "currency") {
        const auto code = name == "fixed"        ? "0.00"
                          : name == "standard"   ? "#,##0.00"
                          : name == "percent"    ? "0.00%"
                          : name == "scientific" ? "0.00E+00"
                          : lcid == 1033         ? "\"$\"#,##0.00;(\"$\"#,##0.00)"
                          : lcid == 1041         ? "\"¥\"#,##0;-\"¥\"#,##0"
                                                 : "\"₩\"#,##0;-\"₩\"#,##0";
        return xmlFormat(v, code, lcid, firstDay, firstWeek);
    }
    auto index = n < 0 && sections.size() >= 2 && !sections[1].empty()    ? 1u
                 : n == 0 && sections.size() >= 3 && !sections[2].empty() ? 2u
                                                                          : 0u;
    return encode(decode(numeric(n, sections[index], index == 0)));
}
} // namespace ppm
