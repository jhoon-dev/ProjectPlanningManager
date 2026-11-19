#include "domain/XmlTimeZone.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>
namespace ppm {
namespace {
using N = std::int64_t;
constexpr N second = 1000000, namedFinish = 4133980800LL * second;
using namespace std::chrono;
constexpr auto firstDate = sys_days{year{1} / 1 / 1};
constexpr auto lastDate = sys_days{year{10000} / 1 / 1};
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML 시간대: " + s});
}
std::string pad(int n, int w = 2) {
    const auto s = std::to_string(n);
    return std::string(std::max(0, w - int(s.size())), '0') + s;
}
N instant(const std::string &raw) {
    std::string s;
    try {
        s = normalizeFieldDateTime(raw.size() == 10 ? raw + "T00:00:00" : raw);
    } catch (const AppError &) {
        invalid("오프셋 없는 ISO 날짜/일시가 필요합니다. 명시 오프셋 문자열은 PPMParseUtc를 사용하세요.");
    }
    const auto d = sys_days{year{std::stoi(s.substr(0, 4))} / unsigned(std::stoi(s.substr(5, 2))) /
                            unsigned(std::stoi(s.substr(8, 2)))};
    N fraction = 0;
    if (s.size() > 19) {
        auto f = s.substr(20);
        f.append(6 - f.size(), '0');
        fraction = std::stoll(f);
    }
    return duration_cast<microseconds>(d.time_since_epoch()).count() +
           (std::stoi(s.substr(11, 2)) * 3600 + std::stoi(s.substr(14, 2)) * 60 + std::stoi(s.substr(17, 2))) * second +
           fraction;
}
std::string stamp(N n) {
    const auto t = sys_time<microseconds>{microseconds{n}};
    if (t < firstDate || t >= lastDate)
        invalid("변환한 날짜가 native의1~9999년 범위를 벗어납니다.");
    const auto day = floor<days>(t);
    const auto ymd = year_month_day{day};
    auto us = (t - day).count();
    const auto sec = us / second;
    std::string result = pad(int(ymd.year()), 4) + "-" + pad(unsigned(ymd.month())) + "-" + pad(unsigned(ymd.day())) +
                         "T" + pad(int(sec / 3600)) + ":" + pad(int(sec / 60 % 60)) + ":" + pad(int(sec % 60));
    if (us % second) {
        auto f = pad(int(us % second), 6);
        while (f.back() == '0')
            f.pop_back();
        result += "." + f;
    }
    return result;
}
int fixedOffset(std::string_view s) {
    if (s == "UTC" || s == "Z")
        return 0;
    if (s.starts_with("UTC"))
        s.remove_prefix(3);
    if (s.size() != 6 || (s[0] != '+' && s[0] != '-') || s[3] != ':' ||
        std::any_of(s.begin() + 1, s.end(), [](char c) { return c != ':' && (c < '0' || c > '9'); }))
        invalid("고정 시간대는 UTC/Z 또는 ±HH:MM입니다. PC 기본 시간대는 사용하지 않습니다.");
    // The colon is permitted only at index3, not in numeric positions.
    for (int i : {1, 2, 4, 5})
        if (s[i] < '0' || s[i] > '9')
            invalid("오프셋 숫자를 확인하세요.");
    const auto h = (s[1] - '0') * 10 + s[2] - '0', m = (s[4] - '0') * 10 + s[5] - '0';
    if (h > 14 || m > 59 || (h == 14 && m) || s == "-00:00")
        invalid("오프셋은 ±14:00 이내이며 알 수 없는 오프셋 -00:00은 허용하지 않습니다.");
    return (h * 3600 + m * 60) * (s[0] == '-' ? -1 : 1);
}
struct Pattern {
    std::vector<std::pair<N, int>> entries;
    std::set<int> offsets;
};
struct Data {
    std::vector<Pattern> patterns;
    std::map<std::string, std::size_t> zones;
    Data() {
        // Avoid evaluating strlen over the entire generated database at compile time.
        static const std::string_view lines[] = {
#include "../../resources/timezones/tzdb-2026f.inc"
        };
        for (const auto &line : lines) {
            calculationCheckpoint();
            if (line.empty())
                continue;
            std::istringstream row{std::string(line)};
            char kind = 0;
            row >> kind;
            if (kind == 'P') {
                std::size_t id = 0;
                std::string values;
                row >> id >> values;
                if (id != patterns.size() || values.empty())
                    invalid("내장 시간대 자료를 확인하세요.");
                Pattern p;
                std::size_t at = 0;
                while (at < values.size()) {
                    const auto colon = values.find(':', at), comma = values.find(',', at);
                    const auto end = comma == std::string::npos ? values.size() : comma;
                    N t = 0;
                    int offset = 0;
                    if (colon == std::string::npos || colon >= end)
                        invalid("내장 전환 자료를 확인하세요.");
                    const auto a = std::from_chars(values.data() + at, values.data() + colon, t);
                    const auto b = std::from_chars(values.data() + colon + 1, values.data() + end, offset);
                    if (a.ec != std::errc{} || b.ec != std::errc{} || a.ptr != values.data() + colon ||
                        b.ptr != values.data() + end || t < 0 || t >= namedFinish / second ||
                        (!p.entries.empty() && t * second <= p.entries.back().first) || offset < -86400 ||
                        offset > 86400)
                        invalid("내장 전환 자료가 유효하지 않습니다.");
                    p.entries.emplace_back(t * second, offset);
                    p.offsets.insert(offset);
                    at = end + 1;
                }
                if (p.entries.empty() || p.entries.front().first != 0)
                    invalid("내장 시간대 시작 경계를 확인하세요.");
                patterns.push_back(std::move(p));
            } else if (kind == 'Z') {
                std::string name;
                std::size_t id = 0;
                row >> name >> id;
                if (name.empty() || id >= patterns.size() || !zones.emplace(name, id).second)
                    invalid("내장 시간대 연결을 확인하세요.");
            } else
                invalid("내장 시간대 자료 형식을 확인하세요.");
        }
        if (zones.size() != 597 || patterns.size() != 306)
            invalid("내장2026f 시간대 자료가 불완전합니다.");
    }
};
const Data &data() {
    static const Data value;
    return value;
}
struct Zone {
    const Pattern *pattern = nullptr;
    int fixed = 0;
    std::optional<int> at(N utc) const {
        if (!pattern)
            return fixed;
        if (utc < 0 || utc >= namedFinish)
            return {};
        const auto it = std::upper_bound(pattern->entries.begin(), pattern->entries.end(), utc,
                                         [](N t, const auto &e) { return t < e.first; });
        return std::prev(it)->second;
    }
};
Zone zone(const std::string &name) {
    calculationCheckpoint();
    if (name.starts_with("2026f:")) {
        const auto &d = data();
        const auto it = d.zones.find(name.substr(6));
        if (it == d.zones.end())
            invalid("없는2026f IANA 시간대입니다: " + name);
        return {&d.patterns.at(it->second), 0};
    }
    if (name.find('/') != std::string::npos || (name.find(':') != std::string::npos && !name.starts_with("UTC") &&
                                                !name.starts_with('+') && !name.starts_with('-')))
        invalid("IANA 시간대는 버전을 포함한2026f:Asia/Seoul 형식으로 지정하세요.");
    return {nullptr, fixedOffset(name)};
}
N resolve(N local, const Zone &z, const std::string &policy) {
    if (policy != "reject" && policy != "earlier" && policy != "later")
        invalid("중복 시각 정책은 reject/earlier/later입니다.");
    std::set<N> candidates;
    const auto offsets = z.pattern ? z.pattern->offsets : std::set<int>{z.fixed};
    bool inRange = false;
    for (const auto offset : offsets) {
        const auto utc = local - N(offset) * second;
        const auto actual = z.at(utc);
        if (!actual)
            continue;
        inRange = true;
        if (*actual == offset)
            candidates.insert(utc);
    }
    if (candidates.empty())
        invalid(inRange ? "전환으로 존재하지 않는 현지 시각입니다. 자동 보정하지 않습니다."
                        : "IANA 자료의 UTC 지원 범위1970-01-01~2101-01-01(끝 제외)를 벗어납니다.");
    if (candidates.size() > 1 && policy == "reject")
        invalid("중복된 현지 시각입니다. earlier 또는 later를 명시 선택하세요.");
    const auto utc = policy == "later" ? *candidates.rbegin() : *candidates.begin();
    (void)stamp(utc);
    return utc;
}
XmlZonedTime atUtc(N utc, const Zone &z) {
    const auto offset = z.at(utc);
    if (!offset)
        invalid("IANA 자료의 UTC 지원 범위1970~2100년을 벗어납니다.");
    return {stamp(utc + N(*offset) * second), *offset};
}
} // namespace
XmlZonedTime xmlTimeZoneAtUtc(const std::string &utc, const std::string &name) {
    return atUtc(instant(utc), zone(name));
}
std::string xmlTimeZoneConvert(const std::string &civil, const std::string &from, const std::string &to,
                               const std::string &policy) {
    const auto source = zone(from), target = zone(to);
    return atUtc(resolve(instant(civil), source, policy), target).civil;
}
std::string xmlTimeZoneText(const std::string &civil, const std::string &from, const std::string &to,
                            const std::string &policy) {
    const auto source = zone(from), target = zone(to);
    const auto value = atUtc(resolve(instant(civil), source, policy), target);
    if (value.offsetSeconds % 60)
        invalid("역사적 초 단위 오프셋은 ISO ±HH:MM으로 정확히 쓸 수 없습니다. UTC 또는 날짜 변환을 사용하세요.");
    const auto a = std::abs(value.offsetSeconds);
    return value.civil +
           (a == 0 ? "Z" : std::string(value.offsetSeconds < 0 ? "-" : "+") + pad(a / 3600) + ":" + pad(a / 60 % 60));
}
int xmlTimeZoneOffset(const std::string &civil, const std::string &name, const std::string &policy) {
    const auto z = zone(name);
    const auto utc = resolve(instant(civil), z, policy);
    return *z.at(utc);
}
std::string xmlParseUtc(const std::string &raw) {
    calculationCheckpoint();
    if (raw.size() < 20 || raw.size() > 32 || raw[10] != 'T' || raw[16] != ':')
        invalid("명시 Z 또는 ±HH:MM이 있는 완전한 ISO 일시가 필요합니다.");
    const bool z = raw.back() == 'Z';
    const auto width = z ? 1u : 6u;
    const auto civil = raw.substr(0, raw.size() - width);
    const auto offset = z ? 0 : fixedOffset(raw.substr(raw.size() - width));
    if (civil.size() < 19)
        invalid("초를 포함한 ISO 일시가 필요합니다.");
    return stamp(instant(civil) - N(offset) * second);
}
} // namespace ppm
