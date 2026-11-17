#include "domain/XmlCalendar.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
namespace jobs {
namespace {
using namespace std::chrono;
using N = std::int64_t;
constexpr N minute = 60000000, day = minute * 1440, span = day * 366 * 20;
constexpr auto epoch = sys_days{year{1} / 1 / 1};
constexpr auto last = sys_days{year{10000} / 1 / 1};
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"근무 달력: " + s});
}
void token(std::string &key, const std::string &s) {
    key += std::to_string(s.size()) + ":" + s;
}
std::string folded(std::string s) {
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    return s;
}
sys_days date(const std::string &s) {
    return sys_days{year{std::stoi(s.substr(0, 4))} / unsigned(std::stoi(s.substr(5, 2))) /
                    unsigned(std::stoi(s.substr(8, 2)))};
}
N instant(const std::string &raw) {
    const auto s = normalizeFieldDateTime(raw.size() == 10 ? raw + "T00:00:00" : raw);
    N fraction = 0;
    if (s.size() > 19) {
        auto f = s.substr(20);
        f.append(6 - f.size(), '0');
        fraction = std::stoll(f);
    }
    return duration_cast<microseconds>(date(s).time_since_epoch()).count() + std::stoi(s.substr(11, 2)) * minute * 60 +
           std::stoi(s.substr(14, 2)) * minute + std::stoi(s.substr(17, 2)) * 1000000 + fraction;
}
std::string stamp(N n) {
    const auto t = sys_time<microseconds>{microseconds{n}};
    auto d = floor<days>(t);
    auto ymd = year_month_day{d};
    if (int(ymd.year()) < 1 || int(ymd.year()) > 9999)
        invalid("지원 날짜 범위를 벗어났습니다.");
    auto us = (t - d).count();
    auto minutes = us / minute;
    auto seconds = (us % minute) / 1000000;
    auto fraction = us % 1000000;
    char out[48];
    std::snprintf(out, sizeof(out), "%04d-%02u-%02uT%02lld:%02lld:%02lld.%06lld", int(ymd.year()),
                  unsigned(ymd.month()), unsigned(ymd.day()), static_cast<long long>(minutes / 60),
                  static_cast<long long>(minutes % 60), static_cast<long long>(seconds),
                  static_cast<long long>(fraction));
    return normalizeFieldDateTime(out);
}
void calendarKey(std::string &key, const TaskCalendar &c) {
    token(key, c.id);
    token(key, c.name);
    token(key, "weekdays");
    for (auto d : c.weekdays)
        token(key, std::to_string(d));
    token(key, "periods");
    for (auto p : c.periods) {
        token(key, std::to_string(p.begin));
        token(key, std::to_string(p.end));
    }
    token(key, "holidays");
    for (const auto &d : c.holidays)
        token(key, d);
}
struct Pattern {
    struct Period {
        N begin, end, prefix;
    };
    std::array<bool, 7> weekdays{};
    std::array<int, 8> weekPrefix{};
    std::vector<Period> periods;
    std::vector<N> holidays;
    N daily = 0;
    explicit Pattern(const TaskCalendar &c) {
        for (auto d : c.weekdays) {
            if (d < 1 || d > 7 || weekdays[d - 1])
                invalid("요일 정의를 확인하세요.");
            weekdays[d - 1] = true;
        }
        for (int i = 0; i < 7; ++i)
            weekPrefix[i + 1] = weekPrefix[i] + int(weekdays[i]);
        int end = -1;
        for (auto p : c.periods) {
            calculationCheckpoint();
            if (p.begin < 0 || p.end > 1440 || p.begin >= p.end || p.begin < end)
                invalid("근무 구간 정의를 확인하세요.");
            periods.push_back({p.begin * minute, p.end * minute, daily});
            daily += (p.end - p.begin) * minute;
            end = p.end;
        }
        if (!daily || !weekPrefix[7])
            invalid("근무 요일/구간이 없습니다.");
        for (const auto &h : c.holidays) {
            calculationCheckpoint();
            if (!validDate(h))
                invalid("휴일 날짜를 확인하세요.");
            const auto d = date(h);
            if (weekdays[weekday{d}.iso_encoding() - 1])
                holidays.push_back((d - epoch).count());
        }
        std::sort(holidays.begin(), holidays.end());
        holidays.erase(std::unique(holidays.begin(), holidays.end()), holidays.end());
    }
    N prefix(N n) const {
        calculationCheckpoint();
        auto t = sys_time<microseconds>{microseconds{n}};
        auto d = floor<days>(t);
        auto days = (d - epoch).count();
        const auto h = std::lower_bound(holidays.begin(), holidays.end(), days);
        N result = (days / 7 * weekPrefix[7] + weekPrefix[days % 7]) * daily - (h - holidays.begin()) * daily;
        if (!weekdays[weekday{d}.iso_encoding() - 1] || (h != holidays.end() && *h == days))
            return result;
        auto civil = (t - d).count();
        auto i =
            std::upper_bound(periods.begin(), periods.end(), civil, [](auto v, const auto &p) { return v < p.begin; });
        if (i != periods.begin()) {
            --i;
            result += i->prefix + std::min(civil - i->begin, i->end - i->begin);
        }
        return result;
    }
};
std::shared_ptr<const Pattern> pattern(const PlanInput &p, FieldScope scope, const Task *task, const Resource *resource,
                                       const std::string &name) {
    TaskCalendar standard;
    standard.holidays = p.holidays;
    const TaskCalendar *selected = &standard;
    if (name.empty()) {
        const std::string id = (scope == FieldScope::Task || scope == FieldScope::Assignment)
                                   ? (task ? task->calendarId : p.xmlFormulaCalendarId)
                               : scope == FieldScope::Resource
                                   ? (resource ? resource->calendarId : p.xmlFormulaCalendarId)
                                   : p.xmlFormulaCalendarId;
        if (!id.empty()) {
            auto i = std::find_if(p.calendars.begin(), p.calendars.end(), [&](const auto &c) { return c.id == id; });
            if (i == p.calendars.end())
                invalid("기본 달력이 없습니다: " + id);
            selected = &*i;
        }
    } else {
        std::size_t count = 0;
        for (const auto &c : p.calendars)
            if (c.name == name) {
                selected = &c;
                ++count;
            }
        if (!count && name == "Standard")
            for (const auto &c : p.calendars)
                if (c.name == "PPM standard") {
                    selected = &c;
                    ++count;
                }
        if (count > 1)
            invalid("같은 이름의 달력이 여러 개입니다: " + name);
        if (!count && name != "Standard" && name != "PPM standard" && name != p.calendar)
            invalid("이름에 해당하는 달력이 없습니다: " + name);
    }
    std::string key = "xml-calendar-pattern/v1/";
    calendarKey(key, *selected);
    if (auto cached = evaluationCached(key))
        return std::static_pointer_cast<const Pattern>(cached);
    auto value = std::make_shared<const Pattern>(*selected);
    evaluationRemember(std::move(key), value);
    return value;
}
} // namespace
bool xmlCalendarFormula(const XmlFormula &f) {
    // Lex identifiers outside strings, date literals and escaped field references. No substring matches.
    bool quoted = false, date = false, ref = false;
    for (std::size_t i = 0; i < f.source.size(); ++i) {
        auto c = f.source[i];
        if (ref) {
            if (c == '\\' && i + 1 < f.source.size())
                ++i;
            else if (c == ']')
                ref = false;
            continue;
        }
        if (quoted) {
            if (c == '"') {
                if (i + 1 < f.source.size() && f.source[i + 1] == '"')
                    ++i;
                else
                    quoted = false;
            }
            continue;
        }
        if (date) {
            if (c == '#')
                date = false;
            continue;
        }
        if (c == '"') {
            quoted = true;
            continue;
        }
        if (c == '#') {
            date = true;
            continue;
        }
        if (c == '[') {
            ref = true;
            continue;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
            auto begin = i;
            while (i + 1 < f.source.size() &&
                   ((f.source[i + 1] >= 'A' && f.source[i + 1] <= 'Z') ||
                    (f.source[i + 1] >= 'a' && f.source[i + 1] <= 'z') ||
                    (f.source[i + 1] >= '0' && f.source[i + 1] <= '9') || f.source[i + 1] == '_'))
                ++i;
            auto fn = folded(f.source.substr(begin, i - begin + 1));
            auto at = i + 1;
            while (at < f.source.size() && std::isspace(static_cast<unsigned char>(f.source[at])))
                ++at;
            if ((fn == "projdateadd" || fn == "projdatesub" || fn == "projdatediff") && at < f.source.size() &&
                f.source[at] == '(')
                return true;
        }
    }
    return false;
}
bool hasXmlCalendarFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlCalendarFormula(*f.xmlFormula))
                return true;
    return false;
}
std::string xmlCalendarInputKey(const PlanInput &p, const Task *t, const Resource *r, const Assignment *a) {
    if (!hasXmlCalendarFunctions(p, true))
        return "";
    std::string key = "xml-calendar-input/v1/";
    token(key, p.calendar);
    token(key, p.xmlFormulaCalendarId);
    for (const auto &h : p.holidays)
        token(key, h);
    token(key, "calendars");
    for (const auto &c : p.calendars)
        calendarKey(key, c);
    if (a) {
        if (!t) {
            auto i = std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &v) { return v.id == a->taskId; });
            if (i != p.tasks.end())
                t = &*i;
        }
        if (!r) {
            auto i = std::find_if(p.resources.begin(), p.resources.end(),
                                  [&](const auto &v) { return v.id == a->resourceId; });
            if (i != p.resources.end())
                r = &*i;
        }
    }
    token(key, t ? t->calendarId : "");
    token(key, r ? r->calendarId : "");
    return key;
}
N xmlCalendarDifference(const PlanInput &p, FieldScope scope, const Task *t, const Resource *r, const std::string &name,
                        const std::string &from, const std::string &to) {
    EvaluationScope evaluation;
    auto a = instant(from), b = instant(to);
    if (std::abs(b - a) > span)
        invalid("차이는 최대20년 범위입니다.");
    auto cal = pattern(p, scope, t, r, name);
    auto us = cal->prefix(b) - cal->prefix(a);
    return us < 0 ? -((-us + 30) / 60) : (us + 30) / 60;
}
std::string xmlCalendarShift(const PlanInput &p, FieldScope scope, const Task *t, const Resource *r,
                             const std::string &name, const std::string &from, N minutes, bool elapsed) {
    EvaluationScope evaluation;
    calculationCheckpoint();
    if (minutes > span / 60 || minutes < -span / 60)
        invalid("이동은 최대20년 범위입니다.");
    auto start = instant(from);
    auto cal = pattern(p, scope, t, r, name);
    if (!minutes)
        return stamp(start);
    const N first = duration_cast<microseconds>(epoch.time_since_epoch()).count(),
            end = duration_cast<microseconds>(last.time_since_epoch()).count() - 1;
    if (elapsed) {
        auto out = start + minutes * 60;
        if (out < first || out > end)
            invalid("이동 결과의 날짜 범위를 벗어났습니다.");
        return stamp(out);
    }
    auto target = cal->prefix(start) + minutes * 60;
    N lo = minutes > 0 ? start : std::max(first, start - span), hi = minutes > 0 ? std::min(end, start + span) : start;
    if (cal->prefix(lo) > target || cal->prefix(hi) < target)
        invalid("20년 안에 필요한 근무시간이 없습니다.");
    if (minutes > 0) {
        while (lo < hi) {
            auto mid = lo + (hi - lo) / 2;
            if (cal->prefix(mid) < target)
                lo = mid + 1;
            else
                hi = mid;
        }
    } else {
        while (lo < hi) {
            auto mid = lo + (hi - lo + 1) / 2;
            if (cal->prefix(mid) > target)
                hi = mid - 1;
            else
                lo = mid;
        }
    }
    return stamp(lo);
}
} // namespace jobs
