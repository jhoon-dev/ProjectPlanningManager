#include "calculation/DatedRemainingWork.h"
#include <algorithm>
#include <map>
#include <tuple>
namespace jobs {
namespace {
using Minute = std::int64_t;
[[noreturn]] void invalid(const std::string &reason) {
    throw AppError(ErrorCode::REMAINING_WORK_INVALID, {reason}, {}, "DATED_REMAINING_INVALID");
}
// Consume untargeted demand chronologically, dividing only the final minute.
std::optional<std::vector<WorkSpan>> take(const std::vector<WorkSpan> &spans, Minute target) {
    if (!target)
        return std::vector<WorkSpan>{};
    std::map<Minute, Minute> events;
    for (const auto &s : spans) {
        events[s.start] += s.units;
        events[s.finish] -= s.units;
    }
    Minute used = 0, work = 0, last = 0, finish = 0, tailWork = 0, tailRate = 0;
    for (const auto &[at, delta] : events) {
        calculationCheckpoint();
        if (used && target - work <= (at - last) * used) {
            finish = last + (target - work + used - 1) / used;
            tailWork = target - work - (finish - last - 1) * used;
            tailRate = used;
            break;
        }
        work += (at - last) * used;
        used += delta;
        last = at;
    }
    if (!finish)
        return {};
    std::vector<WorkSpan> result, tail;
    Minute allocated = 0;
    for (const auto &s : spans) {
        if (s.start >= finish)
            continue;
        const auto end = std::min(s.finish, finish - 1);
        if (end > s.start)
            result.push_back({s.resourceId, s.start, end, s.units});
        if (s.start <= finish - 1 && s.finish >= finish) {
            const auto units = int(Minute(s.units) * tailWork / tailRate);
            allocated += units;
            tail.push_back({s.resourceId, finish - 1, finish, units});
        }
    }
    std::sort(tail.begin(), tail.end(), [](const auto &a, const auto &b) { return a.resourceId < b.resourceId; });
    for (auto &s : tail) {
        if (allocated < tailWork) {
            ++allocated;
            ++s.units;
        }
        if (s.units)
            result.push_back(s);
    }
    return result;
}
} // namespace
TaskExecution::DatedResult TaskExecution::datedExecution(Minute local) const {
    calculationCheckpoint();
    if (datedCache_ && datedCache_->first == local)
        return datedCache_->second;
    if (!task_.workUnitMinutes.value_or(0))
        invalid("잔여 총공수가 0인 작업의 날짜 목표를 해제하세요: " + task_.id);
    TaskSchedule row;
    row.taskId = task_.id;
    row.start = calendar_.boundary(local, false);
    row.remainingStart = row.start;
    row.finish = calendar_.boundary(calendar_.maxOffset(), true);
    auto mandatory = applyDatedRemainingWork(statusAt_, remainingWork_, row, clock_, grid_->availability, {});
    Minute reserved = 0;
    for (const auto &s : mandatory) {
        const auto amount = (s.finish - s.start) * s.units;
        if (amount > *task_.workUnitMinutes - reserved)
            invalid("날짜별 목표 합계가 명시 잔여 총공수를 넘습니다: " + task_.id);
        reserved += amount;
    }
    // Targeted dates replace that resource's profile; every other date keeps its curve.
    std::map<std::string, std::vector<std::pair<Minute, Minute>>> exclusions;
    for (const auto &p : remainingWork_)
        for (const auto &d : p.days) {
            const auto begin = clock_.offset(d.date + " 00:00");
            exclusions[p.resourceId].push_back({begin, begin + 1440});
        }
    const auto limit = calendar_.maxOffset() - local;
    Minute base = 0;
    for (const auto &a : profiles_)
        base += a.units;
    auto width = std::min(limit, std::max(Minute{1}, *task_.workUnitMinutes / std::max(Minute{1}, base) + 1));
    std::optional<std::vector<WorkSpan>> free;
    for (;;) {
        calculationCheckpoint();
        row.finish = calendar_.boundary(local + width, true);
        auto shaped = profileSpans(row);
        std::vector<WorkSpan> kept;
        for (const auto &s : shaped) {
            auto at = s.start;
            for (const auto &[a, b] : exclusions[s.resourceId]) {
                if (a >= s.finish)
                    break;
                if (b <= at)
                    continue;
                if (a > at)
                    kept.push_back({s.resourceId, at, std::min(a, s.finish), s.units});
                at = std::max(at, b);
            }
            if (at < s.finish)
                kept.push_back({s.resourceId, at, s.finish, s.units});
        }
        free = take(kept, *task_.workUnitMinutes - reserved);
        if (free)
            break;
        if (width == limit)
            invalid("날짜 목표 외의 잔여 공수를 기존 배정률/곡선과 달력 안에서 채울 수 없습니다: " + task_.id);
        width = std::min(limit, width * 2);
    }
    mandatory.insert(mandatory.end(), free->begin(), free->end());
    Minute end = clock_.offset(row.start), certified = 0;
    for (const auto &s : mandatory) {
        end = std::max(end, s.finish);
        certified += (s.finish - s.start) * s.units;
    }
    row.finish = clock_.boundary(end, true);
    for (const auto &p : remainingWork_)
        for (const auto &d : p.days)
            if (d.date > row.finish.substr(0, 10))
                invalid("완료 뒤 날짜의 0 목표는 실행을 연장하지 않습니다. 날짜 입력을 해제하세요: " + d.date);
    if (certified != *task_.workUnitMinutes)
        throw AppError(ErrorCode::WORK_RESULT_MISMATCH);
    row.remainingStart.reset();
    row.earlyStartMinutes = clock_.offset(row.start);
    row.earlyFinishMinutes = end;
    std::sort(mandatory.begin(), mandatory.end(), [](const auto &a, const auto &b) {
        return std::tie(a.resourceId, a.start) < std::tie(b.resourceId, b.start);
    });
    DatedResult result{row, std::move(mandatory)};
    datedCache_ = std::make_pair(local, result);
    return result;
}
} // namespace jobs
