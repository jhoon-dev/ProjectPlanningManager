#include "domain/PeriodActuals.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <set>
#include <unordered_map>
namespace ppm {
namespace {
bool label(const std::string &s, std::size_t max = 4096) {
    return !s.empty() && s.size() <= max &&
           std::any_of(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }) &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
void invalid() {
    throw AppError(ErrorCode::PERIOD_ACTUAL_INVALID);
}
} // namespace
PeriodActualKey periodActualKey(const PeriodActualEntry &e) {
    return {e.taskId, e.resourceId, e.date};
}
std::map<PeriodActualKey, const PeriodActualEntry *> periodActualsAsOf(const Project &p, const std::string &stamp) {
    std::map<PeriodActualKey, const PeriodActualEntry *> out;
    for (const auto &e : p.periodActuals) {
        calculationCheckpoint();
        if (e.statusAt > stamp)
            continue;
        auto &current = out[periodActualKey(e)];
        if (!current || e.statusAt >= current->statusAt)
            current = &e;
    }
    return out;
}
std::map<std::string, ActualCostAllocation> actualCostAllocations(const Project &p, const std::string &stamp) {
    std::map<std::string, ActualCostAllocation> out;
    for (const auto &t : p.tasks)
        out[t.id];
    std::map<std::string, const CostEntry *> totals;
    for (const auto &e : p.costEntries)
        if (e.statusAt <= stamp) {
            auto &current = totals[e.taskId];
            if (!current || e.statusAt >= current->statusAt)
                current = &e;
        }
    for (const auto &[task, e] : totals)
        out[task].cumulative = e->actualMinor;
    for (const auto &[key, e] : periodActualsAsOf(p, stamp)) {
        (void)key;
        auto &a = out[e->taskId];
        if (e->actualMinor) {
            if (a.allocated > maxMoney - *e->actualMinor)
                throw AppError(ErrorCode::PERIOD_ACTUAL_BALANCE);
            a.allocated += *e->actualMinor;
            ++a.costReports;
        }
    }
    for (auto &[id, a] : out) {
        (void)id;
        if (a.cumulative) {
            if (*a.cumulative < a.allocated)
                throw AppError(ErrorCode::PERIOD_ACTUAL_BALANCE);
            a.unallocated = *a.cumulative - a.allocated;
        }
    }
    return out;
}
void validatePeriodActuals(const Project &p) {
    if (p.periodActuals.empty())
        return;
    if (p.periodActuals.size() > 50000)
        throw AppError(ErrorCode::PERIOD_ACTUAL_LIMIT);
    std::unordered_map<std::string, const Resource *> resources;
    std::unordered_map<std::string, const CostEntry *> costs;
    std::set<std::string> tasks, ids;
    for (const auto &t : p.tasks)
        tasks.insert(t.id);
    for (const auto &r : p.resources)
        resources.emplace(r.id, &r);
    for (const auto &c : p.costEntries)
        costs.emplace(c.id, &c);
    std::map<PeriodActualKey, const PeriodActualEntry *> previous;
    struct Event {
        std::string stamp;
        std::size_t index;
        bool period;
    };
    std::vector<Event> events;
    for (std::size_t i = 0; i < p.costEntries.size(); ++i)
        events.push_back({p.costEntries[i].statusAt, i, false});
    for (std::size_t i = 0; i < p.periodActuals.size(); ++i) {
        calculationCheckpoint();
        const auto &e = p.periodActuals[i];
        const auto key = periodActualKey(e);
        const auto old = previous.contains(key) ? previous.at(key) : nullptr;
        if (!label(e.id, 1024) || !ids.insert(e.id).second || !tasks.contains(e.taskId) || !validDate(e.date) ||
            !validStamp(e.statusAt) || e.date > e.statusAt.substr(0, 10) || !validUtcStamp(e.recordedAt) ||
            !label(e.reporter, 1024) || !label(e.reason) || !label(e.sourceDocumentId, 1024) ||
            e.currency != p.currency || !e.recordedRevision || e.recordedRevision > p.revision ||
            e.previousEntryId != (old ? old->id : "") ||
            (e.workUnitMinutes && (*e.workUnitMinutes < 0 || *e.workUnitMinutes > maxWork)) ||
            (e.actualMinor && (*e.actualMinor < 0 || *e.actualMinor > maxMoney)))
            invalid();
        if (e.resourceId.empty()) {
            if (!e.resourceType.empty() || e.workUnitMinutes)
                invalid();
        } else if (!resources.contains(e.resourceId) || (e.resourceType != "labor" && e.resourceType != "equipment") ||
                   (old && e.resourceType != old->resourceType))
            invalid();
        if (e.actualMinor || (old && old->actualMinor) || !e.costEntryId.empty()) {
            const auto c = costs.find(e.costEntryId);
            if (c == costs.end() || c->second->taskId != e.taskId || c->second->statusAt != e.statusAt ||
                c->second->recordedRevision != e.recordedRevision ||
                c->second->sourceDocumentId != e.sourceDocumentId || c->second->recordedAt != e.recordedAt ||
                c->second->reporter != e.reporter || c->second->reason != e.reason)
                invalid();
        }
        previous[key] = &e;
        events.push_back({e.statusAt, i, true});
    }
    // Reconcile every historical status, not just the currently selected date.
    std::stable_sort(events.begin(), events.end(), [](const auto &a, const auto &b) { return a.stamp < b.stamp; });
    std::map<PeriodActualKey, std::int64_t> amounts;
    std::map<std::string, std::int64_t> allocated;
    std::map<std::string, std::optional<std::int64_t>> total;
    for (std::size_t i = 0; i < events.size();) {
        const auto stamp = events[i].stamp;
        std::set<std::string> touched;
        do {
            calculationCheckpoint();
            const auto &ev = events[i];
            if (ev.period) {
                const auto &e = p.periodActuals[ev.index];
                auto &a = allocated[e.taskId];
                auto &old = amounts[periodActualKey(e)];
                a -= old;
                const auto amount = e.actualMinor.value_or(0);
                a += amount;
                old = amount;
                touched.insert(e.taskId);
            } else {
                const auto &c = p.costEntries[ev.index];
                total[c.taskId] = c.actualMinor;
                touched.insert(c.taskId);
            }
            ++i;
        } while (i < events.size() && events[i].stamp == stamp);
        for (const auto &task : touched)
            if (allocated[task] > maxMoney || (total[task] && allocated[task] > *total[task]))
                throw AppError(ErrorCode::PERIOD_ACTUAL_BALANCE);
    }
}
std::int64_t parseActualHours(const std::string &text) {
    if (text.ends_with('u')) {
        std::int64_t n = -1;
        const auto end = text.data() + text.size() - 1;
        const auto parsed = std::from_chars(text.data(), end, n);
        if (parsed.ec != std::errc{} || parsed.ptr != end || n < 0 || n > maxWork)
            invalid();
        return n;
    }
    const auto scaled = parseFieldNumber(text);
    if (scaled < 0 || ((scaled % 1000000) * 6000) % 1000000)
        invalid();
    const auto units = scaled / 1000000 * 6000 + (scaled % 1000000) * 6000 / 1000000;
    if (units > maxWork)
        invalid();
    return units;
}
} // namespace ppm
