#include "calculation/Usage.h"
#include "calculation/Resources.h"
#include "calculation/WorkCalendar.h"
#include "calculation/Effort.h"
#include "domain/Wbs.h"
#include "domain/PeriodActuals.h"
#include "calculation/PeriodActualReports.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#include <unordered_map>

namespace ppm {
namespace {
using namespace std::chrono;
sys_days day(const std::string &s) {
    return sys_days{year{std::stoi(s.substr(0, 4))} / unsigned(std::stoi(s.substr(5, 2))) /
                    unsigned(std::stoi(s.substr(8, 2)))};
}
std::string date(sys_days d) {
    const year_month_day ymd{d};
    char text[16];
    std::snprintf(text, sizeof(text), "%04d-%02u-%02u", int(ymd.year()), unsigned(ymd.month()), unsigned(ymd.day()));
    return text;
}
Money sum(Money a, Money b, std::int64_t limit = maxMoney) {
    if (!a || !b || *a < 0 || *b < 0 || *a > limit - *b)
        return {};
    return *a + *b;
}
void add(UsageValue &a, const UsageValue &b) {
    a.labor = sum(a.labor, b.labor, maxWork);
    a.equipment = sum(a.equipment, b.equipment, maxWork);
    a.cost = sum(a.cost, b.cost);
}
UsageValue unknown() {
    return {{}, {}, {}};
}
void initialize(UsageRow &row, const UsageResult &r) {
    for (std::size_t s = 0; s < 3; ++s)
        row.values[s].assign(r.buckets.size(), r.available[s] ? UsageValue{} : unknown());
    row.reportedActual.assign(r.buckets.size(), unknown());
    row.actualCounts.resize(r.buckets.size());
}
void addReported(UsageValue &a, const UsageValue &b) {
    const std::array<Money *, 3> target{&a.labor, &a.equipment, &a.cost};
    const std::array<const Money *, 3> source{&b.labor, &b.equipment, &b.cost};
    for (std::size_t i = 0; i < 3; ++i) {
        if (a.reportOverflow[i] || b.reportOverflow[i]) {
            a.reportOverflow[i] = true;
            target[i]->reset();
        } else if (*source[i]) {
            *target[i] = *target[i] ? sum(*target[i], *source[i], i == 2 ? maxMoney : maxWork) : *source[i];
            if (!*target[i])
                a.reportOverflow[i] = true;
        }
    }
}
std::size_t bucketAt(const UsageResult &r, const std::string &d) {
    const auto it = std::lower_bound(r.buckets.begin(), r.buckets.end(), d,
                                     [](const auto &b, const auto &value) { return b.to < value; });
    return it != r.buckets.end() && it->from <= d ? std::size_t(it - r.buckets.begin()) : r.buckets.size();
}
std::set<std::string> scopedTasks(const PlanInput &p, const std::string &wbs) {
    if (!wbs.empty() && std::none_of(p.wbsNodes.begin(), p.wbsNodes.end(), [&](const auto &n) { return n.id == wbs; }))
        return {};
    std::set<std::string> ids;
    for (const auto i : wbsTaskRows(p, wbs))
        ids.insert(p.tasks[i].id);
    return ids;
}
ReportCell work(Money value) {
    return {value ? decltype(ReportCell::value){*value} : decltype(ReportCell::value){}, CellFormat::WorkHours};
}
ReportCell money(Money value) {
    return {value ? decltype(ReportCell::value){*value} : decltype(ReportCell::value){}, CellFormat::Money};
}
ReportCell text(const std::string &value) {
    return {value, CellFormat::Text};
}
} // namespace

UsageResult buildUsage(const Project &p, const UsageOptions &o, const std::shared_ptr<CalculationControl> &control) {
    EvaluationScope evaluation(control);
    calculationCheckpoint();
    validate(p);
    if (!validDate(o.from) || !validDate(o.to) || o.from > o.to || day(o.to) - day(o.from) >= days{7320} ||
        (o.period != UsagePeriod::Day && o.period != UsagePeriod::Week && o.period != UsagePeriod::Month))
        throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
    UsageResult out;
    out.documentId = p.documentId;
    out.revision = p.revision;
    out.name = p.name;
    out.currency = p.currency;
    out.statusAt = p.statusAt;
    out.options = o;
    if (p.progressPolicy) {
        const auto &s = *p.progressPolicy;
        out.progressPolicyDescription = s.policy + " · " + s.adoptedBy + " · " + s.reason + " · UTC " + s.adoptedAt +
                                        " · 원본 " + s.sourceDocumentId + " rev " + std::to_string(s.sourceRevision) +
                                        (p.statusAt ? "" : " · 상태일 미설정: 비활성");
    }
    const auto &selection = p.remainingLeveling ? p.remainingLeveling : p.planLeveling;
    if (selection)
        out.levelingDescription = std::string(p.remainingLeveling ? "잔여 예측" : "계획") + " · " + selection->policy +
                                  " · " + selection->priority + " · " + std::to_string(selection->horizonWorkDays) +
                                  " 근무일 · " + selection->adoptedAt + " · " + selection->reason + " · 원본 " +
                                  selection->sourceDocumentId + " rev " + std::to_string(selection->sourceRevision) +
                                  " · 자원 반영 여유/임계 · 계산 여부와 구간 재생성 기준은 진단 참조";
    for (auto at = day(o.from), last = day(o.to); at <= last;) {
        calculationCheckpoint();
        auto end = at;
        if (o.period == UsagePeriod::Week)
            end = at + days{7 - weekday{at}.iso_encoding()};
        if (o.period == UsagePeriod::Month) {
            const year_month_day ymd{at};
            end = sys_days{ymd.year() / ymd.month() / std::chrono::last};
        }
        end = std::min(end, last);
        if (out.buckets.size() >= 366)
            throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
        out.buckets.push_back({date(at), date(end)});
        at = end + days{1};
    }
    Project frozen;
    const Baseline *baseline = nullptr;
    if (!o.baselineId.empty()) {
        const auto found =
            std::find_if(p.baselines.begin(), p.baselines.end(), [&](const auto &b) { return b.id == o.baselineId; });
        if (found == p.baselines.end())
            throw AppError(ErrorCode::BASELINE_NOT_FOUND);
        baseline = &*found;
        verifyBaseline(*baseline);
        out.baselineDescription = baseline->name + " · ID " + baseline->id + " · v" +
                                  std::to_string(baseline->version) + " · source rev " +
                                  std::to_string(baseline->inputRevision) + " · " + baseline->engineVersion +
                                  " · 승인 " + baseline->approvedBy + " · " + baseline->createdAt;
        static_cast<PlanInput &>(frozen) = baseline->input;
        frozen.documentId = p.documentId;
        frozen.revision = p.revision;
    }
    if (!o.wbsId.empty() &&
        std::none_of(p.wbsNodes.begin(), p.wbsNodes.end(), [&](const auto &n) { return n.id == o.wbsId; }) &&
        std::none_of(frozen.wbsNodes.begin(), frozen.wbsNodes.end(), [&](const auto &n) { return n.id == o.wbsId; }))
        throw AppError(ErrorCode::WBS_NOT_FOUND);
    auto selected = scopedTasks(p, o.wbsId);
    if (baseline) {
        const auto ids = scopedTasks(frozen, o.wbsId);
        selected.insert(ids.begin(), ids.end());
    }
    for (const auto *source : std::array<const Project *, 2>{&p, &frozen}) {
        for (const auto &t : source->tasks)
            if (selected.contains(t.id) && !out.taskNames.contains(t.id)) {
                out.taskOrder.push_back(t.id);
                out.taskNames[t.id] = t.name;
            }
        for (const auto &r : source->resources)
            if (!out.resourceNames.contains(r.id)) {
                out.resourceOrder.push_back(r.id);
                out.resourceNames[r.id] = r.name;
            }
    }
    std::array<ScheduleResult, 3> schedules;
    schedules[0] = calculatePlan(p);
    if (baseline) {
        schedules[1].documentId = frozen.documentId;
        schedules[1].inputRevision = frozen.revision;
        schedules[1].profile = frozen.calculationProfile;
        schedules[1].engineVersion = baseline->engineVersion;
        schedules[1].tasks = baseline->schedule;
        schedules[1].status = CalculationStatus::Valid;
    }
    if (p.statusAt)
        schedules[2] = calculate(p);
    for (std::size_t s = 0; s < 3; ++s) {
        out.available[s] = schedules[s].status == CalculationStatus::Valid;
        out.diagnostics.insert(out.diagnostics.end(), schedules[s].diagnostics.begin(), schedules[s].diagnostics.end());
    }
    using Key = std::pair<std::string, std::string>;
    std::map<Key, std::size_t> index;
    std::set<std::string> current;
    for (const auto &t : p.tasks)
        current.insert(t.id);
    auto leaf = [&](const std::string &task, const std::string &resource) {
        const Key key{task, resource};
        if (index.contains(key))
            return;
        UsageRow row;
        row.taskId = task;
        row.resourceId = resource;
        row.leaf = true;
        row.fixed = resource.empty();
        row.baselineOnly = !current.contains(task);
        row.name = row.fixed ? (p.periodActuals.empty() ? "작업 고정비" : "계획 고정비 / 실제 직접비")
                             : out.resourceNames.at(resource);
        initialize(row, out);
        index.emplace(key, out.leaves.size());
        out.leaves.push_back(std::move(row));
    };
    std::set<Key> keys;
    std::set<std::string> usedResources;
    for (const auto *source : std::array<const Project *, 2>{&p, &frozen})
        for (const auto &a : source->assignments)
            if (selected.contains(a.taskId)) {
                keys.emplace(a.taskId, a.resourceId);
                usedResources.insert(a.resourceId);
            }
    for (const auto &e : p.periodActuals)
        if (selected.contains(e.taskId) && !e.resourceId.empty()) {
            keys.emplace(e.taskId, e.resourceId);
            usedResources.insert(e.resourceId);
        }
    const auto maximumRows =
        keys.size() + out.taskOrder.size() + std::max(out.taskOrder.size(), usedResources.size() + 1) + 1;
    if (maximumRows * out.buckets.size() > 1000000 / (p.periodActuals.empty() ? 9 : 12))
        throw AppError(ErrorCode::REPORT_CELL_LIMIT);
    for (const auto &id : out.taskOrder)
        leaf(id, {});
    std::set<std::string> assigned;
    for (const auto *source : std::array<const Project *, 2>{&p, &frozen})
        for (const auto &a : source->assignments)
            if (selected.contains(a.taskId)) {
                leaf(a.taskId, a.resourceId);
                if (source == &p)
                    assigned.insert(a.taskId);
            }
    for (const auto &t : p.tasks)
        if (selected.contains(t.id) && !assigned.contains(t.id))
            ++out.unassignedTasks;
    for (const auto &key : keys)
        leaf(key.first, key.second);
    const auto allocations =
        p.statusAt ? actualCostAllocations(p, *p.statusAt) : std::map<std::string, ActualCostAllocation>{};
    for (const auto &id : out.taskOrder) {
        auto *entry = p.statusAt ? costAsOf(p, id, *p.statusAt) : nullptr;
        out.actuals[id] = entry ? entry->actualMinor : Money{};
        out.unallocatedActuals[id] = allocations.contains(id) ? allocations.at(id).unallocated : out.actuals[id];
    }
    if (p.statusAt)
        for (const auto &[key, e] : periodActualsAsOf(p, *p.statusAt)) {
            (void)key;
            if (!selected.contains(e->taskId))
                continue;
            const auto b = bucketAt(out, e->date);
            if (b == out.buckets.size())
                continue;
            auto &row = out.leaves.at(index.at({e->taskId, e->resourceId}));
            UsageValue value = unknown();
            value.cost = e->actualMinor;
            if (e->resourceType == "labor")
                value.labor = e->workUnitMinutes;
            if (e->resourceType == "equipment")
                value.equipment = e->workUnitMinutes;
            addReported(row.reportedActual[b], value);
            row.actualCounts[b].first += e->workUnitMinutes.has_value();
            row.actualCounts[b].second += e->actualMinor.has_value();
        }
    out.actualTables = periodActualTables(p, selected, o.from, o.to);
    for (std::size_t s = 0; s < 3; ++s) {
        calculationCheckpoint();
        if (!out.available[s])
            continue;
        const auto &source = s == 1 ? frozen : p;
        std::unordered_map<std::string, std::string> types;
        for (const auto &r : source.resources)
            types.emplace(r.id, r.type);
        // Occupancy is computed on the full network, then scoped. No graph pruning.
        for (const auto &span : resourceOccupancies(source, schedules[s])) {
            if (!selected.contains(span.taskId))
                continue;
            auto &row = out.leaves.at(index.at({span.taskId, span.resourceId}));
            const auto at = std::max(span.start, o.from + " 00:00");
            const auto stop = std::min(day(span.finish.substr(0, 10)), day(o.to));
            for (auto d = day(at.substr(0, 10)); d <= stop && at < span.finish; d += days{1}) {
                calculationCheckpoint();
                const auto label = date(d);
                if (label < o.from || label > o.to)
                    continue;
                const auto minute = [](const std::string &stamp) {
                    return std::stoi(stamp.substr(11, 2)) * 60 + std::stoi(stamp.substr(14, 2));
                };
                const auto begin = span.start.substr(0, 10) == label ? minute(span.start) : 0;
                const auto finish = span.finish.substr(0, 10) == label ? minute(span.finish) : 1440;
                if (begin >= finish)
                    continue;
                const auto length = finish - begin;
                auto &value = row.values[s][bucketAt(out, label)];
                auto &work = types.at(span.resourceId) == "labor" ? value.labor : value.equipment;
                work = sum(work, length * span.units, maxWork);
            }
        }
        // Project the forecast's remaining execution as a temporary plan for shared costing.
        Project costSource = source;
        auto costSchedule = schedules[s];
        if (s == 2) {
            std::erase_if(costSource.tasks, [&](const auto &t) {
                return std::any_of(costSchedule.tasks.begin(), costSchedule.tasks.end(),
                                   [&](const auto &r) { return r.taskId == t.id && r.completedActual; });
            });
            for (auto &t : costSource.tasks)
                if (usesWork(t))
                    t = pendingTask(source, t);
            WorkCalendar clock(costSource);
            for (auto &row : costSchedule.tasks) {
                row.start = row.remainingStart.value_or(row.start);
                row.earlyStartMinutes = clock.offset(row.start);
            }
            costSchedule.isForecast = false;
        }
        std::erase_if(costSource.tasks, [&](const auto &t) { return !selected.contains(t.id); });
        const bool currencyOk = s != 1 || frozen.currency == p.currency;
        for (const auto &v : plannedCostDays(costSource, costSchedule, o.from, o.to, s != 2)) {
            auto &value = out.leaves.at(index.at({v.taskId, v.resourceId})).values[s][bucketAt(out, v.date)];
            value.cost = sum(value.cost, currencyOk ? v.amount : Money{});
        }
        if (!currencyOk) {
            for (auto &row : out.leaves)
                for (auto &v : row.values[s])
                    v.cost.reset();
            out.diagnostics.push_back({"CURRENCY_MISMATCH", {}, {}, "CURRENCY_MISMATCH"});
        }
    }
    return out;
}
UsageValue usageTotal(const std::vector<UsageValue> &values) {
    UsageValue total;
    for (const auto &v : values)
        add(total, v);
    return total;
}
std::vector<UsageRow> usageRows(const UsageResult &r, UsageHierarchy hierarchy) {
    if (hierarchy != UsageHierarchy::Tasks && hierarchy != UsageHierarchy::Resources)
        throw AppError(ErrorCode::REPORT_OPTIONS_INVALID);
    std::vector<UsageRow> rows;
    UsageRow root;
    root.name = "범위 합계";
    initialize(root, r);
    root.unallocatedActual = 0;
    root.cumulativeActual = 0;
    for (const auto &[id, actual] : r.actuals) {
        (void)id;
        root.cumulativeActual = sum(root.cumulativeActual, actual);
        root.unallocatedActual = sum(root.unallocatedActual, r.unallocatedActuals.at(id));
    }
    rows.push_back(std::move(root));
    std::map<std::string, std::size_t> parents;
    auto parent = [&](const std::string &id, const std::string &name) {
        UsageRow row;
        row.name = name;
        row.parent = 0;
        if (hierarchy == UsageHierarchy::Tasks) {
            row.taskId = id;
            row.cumulativeActual = r.actuals.at(id);
            row.unallocatedActual = r.unallocatedActuals.at(id);
        } else
            row.resourceId = id;
        initialize(row, r);
        parents[id] = rows.size();
        rows.push_back(std::move(row));
    };
    if (hierarchy == UsageHierarchy::Tasks)
        for (const auto &id : r.taskOrder)
            parent(id, r.taskNames.at(id));
    else {
        for (const auto &id : r.resourceOrder)
            if (std::any_of(r.leaves.begin(), r.leaves.end(), [&](const auto &row) { return row.resourceId == id; }))
                parent(id, r.resourceNames.at(id));
        parent({}, r.actualTables.empty() ? "작업 고정비 · 자원 미배분" : "고정비 / 실제 직접비 · 자원 미배분");
    }
    for (const auto &leaf : r.leaves) {
        calculationCheckpoint();
        const auto index = parents.at(hierarchy == UsageHierarchy::Tasks ? leaf.taskId : leaf.resourceId);
        auto row = leaf;
        row.parent = index;
        if (hierarchy == UsageHierarchy::Resources)
            row.name = r.taskNames.at(leaf.taskId);
        for (std::size_t s = 0; s < 3; ++s)
            for (std::size_t b = 0; b < r.buckets.size(); ++b) {
                add(rows[0].values[s][b], leaf.values[s][b]);
                add(rows[index].values[s][b], leaf.values[s][b]);
            }
        for (std::size_t b = 0; b < r.buckets.size(); ++b)
            for (const auto target : {std::size_t{0}, index}) {
                addReported(rows[target].reportedActual[b], leaf.reportedActual[b]);
                rows[target].actualCounts[b].first += leaf.actualCounts[b].first;
                rows[target].actualCounts[b].second += leaf.actualCounts[b].second;
            }
        rows.push_back(std::move(row));
    }
    return rows;
}
UsageValue usageReportedTotal(const std::vector<UsageValue> &values) {
    auto total = unknown();
    for (const auto &v : values)
        addReported(total, v);
    return total;
}
ProjectReport usageReport(const UsageResult &r, UsageHierarchy hierarchy, const std::string &id,
                          const std::string &generatedAt, bool unsaved) {
    ProjectReport report;
    report.projectName = r.name;
    report.documentId = r.documentId;
    report.revision = r.revision;
    report.currency = r.currency;
    report.statusAt = r.statusAt;
    report.scopeName = "Usage · WBS " + r.options.wbsId;
    report.options.reportId = id;
    report.options.generatedAt = generatedAt;
    report.options.unsaved = unsaved;
    report.options.wbsId = r.options.wbsId;
    report.options.baselineId = r.options.baselineId;
    report.options.fromDate = r.options.from;
    report.options.toDate = r.options.to;
    report.scheduleStatus = r.available[0] ? CalculationStatus::Valid : CalculationStatus::Incomplete;
    ReportTable table;
    table.id = "usage";
    table.title = "시간축 공수와 원가";
    table.note = "배정 명세만 수록 · 상위 합계와 중복 합산 금지 · 공수는 인시/장비시 별도 · 잔여 원가는 자원 "
                 "배정만(고정비 제외)";
    table.columns = {{"작업 ID", 20},   {"작업", 24},        {"자원 ID", 18},       {"자원/고정비", 24},
                     {"기간 시작", 14}, {"기간 끝", 14},     {"계획 인시", 14},     {"계획 장비시", 14},
                     {"계획 원가", 18}, {"기준선 인시", 14}, {"기준선 장비시", 14}, {"기준선 원가", 18},
                     {"잔여 인시", 14}, {"잔여 장비시", 14}, {"잔여 배정원가", 18}};
    if (r.leaves.size() * r.buckets.size() > 1000000 / table.columns.size())
        throw AppError(ErrorCode::REPORT_CELL_LIMIT);
    for (const auto &row : r.leaves)
        for (std::size_t b = 0; b < r.buckets.size(); ++b) {
            calculationCheckpoint();
            std::vector<ReportCell> cells{
                text(row.taskId),        text(r.taskNames.at(row.taskId)),
                text(row.resourceId),    text(row.fixed ? "작업 고정비" : r.resourceNames.at(row.resourceId)),
                text(r.buckets[b].from), text(r.buckets[b].to)};
            for (std::size_t s = 0; s < 3; ++s) {
                const auto &v = row.values[s][b];
                cells.push_back(work(v.labor));
                cells.push_back(work(v.equipment));
                cells.push_back(s == 2 && row.fixed ? text("고정비 제외") : money(v.cost));
            }
            table.rows.push_back(std::move(cells));
        }
    report.tables.push_back(std::move(table));
    ReportTable actual;
    actual.id = "usageActual";
    actual.title = r.actualTables.empty() ? "기간 미배분 실제원가" : "작업 누적 실제원가";
    actual.note =
        r.actualTables.empty()
            ? "상태일 기준 누적 작업 원장 · 날짜/자원 배분 없음 · 실제공수 근거 없음 · 시간축 합계에 더하지 않음"
            : "상태일 기준 누적 AC · 기간별 배분액을 포함하며 다시 더하지 않음 · 전기간 배분/미배분은 대사 표 참조";
    actual.columns = {{"작업 ID", 22}, {"작업", 28}, {"상태일", 24}, {"누적 실제원가", 20}};
    for (const auto &task : r.taskOrder)
        actual.rows.push_back(
            {text(task), text(r.taskNames.at(task)), text(r.statusAt.value_or("미설정")), money(r.actuals.at(task))});
    report.tables.push_back(std::move(actual));
    ReportTable source;
    source.id = "usageSource";
    source.title = "Usage 출처와 정책";
    source.note = "usage-civil-day-v1 · 일별 확정 후 주/월 합산 · 누계 실적을 날짜/자원에 배분하지 않음";
    source.columns = {{"항목", 26}, {"값", 100}};
    const std::vector<std::pair<std::string, std::string>> facts{
        {"문서 ID", r.documentId},
        {"revision", std::to_string(r.revision)},
        {"WBS ID", r.options.wbsId.empty() ? "프로젝트 전체" : r.options.wbsId},
        {"기준선", r.baselineDescription.empty() ? "미선택" : r.baselineDescription},
        {"평준화 채택", r.levelingDescription.empty() ? "미채택 · 논리 일정" : r.levelingDescription},
        {"진척 정책 채택", r.progressPolicyDescription.empty() ? "기본 잔여 논리 유지" : r.progressPolicyDescription},
        {"기간", r.options.from + " ~ " + r.options.to},
        {"시간축", r.options.period == UsagePeriod::Day    ? "일"
                   : r.options.period == UsagePeriod::Week ? "주 · 월요일 시작"
                                                           : "달력 월"},
        {"상태일", r.statusAt.value_or("미설정")},
        {"통화", r.currency},
        {"저장 상태", unsaved ? "미저장 메모리" : "저장된 원본"},
        {"생성 UTC", generatedAt},
        {"보고서 ID", id},
        {"공수 단위", "인시/장비시 별도 · 내부 %·분 / 6000"},
        {"잔여 원가", "잔여 배정 공수 × 유효 단가 · 고정비 제외 · EAC/ETC 아님"},
        {"실제공수", r.actualTables.empty() ? "배정/기간별 원장 근거 없음"
                                            : "관측 보고분만 합산 · 미보고와 0 구분 · 인시/장비시 별도"},
        {"미배정 현재 작업", std::to_string(r.unassignedTasks)}};
    for (const auto &[key, value] : facts)
        source.rows.push_back({text(key), text(value)});
    report.tables.push_back(std::move(source));
    report.tables.insert(report.tables.end(), r.actualTables.begin(), r.actualTables.end());
    (void)hierarchy; // Both on-screen hierarchies export the same unique leaf grain.
    return report;
}
} // namespace ppm
