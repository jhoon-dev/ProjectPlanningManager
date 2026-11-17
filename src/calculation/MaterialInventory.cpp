#include "calculation/MaterialInventory.h"
#include "calculation/Effort.h"
#include "domain/ResourcePlan.h"
#include "domain/MaterialActuals.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
namespace jobs {
namespace {
using Minute = std::int64_t;
Project inventoryInput(const Project &p, bool forecast, const std::shared_ptr<CalculationControl> &control) {
    EvaluationScope evaluation(control);
    validate(p);
    return forecast ? remainingMaterialInventoryInput(p) : p;
}
Minute minute(const std::string &s) {
    if (!validStamp(s))
        throw AppError(ErrorCode::CIVIL_TIMESTAMP_INVALID);
    using namespace std::chrono;
    return sys_days{year{std::stoi(s.substr(0, 4))} / unsigned(std::stoi(s.substr(5, 2))) /
                    unsigned(std::stoi(s.substr(8, 2)))}
                   .time_since_epoch()
                   .count() *
               1440 +
           std::stoi(s.substr(11, 2)) * 60 + std::stoi(s.substr(14, 2));
}
std::string stamp(Minute n) {
    using namespace std::chrono;
    auto day = floor<days>(sys_time<minutes>{minutes{n}});
    year_month_day date{day};
    auto time = n - duration_cast<minutes>(day.time_since_epoch()).count();
    char s[32];
    std::snprintf(s, sizeof(s), "%04d-%02u-%02u %02d:%02d", int(date.year()), unsigned(date.month()),
                  unsigned(date.day()), int(time / 60), int(time % 60));
    return s;
}
Diagnostic issue(const std::string &text) {
    return {"MATERIAL_STOCK", {text}, {}, "MATERIAL_STOCK"};
}
} // namespace
std::int64_t MaterialInventoryQuery::Demand::cumulative(Minute at) const {
    if (instant)
        return at >= *instant ? quantity : 0;
    Minute done = 0;
    for (const auto &[from, to] : spans) {
        calculationCheckpoint();
        if (at <= from)
            break;
        done += std::min(at, to) - from;
        if (at < to)
            break;
    }
    // Exact half-up cumulative allocation; multiplication is bounded by the calendar horizon.
    return (quantity / duration) * done + ((quantity % duration) * done + duration / 2) / duration;
}
MaterialInventoryQuery::MaterialInventoryQuery(const Project &p, const ScheduleResult &schedule,
                                               const std::shared_ptr<CalculationControl> &control)
    : MaterialInventoryQuery(inventoryInput(p, schedule.isForecast, control), schedule, control, nullptr, true,
                             schedule.isForecast) {}
MaterialInventoryQuery::MaterialInventoryQuery(const Project &p, const ScheduleResult &schedule,
                                               const std::shared_ptr<CalculationControl> &control,
                                               const std::set<std::string> *scheduled, bool validated, bool forecast)
    : control_(control) {
    EvaluationScope evaluation(control_);
    calculationCheckpoint();
    if (!validated)
        validate(p);
    if (schedule.status != CalculationStatus::Valid || schedule.documentId != p.documentId ||
        schedule.inputRevision != p.revision || schedule.profile != p.calculationProfile ||
        schedule.isForecast != forecast || (forecast && schedule.statusAt != p.statusAt))
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    std::map<std::string, const TaskSchedule *> rows;
    for (const auto &row : schedule.tasks)
        if (!rows.emplace(row.taskId, &row).second || !validStamp(row.start) || !validStamp(row.finish) ||
            row.start > row.finish)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    if ((!scheduled && rows.size() != p.tasks.size()) || (scheduled && rows.size() != scheduled->size()))
        throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    std::map<std::string, const Task *> tasks;
    for (const auto &t : p.tasks) {
        tasks[t.id] = &t;
        if ((!scheduled || scheduled->contains(t.id)) && !rows.contains(t.id))
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    }
    for (const auto &[id, row] : rows) {
        (void)row;
        if (!tasks.contains(id) || (scheduled && !scheduled->contains(id)))
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    }
    for (const auto &m : p.resourcePlan.materials) {
        Entry entry;
        entry.material = m;
        entry.base = minute(m.stockAt.value_or(p.startDate + " 09:00"));
        entry.checkpoints.insert(entry.base);
        entries_.emplace(m.id, std::move(entry));
    }
    std::size_t count = 0;
    const auto budget = [&] {
        calculationCheckpoint();
        if (++count > 500000)
            throw AppError(ErrorCode::WORKSPACE_LIMIT);
    };
    for (const auto &r : p.resourcePlan.receipts) {
        budget();
        auto &e = entries_.at(r.materialId);
        auto at = minute(r.at);
        e.receipts[at] += r.quantityMilli;
        e.checkpoints.insert(at);
        if (at > e.base)
            e.checkpoints.insert(at - 1);
    }
    std::map<std::string, std::vector<std::pair<Minute, Minute>>> cached;
    WorkCalendar clock(p);
    ExecutionInputs inputs(p);
    for (const auto &u : p.resourcePlan.uses) {
        if (scheduled && !scheduled->contains(u.taskId))
            continue;
        budget();
        auto &e = entries_.at(u.materialId);
        const auto &row = *rows.at(u.taskId);
        Demand d;
        d.quantity = u.quantityMilli;
        if (u.accrual != "uniform" || row.start == row.finish)
            d.instant = minute(u.accrual == "start" ? row.remainingStart.value_or(row.start) : row.finish);
        else {
            if (!cached.contains(u.taskId)) {
                const auto *task = tasks.at(u.taskId);
                std::vector<std::pair<Minute, Minute>> spans;
                if (isEffort(p) && inputs.assignments.contains(task->id)) {
                    for (const auto &span :
                         TaskExecution(p, forecast ? pendingTask(p, *task) : *task, &inputs, forecast).spans(row))
                        if (span.start < span.finish)
                            spans.emplace_back(minute(clock.boundary(span.start, false)),
                                               minute(clock.boundary(span.finish, true)));
                } else {
                    for (const auto &[from, to] :
                         WorkCalendar(p, *task).segments(row.remainingStart.value_or(row.start), row.finish))
                        spans.emplace_back(minute(from), minute(to));
                }
                std::sort(spans.begin(), spans.end());
                auto &merged = cached[u.taskId];
                for (const auto &span : spans) {
                    if (!merged.empty() && span.first <= merged.back().second)
                        merged.back().second = std::max(merged.back().second, span.second);
                    else
                        merged.push_back(span);
                }
            }
            d.spans = cached.at(u.taskId);
            for (const auto &[from, to] : d.spans) {
                budget();
                d.duration += to - from;
                e.checkpoints.insert(std::max(e.base, to));
            }
            if (!d.duration)
                throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        }
        if (d.instant)
            e.checkpoints.insert(std::max(e.base, *d.instant));
        d.openingConsumed = d.cumulative(e.base - 1);
        e.demands.push_back(std::move(d));
    }
    for (auto &[id, e] : entries_) {
        (void)id;
        std::uint64_t weight = e.demands.size();
        for (const auto &d : e.demands)
            weight += d.spans.size();
        shortageWork_ += weight * (e.checkpoints.size() + 34);
        Minute sum = 0;
        for (auto &[at, q] : e.receipts) {
            (void)at;
            sum += q;
            q = sum;
        }
    }
}
MaterialInventoryPoint MaterialInventoryQuery::at(const std::string &id, const std::string &at) const {
    EvaluationScope evaluation(control_);
    calculationCheckpoint();
    auto time = minute(at);
    if (!entries_.contains(id))
        throw AppError(ErrorCode::RESOURCE_INPUT_INVALID);
    const auto &e = entries_.at(id);
    MaterialInventoryPoint out;
    out.materialId = id;
    out.name = e.material.name;
    out.unit = e.material.unit;
    out.at = at;
    out.stockAt = stamp(e.base);
    out.opening = e.material.stockMilli;
    if (time < e.base) {
        out.issue = "개시 재고 기준 시각 이전은 추정하지 않습니다.";
        return out;
    }
    auto receipt = e.receipts.upper_bound(time);
    out.received = receipt == e.receipts.begin() ? 0 : std::prev(receipt)->second;
    out.consumed = 0;
    for (const auto &d : e.demands) {
        calculationCheckpoint();
        *out.consumed += d.cumulative(time) - d.openingConsumed;
    }
    if (out.opening) {
        out.remaining = *out.opening + *out.received - *out.consumed;
        out.available = std::max<Minute>(0, *out.remaining);
        out.shortage = std::max<Minute>(0, -*out.remaining);
    } else
        out.issue = "개시 재고가 미정입니다.";
    return out;
}
std::vector<MaterialInventoryPoint> MaterialInventoryQuery::allAt(const std::string &at) const {
    (void)minute(at);
    std::vector<MaterialInventoryPoint> out;
    for (const auto &[id, e] : entries_) {
        (void)e;
        out.push_back(this->at(id, at));
    }
    return out;
}
std::optional<std::string> MaterialInventoryQuery::firstShortage(const std::string &id) const {
    EvaluationScope evaluation(control_);
    calculationCheckpoint();
    if (!entries_.contains(id))
        throw AppError(ErrorCode::RESOURCE_INPUT_INVALID);
    const auto &e = entries_.at(id);
    if (!e.material.stockMilli || e.demands.empty())
        return {};
    if (shortageWork_ > 5000000)
        throw AppError(ErrorCode::WORKSPACE_LIMIT);
    auto previous = e.base;
    for (auto at : e.checkpoints) {
        if (this->at(id, stamp(at)).remaining.value() < 0) {
            auto low = previous, high = at;
            while (low < high) {
                auto middle = low + (high - low) / 2;
                if (this->at(id, stamp(middle)).remaining.value() < 0)
                    high = middle;
                else
                    low = middle + 1;
            }
            return stamp(low);
        }
        previous = at;
    }
    return {};
}
std::vector<Diagnostic> materialInventoryDiagnostics(const Project &p, const ScheduleResult &schedule) {
    if (!hasMaterialInventory(p))
        return {};
    if (schedule.isForecast && !p.resourcePlan.autoDelayRemainingForReceipts)
        return {issue("잔여 자재 자동 대기 옵션과 상태일 원장 확인이 필요합니다.")};
    MaterialInventoryQuery query(p, schedule);
    std::vector<Diagnostic> out;
    std::set<std::string> used;
    for (const auto &u : p.resourcePlan.uses)
        used.insert(u.materialId);
    for (const auto &m : p.resourcePlan.materials) {
        calculationCheckpoint();
        if (!used.contains(m.id))
            continue;
        if (!m.stockMilli)
            out.push_back(issue(m.name + " · 개시 재고 미정"));
        else if (auto at = query.firstShortage(m.id))
            out.push_back(issue(m.name + " · 최초 계획 재고 부족 " + *at + " · 입고 또는 작업 날짜를 조정하세요."));
    }
    return out;
}
} // namespace jobs
