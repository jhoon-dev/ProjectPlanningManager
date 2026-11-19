#include "calculation/ScheduleFormulaScope.h"
#include "calculation/BaselineFormulaValues.h"
#include "calculation/EarlyLateFormulaValues.h"
#include "domain/Classification.h"
#include "domain/PeriodActuals.h"
#include "calculation/Resources.h"
#include "calculation/ResourceAvailabilityQuery.h"
#include "domain/XmlFormula.h"
#include "calculation/WorkCalendar.h"
#include <atomic>
#include <set>
namespace ppm {
namespace {
void actualValues(const Project &p, XmlScheduleValues &out) {
    if (!hasXmlActualReferences(p, true))
        return;
    out.actualCaption = "실적 · 상태일 " + p.statusAt.value_or("미설정");
    if (!p.statusAt) {
        out.actualMissing = true;
        out.actualIssue = "실적 기준 상태일을 설정하세요.";
        return;
    }
    if (p.tasks.empty()) {
        out.actualMissing = true;
        out.actualIssue = "실적 대상 작업이 없습니다.";
        return;
    }
    try {
        if (!validStamp(*p.statusAt))
            throw AppError(ErrorCode::FORMULA_INVALID, {"실적 기준 상태일 형식을 확인하세요."});
        out.actualStatusAt = normalizeFieldDateTime(*p.statusAt);
        std::set<std::string> ids;
        for (const auto &t : p.tasks)
            ids.insert(t.id);
        std::map<std::string, const ProgressEntry *> selected;
        for (const auto &e : p.progress) {
            calculationCheckpoint();
            if (e.statusAt > *p.statusAt)
                continue;
            if (!validStamp(e.statusAt) || !ids.contains(e.taskId))
                throw AppError(ErrorCode::FORMULA_INVALID, {"실적 보고의 상태일·작업 ID가 현재 입력과 다릅니다."});
            auto &old = selected[e.taskId];
            if (!old || old->statusAt <= e.statusAt)
                old = &e;
        }
        bool complete = true;
        for (const auto &t : p.tasks) {
            calculationCheckpoint();
            auto i = selected.find(t.id);
            if (i == selected.end()) {
                complete = false;
                continue;
            }
            const auto &e = *i->second;
            if (e.actualDurationMinutes < 0 || e.actualDurationMinutes > 5000000 ||
                (!e.actualStart && e.actualDurationMinutes != 0))
                throw AppError(ErrorCode::FORMULA_INVALID, {"실적 보고의 실제 기간을 확인하세요."});
            if ((e.actualStart && (!validStamp(*e.actualStart) || *e.actualStart > e.statusAt)) ||
                (e.actualFinish && (!e.actualStart || !validStamp(*e.actualFinish) ||
                                    *e.actualFinish < *e.actualStart || *e.actualFinish > e.statusAt)))
                throw AppError(ErrorCode::FORMULA_INVALID, {"실적 보고의 실제 시작·완료일을 확인하세요."});
            std::optional<std::string> start, finish;
            if (e.actualStart)
                start = normalizeFieldDateTime(*e.actualStart);
            if (e.actualFinish)
                finish = normalizeFieldDateTime(*e.actualFinish);
            out.actualTasks.emplace(t.id, std::pair{start, finish});
            out.actualDurations.emplace(t.id, e.actualDurationMinutes);
            if (start && (!out.actualStart || *start < *out.actualStart))
                out.actualStart = start;
            if (finish && (!out.actualFinish || *finish > *out.actualFinish))
                out.actualFinish = finish;
            if (!finish)
                complete = false;
        }
        if (!complete)
            out.actualFinish.reset();
    } catch (const CalculationCancelled &) {
        throw;
    } catch (const AppError &e) {
        out.actualTasks.clear();
        out.actualDurations.clear();
        out.actualStart.reset();
        out.actualFinish.reset();
        out.actualIssue = e.diagnostic().arguments.empty() ? e.code() : e.diagnostic().arguments.front();
    }
}
void actualWorkValues(const Project &p, XmlScheduleValues &out) {
    if (!hasXmlActualQuantityReferences(p, true) || !p.statusAt || !out.actualIssue.empty())
        return;
    out.actualCaption += " · 보고 공수 합계(미보고 제외)";
    try {
        std::set<std::string> tasks, resources;
        for (const auto &t : p.tasks)
            tasks.insert(t.id);
        for (const auto &r : p.resources)
            resources.insert(r.id);
        auto add = [](std::int64_t &total, std::int64_t value) {
            if (value < 0 || value > maxWork || total > maxWork - value)
                throw AppError(ErrorCode::FORMULA_INVALID, {"보고된 실제 공수의 합계 범위를 초과했습니다."});
            total += value;
        };
        for (const auto &[key, e] : periodActualsAsOf(p, *p.statusAt)) {
            (void)key;
            calculationCheckpoint();
            if (!validStamp(e->statusAt) || !validDate(e->date) || e->date > e->statusAt.substr(0, 10) ||
                !tasks.contains(e->taskId) ||
                (!e->resourceId.empty() && (!resources.contains(e->resourceId) ||
                                            (e->resourceType != "labor" && e->resourceType != "equipment"))))
                throw AppError(ErrorCode::FORMULA_INVALID, {"기간 실적의 상태일·날짜·작업/자원을 확인하세요."});
            if (!e->workUnitMinutes)
                continue;
            if (e->resourceId.empty())
                throw AppError(ErrorCode::FORMULA_INVALID, {"실제 공수에는 자원이 필요합니다."});
            const auto amount = *e->workUnitMinutes;
            if (!out.actualWork)
                out.actualWork = 0;
            add(*out.actualWork, amount);
            add(out.actualTaskWork[e->taskId], amount);
            add(out.actualResourceWork[e->resourceId], amount);
            add(out.actualAssignmentWork[{e->taskId, e->resourceId}], amount);
        }
    } catch (const CalculationCancelled &) {
        throw;
    } catch (const AppError &e) {
        out.actualTaskWork.clear();
        out.actualResourceWork.clear();
        out.actualAssignmentWork.clear();
        out.actualWork.reset();
        out.actualWorkIssue = e.diagnostic().arguments.empty() ? e.code() : e.diagnostic().arguments.front();
    }
}
void remainingValues(const Project &p, const ScheduleResult *result, XmlScheduleValues &out) {
    if (!hasXmlRemainingReferences(p, true))
        return;
    out.remainingCaption = "잔여 예측 · 상태일 " + p.statusAt.value_or("미설정");
    auto missing = [&](const std::string &reason) {
        out.remainingMissing = true;
        out.remainingIssue = reason;
    };
    if (!p.statusAt) {
        missing("잔여 참조에는 상태일이 필요합니다.");
        return;
    }
    if (!result || result->status == CalculationStatus::Cancelled) {
        missing("현재 입력의 잔여 예측 계산이 필요합니다.");
        return;
    }
    if (result->documentId != p.documentId || result->inputRevision != p.revision ||
        result->profile != p.calculationProfile || result->status == CalculationStatus::Stale ||
        (result->isForecast && result->statusAt != p.statusAt)) {
        out.remainingIssue = "현재 문서/입력과 일치하지 않는 오래된 잔여 예측입니다.";
        return;
    }
    if (result->status != CalculationStatus::Valid) {
        out.remainingMissing = result->status == CalculationStatus::Incomplete;
        out.remainingIssue = "유효한 잔여 예측 결과가 없습니다.";
        return;
    }
    if (!result->isForecast) {
        missing("계획 결과를 잔여 예측으로 사용할 수 없습니다. 잔여 예측을 계산하세요.");
        return;
    }
    if (result->resourceLeveled)
        out.remainingCaption += " · 자원 평준화";
    if (p.progressPolicy)
        out.remainingCaption += " · 선택 진척 정책";
    try {
        if (!validStamp(*p.statusAt))
            throw AppError(ErrorCode::FORMULA_INVALID, {"잔여 기준 상태일 형식을 확인하세요."});
        std::map<std::string, const Task *> tasks;
        for (const auto &t : p.tasks)
            tasks.emplace(t.id, &t);
        std::set<std::string> seen;
        for (const auto &row : result->tasks) {
            calculationCheckpoint();
            if (!tasks.contains(row.taskId) || !seen.insert(row.taskId).second)
                throw AppError(ErrorCode::FORMULA_INVALID, {"잔여 예측의 작업 ID가 다르거나 중복됩니다."});
            if (!validStamp(row.start) || !validStamp(row.finish) || row.start > row.finish)
                throw AppError(ErrorCode::FORMULA_INVALID, {"잔여 예측의 날짜 형식을 확인하세요."});
            std::int64_t duration = 0;
            if (!row.completedActual) {
                const auto start = row.remainingStart.value_or(row.start);
                if (!validStamp(start) || !validStamp(row.finish) || start < *p.statusAt || start > row.finish)
                    throw AppError(ErrorCode::FORMULA_INVALID, {"잔여 예측의 시작/완료 구간을 확인하세요."});
                WorkCalendar calendar(p, *tasks.at(row.taskId));
                duration = calendar.offset(row.finish) - calendar.offset(start);
                if (duration < 0 || duration > 5000000)
                    throw AppError(ErrorCode::FORMULA_INVALID, {"잔여 기간 범위를 초과했습니다."});
            }
            if (row.completedActual)
                out.remainingCompleted.insert(row.taskId);
            else
                out.remainingTasks.emplace(row.taskId,
                                           std::pair{normalizeFieldDateTime(row.remainingStart.value_or(row.start)),
                                                     normalizeFieldDateTime(row.finish)});
            out.remainingDurations[row.taskId] = duration;
            out.remainingTaskWork[row.taskId] = 0;
        }
        if (seen.size() != p.tasks.size() || p.tasks.empty())
            throw AppError(ErrorCode::FORMULA_INVALID, {"전체 작업의 잔여 예측이 필요합니다."});
        for (const auto &r : p.resources)
            out.remainingResourceWork[r.id] = 0;
        for (const auto &a : p.assignments)
            out.remainingAssignmentWork[{a.taskId, a.resourceId}] = 0;
        out.remainingWork = 0;
        auto add = [](std::int64_t &sum, std::int64_t value) {
            if (value < 0 || value > maxWork || sum > maxWork - value)
                throw AppError(ErrorCode::FORMULA_INVALID, {"잔여 공수 합계 범위를 초과했습니다."});
            sum += value;
        };
        for (const auto &span : resourceOccupancies(p, *result)) {
            calculationCheckpoint();
            const auto key = std::pair{span.taskId, span.resourceId};
            if (!out.remainingAssignmentWork.contains(key) || span.units < 0 || !validStamp(span.start) ||
                !validStamp(span.finish) || span.start < *p.statusAt || span.start > span.finish)
                throw AppError(ErrorCode::FORMULA_INVALID, {"잔여 공수의 자원/작업 구간을 확인하세요."});
            const auto civil = [](const std::string &stamp) {
                using namespace std::chrono;
                const auto date = year{std::stoi(stamp.substr(0, 4))} / month{unsigned(std::stoi(stamp.substr(5, 2)))} /
                                  day{unsigned(std::stoi(stamp.substr(8, 2)))};
                return sys_days{date}.time_since_epoch().count() * 1440 + std::stoi(stamp.substr(11, 2)) * 60 +
                       std::stoi(stamp.substr(14, 2));
            };
            const auto length = civil(span.finish) - civil(span.start);
            if (span.units && length > maxWork / span.units)
                throw AppError(ErrorCode::FORMULA_INVALID, {"잔여 공수 구간 범위를 초과했습니다."});
            const auto work = length * span.units;
            add(*out.remainingWork, work);
            add(out.remainingTaskWork.at(span.taskId), work);
            add(out.remainingResourceWork.at(span.resourceId), work);
            add(out.remainingAssignmentWork.at(key), work);
        }
    } catch (const CalculationCancelled &) {
        throw;
    } catch (const std::exception &e) {
        out.remainingTasks.clear();
        out.remainingCompleted.clear();
        out.remainingDurations.clear();
        out.remainingTaskWork.clear();
        out.remainingResourceWork.clear();
        out.remainingAssignmentWork.clear();
        out.remainingWork.reset();
        out.remainingIssue = e.what();
        if (const auto *error = dynamic_cast<const AppError *>(&e); error && !error->diagnostic().arguments.empty())
            out.remainingIssue = error->diagnostic().arguments.front();
    }
}
} // namespace
std::shared_ptr<const XmlScheduleValues> makeScheduleFormulaValues(const Project &p, const ScheduleResult *result,
                                                                   const ScheduleResult *remaining) {
    if (!hasXmlScheduleReferences(p, true))
        return {};
    static std::atomic<std::uint64_t> next{0};
    auto out = std::make_shared<XmlScheduleValues>();
    out->cacheKey = "xml-schedule/" + std::to_string(++next);
    if (hasXmlResourceLoadFunctions(p, true)) {
        out->resourceLoadIssue = "현재 입력의 유효한 일정 계산이 필요합니다.";
        if (result) {
            out->resourceLoadMissing =
                result->status == CalculationStatus::Incomplete || result->status == CalculationStatus::Cancelled;
            try {
                auto query = std::make_shared<const ResourceAvailabilityQuery>(p, *result);
                out->resourceLoad = [query](const std::string &id, const std::string &at, bool remaining) {
                    const auto point = query->at(id, at);
                    auto units = remaining ? point.remaining : point.demand;
                    return XmlResourceLoadValue{units, units ? "" : point.issue, !units};
                };
                out->resourceLoadIssue.clear();
            } catch (const CalculationCancelled &) {
                throw;
            } catch (const std::exception &e) {
                out->resourceLoadIssue = e.what();
            }
        }
    }
    addBaselineFormulaValues(p, *out);
    actualValues(p, *out);
    actualWorkValues(p, *out);
    remainingValues(p, remaining ? remaining : result, *out);
    if (!hasXmlComputedReferences(p, true))
        return out;
    if (!result) {
        out->missing = true;
        out->caption = "일정 계산 필요";
        out->issue = "현재 입력의 일정을 계산한 후 다시 확인하세요.";
        return out;
    }
    if (result->status == CalculationStatus::Cancelled) {
        out->missing = true;
        out->caption = "계산 취소";
        out->issue = "일정 계산이 취소되어 계산된 날짜가 없습니다.";
        return out;
    }
    out->caption = result->isForecast ? "상태일 예측" : "현재 계획";
    if (result->documentId != p.documentId || result->inputRevision != p.revision ||
        result->profile != p.calculationProfile || (result->isForecast && result->statusAt != p.statusAt) ||
        result->status == CalculationStatus::Stale) {
        out->issue = "현재 문서/입력과 일치하지 않는 오래된 일정 결과입니다.";
        return out;
    }
    if (result->status != CalculationStatus::Valid) {
        out->missing = result->status == CalculationStatus::Incomplete;
        out->issue = out->missing ? "일정 입력이 불완전하여 계산된 날짜가 없습니다."
                                  : "일정 충돌로 계산된 날짜를 사용할 수 없습니다.";
        return out;
    }
    std::set<std::string> ids;
    for (const auto &t : p.tasks)
        ids.insert(t.id);
    try {
        for (const auto &r : result->tasks) {
            calculationCheckpoint();
            if (!ids.contains(r.taskId) || out->tasks.contains(r.taskId))
                throw AppError(ErrorCode::FORMULA_INVALID, {"일정 행 ID가 현재 입력과 다르거나 중복됩니다."});
            auto start = normalizeFieldDateTime(r.start.size() == 10 ? r.start + "T00:00:00" : r.start),
                 finish = normalizeFieldDateTime(r.finish.size() == 10 ? r.finish + "T00:00:00" : r.finish);
            if (start > finish)
                throw AppError(ErrorCode::FORMULA_INVALID, {"일정 시작/종료 순서 오류"});
            out->tasks.emplace(r.taskId, std::pair{start, finish});
            if (!out->start || start < *out->start)
                out->start = start;
        }
        if (out->tasks.size() != p.tasks.size() || p.tasks.empty() || !result->projectFinish)
            throw AppError(ErrorCode::FORMULA_INVALID, {"전체 작업/프로젝트 완료일이 없는 일정 결과입니다."});
        out->finish = normalizeFieldDateTime(result->projectFinish->size() == 10 ? *result->projectFinish + "T00:00:00"
                                                                                 : *result->projectFinish);
    } catch (const CalculationCancelled &) {
        throw;
    } catch (const std::exception &e) {
        out->tasks.clear();
        out->start.reset();
        out->finish.reset();
        out->issue = e.what();
    }
    if (out->issue.empty())
        addEarlyLateFormulaValues(p, *result, *out);
    return out;
}
ScheduleFormulaScope::ScheduleFormulaScope(const Project &p, const ScheduleResult *result, ScheduleFormulaMode mode,
                                           bool calculateMissing) {
    if (!hasXmlScheduleReferences(p, true))
        return;
    if (mode == ScheduleFormulaMode::Inherit && !result)
        if (auto previous = XmlScheduleScope::current(p)) {
            values_ = std::move(previous);
            return;
        }
    if (mode == ScheduleFormulaMode::Plan && result && result->isForecast)
        result = nullptr;
    if (!result && calculateMissing && hasXmlComputedReferences(p, true)) {
        XmlScheduleScope blocked;
        calculated_ = mode == ScheduleFormulaMode::Plan ? calculatePlan(p) : calculate(p);
        result = &*calculated_;
    }
    if (calculated_ && calculated_->status == CalculationStatus::Cancelled)
        throw CalculationCancelled{};
    const bool currentPlan = result && !result->isForecast && result->status == CalculationStatus::Valid &&
                             result->documentId == p.documentId && result->inputRevision == p.revision &&
                             result->profile == p.calculationProfile;
    if (mode == ScheduleFormulaMode::Plan && calculateMissing && p.statusAt && hasXmlRemainingReferences(p, true) &&
        (!result || currentPlan)) {
        XmlScheduleScope blocked;
        remainingCalculated_ = calculate(p);
        if (remainingCalculated_->status == CalculationStatus::Cancelled)
            throw CalculationCancelled{};
    }
    values_ = makeScheduleFormulaValues(p, result, remainingCalculated_ ? &*remainingCalculated_ : nullptr);
    scope_ = std::make_unique<XmlScheduleScope>(p, values_);
}
} // namespace ppm
