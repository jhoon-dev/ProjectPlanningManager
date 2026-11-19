#include "calculation/EarlyLateFormulaValues.h"
#include "calculation/WorkCalendar.h"
#include "domain/Classification.h"
namespace ppm {
void addEarlyLateFormulaValues(const Project &p, const ScheduleResult &result, XmlScheduleValues &out) {
    if (!hasXmlEarlyLateReferences(p, true))
        return;
    auto issue = [](const std::exception &e) {
        if (const auto *error = dynamic_cast<const AppError *>(&e); error && !error->diagnostic().arguments.empty())
            return error->diagnostic().arguments.front();
        return std::string(e.what());
    };
    auto range = [](const auto &dates, auto &start, auto &finish) {
        if (!start || dates.first < *start)
            start = dates.first;
        if (!finish || dates.second > *finish)
            finish = dates.second;
    };
    try {
        for (const auto &row : result.tasks) {
            calculationCheckpoint();
            if (result.isForecast && row.completedActual) {
                out.earlyLateCompleted.insert(row.taskId);
                continue;
            }
            const auto start = result.isForecast ? row.remainingStart.value_or(row.start) : row.start;
            if (!validStamp(start) || !validStamp(row.finish) || start > row.finish ||
                (result.isForecast && (!p.statusAt || start < *p.statusAt)))
                throw AppError(ErrorCode::FORMULA_INVALID, {"Early 잔여/계획 날짜 구간을 확인하세요."});
            const auto dates = std::pair{normalizeFieldDateTime(start), normalizeFieldDateTime(row.finish)};
            out.earlyTasks.emplace(row.taskId, dates);
            range(dates, out.earlyStart, out.earlyFinish);
            if (!hasScheduleFloat(result, row))
                out.lateIssue = "현재 일정의 후진 날짜가 미산정입니다. " +
                                (result.resourceLeveled ? result.levelingFloatReason : result.logicalFloatReason);
        }
    } catch (const CalculationCancelled &) {
        throw;
    } catch (const std::exception &e) {
        out.earlyTasks.clear();
        out.earlyStart.reset();
        out.earlyFinish.reset();
        out.earlyLateCompleted.clear();
        out.earlyLateIssue = issue(e);
        return;
    }
    if (!out.lateIssue.empty())
        return;
    try {
        WorkCalendar clock(p);
        for (const auto &row : result.tasks) {
            calculationCheckpoint();
            if (out.earlyLateCompleted.contains(row.taskId))
                continue;
            const auto start = result.isForecast ? row.remainingStart.value_or(row.start) : row.start;
            if (row.lateStartMinutes < 0 || row.lateFinishMinutes < row.lateStartMinutes ||
                row.lateFinishMinutes > clock.maxOffset())
                throw AppError(ErrorCode::FORMULA_INVALID, {"Late 날짜 좌표 범위를 확인하세요."});
            // PDM/effort coordinates are civil minutes, basic CPM coordinates are working minutes.
            // Preserve the exact early boundary when coordinates coincide, including non-working milestones.
            const auto lateStart = row.lateStartMinutes == clock.offset(start)
                                       ? start
                                       : clock.boundary(row.lateStartMinutes, start == row.finish);
            const auto lateFinish = row.lateFinishMinutes == clock.offset(row.finish)
                                        ? row.finish
                                        : clock.boundary(row.lateFinishMinutes, true);
            if (lateStart < start || lateFinish < row.finish || lateStart > lateFinish ||
                (result.projectFinish && lateFinish > *result.projectFinish))
                throw AppError(ErrorCode::FORMULA_INVALID, {"Late 날짜가 현재 배치·프로젝트 완료 구간과 다릅니다."});
            const auto dates = std::pair{normalizeFieldDateTime(lateStart), normalizeFieldDateTime(lateFinish)};
            out.lateTasks.emplace(row.taskId, dates);
            range(dates, out.lateStart, out.lateFinish);
        }
    } catch (const CalculationCancelled &) {
        throw;
    } catch (const std::exception &e) {
        out.lateTasks.clear();
        out.lateStart.reset();
        out.lateFinish.reset();
        out.lateIssue = issue(e);
        out.lateMissing = false;
    }
}
} // namespace ppm
