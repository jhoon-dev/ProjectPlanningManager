#include "domain/ResourceAvailability.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
namespace ppm {
namespace {
std::string shift(const std::string &s, int count) {
    using namespace std::chrono;
    // MSVC's standard minutes use a 32-bit rep, too small for year 9999.
    using Minutes = duration<std::int64_t, minutes::period>;
    auto value = sys_days{year{std::stoi(s.substr(0, 4))} / unsigned(std::stoi(s.substr(5, 2))) /
                          unsigned(std::stoi(s.substr(8, 2)))} +
                 Minutes{std::stoi(s.substr(11, 2)) * 60 + std::stoi(s.substr(14, 2)) + count};
    const auto day = floor<days>(value);
    const year_month_day ymd{day};
    if (int(ymd.year()) > 9999 || int(ymd.year()) < 1)
        return {};
    const auto time = duration_cast<Minutes>(value - day).count();
    char out[32];
    std::snprintf(out, sizeof(out), "%04d-%02u-%02u %02d:%02d", int(ymd.year()), unsigned(ymd.month()),
                  unsigned(ymd.day()), int(time / 60), int(time % 60));
    return out;
}
} // namespace
bool hasResourceAvailability(const Resource &r) {
    return r.availableFrom || r.availableTo || !r.availability.empty();
}
void validateResourceAvailability(const Resource &r, int maxUnits) {
    const auto fail = [&] {
        throw AppError(ErrorCode::RESOURCE_EXCEPTION_INVALID, {}, ErrorContext{.resourceIds = {r.id}});
    };
    if ((r.availableFrom && !validStamp(*r.availableFrom)) || (r.availableTo && !validStamp(*r.availableTo)) ||
        (r.availableFrom && r.availableTo && *r.availableFrom > *r.availableTo) || r.availability.size() > 10000)
        fail();
    std::vector<const AvailabilityPeriod *> sorted;
    for (const auto &v : r.availability) {
        calculationCheckpoint();
        if ((v.from && !validStamp(*v.from)) || (v.to && !validStamp(*v.to)) || (v.from && v.to && *v.from > *v.to) ||
            (v.units && (*v.units < 0 || *v.units > maxUnits)))
            fail();
        sorted.push_back(&v);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto *a, const auto *b) { return a->from < b->from; });
    for (std::size_t i = 1; i < sorted.size(); ++i)
        if (!sorted[i - 1]->to || !sorted[i]->from || *sorted[i - 1]->to >= *sorted[i]->from)
            fail();
}
std::optional<int> resourceUnitsAt(const Resource &r, const std::string &at) {
    if (!validStamp(at))
        throw AppError(ErrorCode::CIVIL_TIMESTAMP_INVALID);
    if ((r.availableFrom && at < *r.availableFrom) || (r.availableTo && at > *r.availableTo))
        return 0;
    for (const auto &day : r.exceptions) {
        calculationCheckpoint();
        if (day.date == at.substr(0, 10))
            return day.units;
    }
    for (const auto &v : r.availability) {
        calculationCheckpoint();
        if ((!v.from || at >= *v.from) && (!v.to || at <= *v.to))
            return v.units;
    }
    return r.maxUnits;
}
std::vector<std::string> resourceCapacityChanges(const Resource &r) {
    std::vector<std::string> out;
    const auto add = [&](const std::optional<std::string> &from, const std::optional<std::string> &to) {
        if (from)
            out.push_back(*from);
        if (to) {
            auto next = shift(*to, 1);
            if (!next.empty())
                out.push_back(std::move(next));
        }
    };
    add(r.availableFrom, r.availableTo);
    for (const auto &v : r.availability)
        add(v.from, v.to);
    for (const auto &day : r.exceptions)
        add(day.date + " 00:00", day.date + " 23:59");
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
std::vector<AvailabilityPeriod> resourceAvailabilityIntervals(const Resource &r) {
    std::vector<AvailabilityPeriod> result;
    std::map<std::string, const AvailabilityPeriod *> periods;
    std::map<std::string, std::optional<int>> days;
    for (const auto &v : r.availability)
        periods[v.from.value_or("")] = &v;
    for (const auto &v : r.exceptions)
        days[v.date] = v.units;
    const auto valueAt = [&](const std::string &at) -> std::optional<int> {
        if ((r.availableFrom && at < *r.availableFrom) || (r.availableTo && at > *r.availableTo))
            return 0;
        if (const auto day = days.find(at.substr(0, 10)); day != days.end())
            return day->second;
        auto it = periods.upper_bound(at);
        if (it != periods.begin()) {
            const auto &v = *std::prev(it)->second;
            if (!v.to || at <= *v.to)
                return v.units;
        }
        return r.maxUnits;
    };
    std::optional<std::string> from;
    const auto append = [&](const std::optional<std::string> &to) {
        calculationCheckpoint();
        const auto units = valueAt(from.value_or("0001-01-01 00:00"));
        if (!result.empty() && result.back().units == units)
            result.back().to = to;
        else
            result.push_back({from, to, units});
    };
    for (const auto &at : resourceCapacityChanges(r)) {
        const auto before = shift(at, -1);
        if (!before.empty())
            append(before);
        from = at;
    }
    append({});
    return result;
}
} // namespace ppm
