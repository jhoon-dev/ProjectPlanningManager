#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
namespace jobs {
// Exact pointwise demand, then pointwise minimum across alternatives. Missing
// resources and gaps contribute zero; bounding envelopes cannot replace this.
template <class Span, class Check> std::vector<Span> normalizeDemand(const std::vector<Span> &spans, Check check) {
    struct Event {
        std::size_t resource;
        std::int64_t at, delta;
    };
    std::vector<Event> events;
    for (const auto &s : spans) {
        check();
        if (s.start < s.finish && s.units > 0) {
            events.push_back({s.resource, s.start, s.units});
            events.push_back({s.resource, s.finish, -std::int64_t(s.units)});
        }
    }
    std::sort(events.begin(), events.end(), [](const auto &a, const auto &b) {
        return a.resource != b.resource ? a.resource < b.resource : a.at < b.at;
    });
    std::vector<Span> result;
    std::int64_t used = 0;
    for (std::size_t i = 0; i < events.size();) {
        const auto resource = events[i].resource;
        const auto at = events[i].at;
        do {
            check();
            used += events[i++].delta;
        } while (i < events.size() && events[i].resource == resource && events[i].at == at);
        if (used < 0 || used > std::numeric_limits<decltype(Span{}.units)>::max())
            throw std::runtime_error("Invalid normalized resource demand");
        if (used && i < events.size() && events[i].resource == resource)
            result.push_back({resource, at, events[i].at, static_cast<decltype(Span{}.units)>(used)});
    }
    return result;
}
template <class Span, class Check>
std::vector<Span> intersectDemand(const std::vector<Span> &a, const std::vector<Span> &b, Check check) {
    std::vector<Span> result;
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        check();
        const auto &x = a[i], &y = b[j];
        if (x.resource < y.resource) {
            ++i;
            continue;
        }
        if (y.resource < x.resource) {
            ++j;
            continue;
        }
        const auto start = std::max(x.start, y.start), finish = std::min(x.finish, y.finish);
        const auto units = std::min(x.units, y.units);
        if (start < finish && units > 0) {
            if (!result.empty() && result.back().resource == x.resource && result.back().finish == start &&
                result.back().units == units)
                result.back().finish = finish;
            else
                result.push_back({x.resource, start, finish, units});
        }
        if (x.finish <= y.finish)
            ++i;
        else
            ++j;
    }
    return result;
}
} // namespace jobs
