#include "domain/XerFinancialAllocation.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <numeric>
#include <set>
namespace jobs {
namespace {
[[noreturn]] void invalid(const char *where) {
    throw AppError(ErrorCode::FILE_PRIMAVERAXER_080, {where});
}
std::vector<std::optional<std::int64_t>> split(std::optional<std::int64_t> total,
                                               const std::vector<XerFinancialDay> &days) {
    std::vector<std::optional<std::int64_t>> out(days.size());
    if (!total)
        return out;
    std::int64_t sum = 0;
    for (const auto &d : days)
        sum += d.weight;
    std::vector<std::pair<std::int64_t, std::size_t>> remainder;
    std::int64_t assigned = 0;
    for (std::size_t i = 0; i < days.size(); ++i) {
        // Quotient/remainder avoids total * sum overflow at the native money limit.
        const auto n = (*total / sum) * days[i].weight + ((*total % sum) * days[i].weight) / sum;
        out[i] = n;
        assigned += n;
        remainder.emplace_back(((*total % sum) * days[i].weight) % sum, i);
    }
    std::stable_sort(remainder.begin(), remainder.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });
    for (std::int64_t i = 0; i < *total - assigned; ++i)
        ++*out[remainder.at(std::size_t(i)).second];
    return out;
}
void add(std::optional<std::int64_t> &a, std::optional<std::int64_t> b, std::int64_t limit) {
    if (!b)
        return;
    if (a.value_or(0) > limit - *b)
        invalid("financial allocation overflow");
    a = a.value_or(0) + *b;
}
} // namespace
std::map<PeriodActualKey, PeriodActualEntry>
allocateXerFinancials(const XerFinancialSnapshot &s, const std::vector<XerFinancialAllocation> &allocations) {
    validateXerFinancials(s);
    if (allocations.size() > 1000)
        invalid("financial allocation limit");
    std::set<std::size_t> periods;
    std::size_t count = 0;
    std::map<PeriodActualKey, PeriodActualEntry> out;
    for (const auto &a : allocations) {
        calculationCheckpoint();
        if (a.periodIndex >= s.periods.size() || !periods.insert(a.periodIndex).second || a.days.empty())
            invalid("financial allocation period");
        const auto &p = s.periods[a.periodIndex];
        count += a.days.size();
        if (count > 1000)
            invalid("financial allocation day limit");
        auto days = a.days;
        std::sort(days.begin(), days.end(), [](const auto &l, const auto &r) { return l.date < r.date; });
        std::string previous;
        for (const auto &d : days) {
            if (!validDate(d.date) || d.date < p.start.substr(0, 10) || d.date > p.finish.substr(0, 10) ||
                d.date == previous || d.weight < 1 || d.weight > 1000000)
                invalid("financial allocation date/weight");
            previous = d.date;
        }
        const auto work = split(p.resourceId.empty() ? std::nullopt : p.work, days);
        const auto cost = split(p.resourceId.empty() ? p.expenseMinor : p.actualMinor, days);
        for (std::size_t i = 0; i < days.size(); ++i) {
            if (!work[i] && !cost[i])
                continue;
            auto &e = out[{p.taskId, p.resourceId, days[i].date}];
            e.taskId = p.taskId;
            e.resourceId = p.resourceId;
            e.date = days[i].date;
            add(e.workUnitMinutes, work[i], maxWork);
            add(e.actualMinor, cost[i], maxMoney);
        }
    }
    return out;
}
std::vector<XerFinancialAllocation> initialXerFinancialAllocations(const XerFinancialSnapshot &s) {
    std::vector<XerFinancialAllocation> out;
    for (std::size_t i = 0; i < s.periods.size(); ++i) {
        const auto &p = s.periods[i];
        if (p.start.substr(0, 10) == p.finish.substr(0, 10) &&
            (!p.resourceId.empty() ? bool(p.work || p.actualMinor) : bool(p.expenseMinor)))
            out.push_back({i, {{p.start.substr(0, 10), 1}}});
    }
    return out;
}
} // namespace jobs
