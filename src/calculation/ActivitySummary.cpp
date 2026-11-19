#include "calculation/ActivitySummary.h"
#include "calculation/WorkCalendar.h"
#include "domain/Activities.h"
#include "domain/PeriodActuals.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
namespace ppm {
std::vector<ActivityInterval> activityIntervals(const Project &p, const ScheduleResult &schedule) {
    validate(p);
    std::map<std::string, const TaskSchedule *> rows;
    for (const auto &r : schedule.tasks)
        rows.emplace(r.taskId, &r);
    const bool current = schedule.documentId == p.documentId && schedule.inputRevision == p.revision &&
                         schedule.status == CalculationStatus::Valid;
    std::map<std::string, std::string> parents;
    for (const auto &w : p.wbsNodes)
        parents.emplace(w.id, w.parentId);
    std::vector<ActivityInterval> result;
    for (const auto &a : p.activities) {
        calculationCheckpoint();
        if (a.kind == "steps")
            continue;
        ActivityInterval row;
        row.id = a.id;
        row.name = a.name;
        row.kind = a.kind;
        if (!current)
            row.reason = "원본 일정 미산정·오래됨";
        else {
            std::vector<const TaskSchedule *> sources;
            if (a.kind == "loe") {
                if (rows.contains(a.startTaskId) && rows.contains(a.finishTaskId)) {
                    row.start = rows.at(a.startTaskId)->start;
                    row.finish = rows.at(a.finishTaskId)->finish;
                    row.sourceTasks = a.startTaskId == a.finishTaskId ? 1 : 2;
                }
            } else {
                for (const auto &t : p.tasks) {
                    auto wbs = t.wbsId;
                    bool included = a.wbsId.empty();
                    std::set<std::string> seen;
                    while (!included && !wbs.empty() && seen.insert(wbs).second) {
                        included = wbs == a.wbsId;
                        wbs = parents.contains(wbs) ? parents.at(wbs) : std::string{};
                    }
                    if (included && rows.contains(t.id))
                        sources.push_back(rows.at(t.id));
                }
                row.sourceTasks = sources.size();
                for (const auto *r : sources) {
                    if (!row.start || r->start < *row.start)
                        row.start = r->start;
                    if (!row.finish || r->finish > *row.finish)
                        row.finish = r->finish;
                }
            }
            if (!row.start || !row.finish)
                row.reason = "범위 안에 계산된 실제 작업이 없습니다.";
            else if (*row.start > *row.finish) {
                row.reason = "LOE 시작이 완료보다 늦습니다. 기준 작업을 확인하세요.";
                row.start.reset();
                row.finish.reset();
            } else {
                Task clockTask;
                clockTask.calendarId = a.calendarId;
                try {
                    WorkCalendar clock(p, clockTask);
                    row.durationMinutes = clock.offset(*row.finish) - clock.offset(*row.start);
                } catch (const std::exception &error) {
                    row.reason = std::string("지원 달력의 기간 산정 불가: ") + error.what();
                }
            }
        }
        result.push_back(std::move(row));
    }
    return result;
}
ActivityProgress activityProgress(const Project &p, const std::string &taskId) {
    ActivityProgress result;
    if (!p.statusAt)
        return result;
    const auto *fact = progressAsOf(p, taskId);
    const auto definition = std::find_if(p.activities.begin(), p.activities.end(), [&](const auto &a) {
        return a.kind == "steps" && a.taskId == taskId && a.physicalFromSteps;
    });
    if (definition != p.activities.end()) {
        result.physical = stepsPhysicalAsOf(p, taskId, *p.statusAt);
        result.physicalSource = "Steps 보고 시점의 동결 가중치";
    } else {
        const auto *cost = costAsOf(p, taskId, *p.statusAt);
        result.physical = cost ? cost->physicalBasisPoints : std::nullopt;
        result.physicalSource = "수동 물리 진척 보고";
    }
    if (fact) {
        const auto total = fact->actualDurationMinutes + fact->remainingMinutes;
        if (fact->actualFinish)
            result.duration = 10000;
        else if (total > 0)
            result.duration = int((fact->actualDurationMinutes * 10000 + total / 2) / total);
        if (fact->remainingWorkUnitMinutes) {
            std::int64_t actual = 0;
            bool known = false, missing = false;
            for (const auto &[key, entry] : periodActualsAsOf(p, *p.statusAt)) {
                (void)key;
                if (entry->taskId == taskId && !entry->resourceId.empty()) {
                    if (entry->workUnitMinutes) {
                        actual += *entry->workUnitMinutes;
                        known = true;
                    } else
                        missing = true;
                }
            }
            const auto totalWork = actual + *fact->remainingWorkUnitMinutes;
            if (known && !missing && totalWork > 0)
                result.work = int(std::floor(static_cast<long double>(actual) * 10000 / totalWork + 0.5L));
        }
    }
    return result;
}
} // namespace ppm
