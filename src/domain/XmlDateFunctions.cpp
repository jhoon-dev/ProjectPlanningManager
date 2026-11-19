#include "domain/XmlDateFunctions.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <chrono>
#include <charconv>
namespace ppm {
namespace {
using namespace std::chrono;
using N = std::int64_t;
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML 날짜 함수: " + s});
}
std::string intervalKey(std::string s) {
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    if (s != "yyyy" && s != "q" && s != "m" && s != "y" && s != "d" && s != "w" && s != "ww" && s != "h" && s != "n" &&
        s != "s")
        invalid("간격은yyyy/q/m/y/d/w/ww/h/n/s입니다.");
    return s;
}
void controls(int &firstDay, int &firstWeek) {
    if (firstDay < 0 || firstDay > 7 || firstWeek < 0 || firstWeek > 3)
        invalid("주 시작일0~7·첫 주0~3 범위 오류");
    if (!firstDay)
        firstDay = 1;
    if (!firstWeek)
        firstWeek = 1;
}
std::string pad(int n, int width = 2) {
    auto s = std::to_string(n);
    return std::string(std::max(0, width - int(s.size())), '0') + s;
}
struct Civil {
    std::string iso;
    int y, m, d, h, n, s;
    sys_days day;
    N micros;
};
Civil parse(const std::string &raw) {
    auto s = normalizeFieldDateTime(raw.size() == 10 ? raw + "T00:00:00" : raw);
    Civil c{s,
            std::stoi(s.substr(0, 4)),
            std::stoi(s.substr(5, 2)),
            std::stoi(s.substr(8, 2)),
            std::stoi(s.substr(11, 2)),
            std::stoi(s.substr(14, 2)),
            std::stoi(s.substr(17, 2)),
            {},
            0};
    c.day = sys_days(year(c.y) / unsigned(c.m) / unsigned(c.d));
    c.micros =
        duration_cast<microseconds>(c.day.time_since_epoch()).count() + (N(c.h) * 3600 + c.n * 60 + c.s) * 1000000;
    if (s.size() > 19) {
        auto fraction = s.substr(20);
        fraction.append(6 - fraction.size(), '0');
        c.micros += std::stoi(fraction);
    }
    return c;
}
sys_days weekStart(sys_days d, int firstDay) {
    return d - days((int(weekday(d).c_encoding()) - firstDay + 8) % 7);
}
sys_days yearWeekStart(int y, int firstDay, int firstWeek) {
    const auto jan = sys_days(year(y) / 1 / 1);
    auto start = weekStart(jan, firstDay);
    const auto offset = (jan - start).count();
    if ((firstWeek == 2 && offset > 3) || (firstWeek == 3 && offset))
        start += days(7);
    return start;
}
N unit(const std::string &key) {
    return key == "h" ? 3600000000LL : key == "n" ? 60000000LL : key == "s" ? 1000000LL : 86400000000LL;
}
N floorDiv(N n, N d) {
    return n / d - (n < 0 && n % d ? 1 : 0);
}
std::string stamp(N value) {
    const auto min = duration_cast<microseconds>(sys_days(year(1) / 1 / 1).time_since_epoch()).count();
    const auto max = duration_cast<microseconds>(sys_days(year(10000) / 1 / 1).time_since_epoch()).count();
    if (value < min || value >= max)
        invalid("날짜 범위1~9999년 초과");
    const auto t = sys_time<microseconds>(microseconds(value));
    const auto day = floor<days>(t);
    const year_month_day ymd(day);
    auto remainder = (t - day).count();
    auto h = remainder / 3600000000LL;
    remainder %= 3600000000LL;
    auto n = remainder / 60000000LL;
    remainder %= 60000000LL;
    auto s = remainder / 1000000;
    remainder %= 1000000;
    return normalizeFieldDateTime(pad(int(ymd.year()), 4) + '-' + pad(unsigned(ymd.month())) + '-' +
                                  pad(unsigned(ymd.day())) + 'T' + pad(int(h)) + ':' + pad(int(n)) + ':' + pad(int(s)) +
                                  (remainder ? "." + pad(int(remainder), 6) : ""));
}
} // namespace
N xmlDatePart(const std::string &interval, const std::string &date, int firstDay, int firstWeek) {
    calculationCheckpoint();
    controls(firstDay, firstWeek);
    const auto key = intervalKey(interval);
    const auto c = parse(date);
    if (key == "yyyy")
        return c.y;
    if (key == "q")
        return (c.m - 1) / 3 + 1;
    if (key == "m")
        return c.m;
    if (key == "d")
        return c.d;
    if (key == "y")
        return (c.day - sys_days(year(c.y) / 1 / 1)).count() + 1;
    if (key == "w")
        return (int(weekday(c.day).c_encoding()) - firstDay + 8) % 7 + 1;
    if (key == "h")
        return c.h;
    if (key == "n")
        return c.n;
    if (key == "s")
        return c.s;
    // Match the existing Format ww contract: do not renumber late December
    // as next year's week 1; initial dates may belong to the previous year.
    auto start = yearWeekStart(c.y, firstDay, firstWeek);
    if (c.day < start)
        start = yearWeekStart(c.y - 1, firstDay, firstWeek);
    return (c.day - start).count() / 7 + 1;
}
std::string xmlDateAdd(const std::string &interval, N count, const std::string &date) {
    calculationCheckpoint();
    const auto key = intervalKey(interval);
    if (count < -10000000 || count > 10000000)
        invalid("DateAdd 횟수는±10,000,000 정수까지입니다.");
    const auto c = parse(date);
    if (key == "yyyy" || key == "q" || key == "m") {
        const N target = N(c.y - 1) * 12 + c.m - 1 + count * (key == "yyyy" ? 12 : key == "q" ? 3 : 1);
        if (target < 0 || target >= 9999 * 12)
            invalid("날짜 범위1~9999년 초과");
        const int y = int(target / 12) + 1, m = int(target % 12) + 1;
        const auto last = year_month_day_last(year(y), month_day_last(month(unsigned(m))));
        const auto d = std::min(c.d, int(unsigned(last.day())));
        return pad(y, 4) + '-' + pad(m) + '-' + pad(d) + c.iso.substr(10);
    }
    return stamp(c.micros + count * unit(key) * (key == "ww" ? 7 : 1));
}
N xmlDateDifference(const std::string &interval, const std::string &from, const std::string &to, int firstDay,
                    int firstWeek) {
    calculationCheckpoint();
    controls(firstDay, firstWeek);
    const auto key = intervalKey(interval);
    const auto a = parse(from), b = parse(to);
    if (key == "yyyy")
        return b.y - a.y;
    if (key == "q")
        return N(b.y - a.y) * 4 + (b.m - 1) / 3 - (a.m - 1) / 3;
    if (key == "m")
        return N(b.y - a.y) * 12 + b.m - a.m;
    if (key == "w")
        return (b.day - a.day).count() / 7;
    if (key == "ww")
        return (weekStart(b.day, firstDay) - weekStart(a.day, firstDay)).count() / 7;
    // Calendar boundary counts, including instants before the Unix epoch.
    const auto divisor = unit(key);
    return floorDiv(b.micros, divisor) - floorDiv(a.micros, divisor);
}
std::string xmlTimeSerial(N hour, N minute, N second) {
    calculationCheckpoint();
    for (auto value : {hour, minute, second})
        if (value < -32768 || value > 32767)
            invalid("TimeSerial의 각 인수는반올림 후-32768~32767입니다.");
    const N total = hour * 3600 + minute * 60 + second;
    const N day = total / 86400;
    const N remainder = total % 86400;
    // OLE DATE truncates the signed day portion and uses the absolute time
    // fraction. Negative serials must not be treated as a chronological offset.
    const N civilSeconds = day * 86400 + (remainder < 0 ? -remainder : remainder);
    return stamp(parse("1899-12-30").micros + civilSeconds * 1000000);
}
std::string xmlTimeValue(const std::string &raw) {
    calculationCheckpoint();
    if (raw.size() > 4096)
        invalid("TimeValue 입력은4,096 bytes까지입니다.");
    const auto trim = [](const std::string &s) {
        const auto first = s.find_first_not_of(" \t\r\n");
        return first == std::string::npos ? std::string{} : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
    };
    auto clock = trim(raw);
    if (clock.size() >= 10 && clock[4] == '-' && clock[7] == '-') {
        // Check the date before discarding it, including invalid leap days.
        (void)parse(clock.substr(0, 10));
        if (clock.size() == 10)
            return "1899-12-30T00:00:00";
        if (clock[10] != 'T' && clock[10] != ' ')
            invalid("TimeValue의 ISO 날짜와 시각 구분자는T 또는공백입니다.");
        clock = trim(clock.substr(11));
    }
    bool period = false, pm = false;
    if (clock.size() >= 2) {
        const auto suffix = clock.substr(clock.size() - 2);
        const auto a = suffix[0], b = suffix[1];
        if ((a == 'a' || a == 'A' || a == 'p' || a == 'P') && (b == 'm' || b == 'M')) {
            period = true;
            pm = a == 'p' || a == 'P';
            clock = trim(clock.substr(0, clock.size() - 2));
        }
    }
    const auto number = [](const std::string &s) {
        if (s.empty() || s.size() > 2 || std::any_of(s.begin(), s.end(), [](char c) { return c < '0' || c > '9'; }))
            invalid("시·분·초는1~2자리 ASCII 숫자입니다.");
        int n = 0;
        const auto result = std::from_chars(s.data(), s.data() + s.size(), n);
        if (result.ec != std::errc{})
            invalid("시각 숫자 오류");
        return n;
    };
    const auto first = clock.find(':');
    if (first == std::string::npos && !period)
        invalid("TimeValue는H:MM[:SS[.소수6자리]] 또는영문 AM/PM 시각을 받습니다.");
    const auto second = first == std::string::npos ? std::string::npos : clock.find(':', first + 1);
    const auto hour = number(clock.substr(0, first));
    const auto minute =
        first == std::string::npos
            ? 0
            : number(clock.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1));
    int seconds = 0;
    std::string fraction;
    if (second != std::string::npos) {
        auto field = clock.substr(second + 1);
        const auto dot = field.find('.');
        seconds = number(field.substr(0, dot));
        if (dot != std::string::npos) {
            fraction = field.substr(dot + 1);
            if (fraction.empty() || fraction.size() > 6 ||
                std::any_of(fraction.begin(), fraction.end(), [](char c) { return c < '0' || c > '9'; }))
                invalid("TimeValue 소수 초는1~6자리입니다.");
        }
    }
    if (hour > (period ? 12 : 23) || minute > 59 || seconds > 59)
        invalid("TimeValue 시·분·초 범위 오류");
    const auto h = period ? hour % 12 + (pm ? 12 : 0) : hour;
    return normalizeFieldDateTime("1899-12-30T" + pad(h) + ':' + pad(minute) + ':' + pad(seconds) +
                                  (fraction.empty() ? "" : "." + fraction));
}
} // namespace ppm
