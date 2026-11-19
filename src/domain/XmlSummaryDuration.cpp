#include "domain/XmlSummaryDuration.h"
#include "domain/CalculationControl.h"
#include "domain/XmlCalendar.h"
#include "domain/XmlSchedule.h"
#include "domain/Classification.h"
#include "domain/XmlBaselines.h"
#include <set>
namespace ppm {
bool xmlDurationReference(const std::string &key) {
    return key == "builtin:actualduration" || key == "builtin:remainingduration" ||
           xmlBaselineBaseKey(key) == "builtin:baselineduration";
}
bool hasXmlSummaryDurationReferences(const PlanInput &p, bool projectOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula)
                for (const auto &r : f.xmlFormula->references)
                    if (xmlDurationReference(r.key) &&
                        (r.scope == FieldScope::Project ||
                         (!projectOnly && scope == FieldScope::Task && r.scope == scope && f.xmlRollUp &&
                          f.xmlRollUp->calculationType == 2)))
                        return true;
    return false;
}
XmlFormulaResult xmlSummaryDuration(const PlanInput &p, const std::string &key, const std::vector<std::string> &taskIds,
                                    bool wholeBaseline) {
    calculationCheckpoint();
    auto values = XmlScheduleScope::current(p);
    if (!values)
        return {{}, "요약 기간의 일정·실적 평가 문맥이 필요합니다.", false};
    const auto base = xmlBaselineBaseKey(key);
    const bool baseline = base == "builtin:baselineduration";
    if (const auto slot = xmlBaselineSlot(key)) {
        const auto it = values->numberedBaselines.find(*slot);
        if (it == values->numberedBaselines.end())
            return {{}, "번호별 기준선 평가 문맥이 없습니다.", true};
        values = it->second;
    }
    const auto &issue = baseline                           ? values->baselineIssue
                        : base == "builtin:actualduration" ? values->actualIssue
                                                           : values->remainingIssue;
    const bool missing = baseline                           ? values->baselineMissing
                         : base == "builtin:actualduration" ? values->actualMissing
                                                            : values->remainingMissing;
    if (!issue.empty())
        return {{}, issue, missing};
    if (baseline && wholeBaseline && values->externalBaseline) {
        const auto r = values->externalBaselineRows.find(xmlBaselineRowKey("project", ""));
        if (r == values->externalBaselineRows.end() || !r->second.duration)
            return {{}, "외부 프로젝트 기준선의 명시 기간이 없습니다.", true};
        return {formatFieldNumber(*r->second.duration), {}, false};
    }
    std::vector<std::string> ids = taskIds;
    if (baseline && wholeBaseline) {
        ids.clear();
        for (const auto &[id, dates] : values->baselineTasks) {
            (void)dates;
            ids.push_back(id);
        }
    }
    if (ids.empty())
        return {{}, "요약 기간 대상 작업이 없습니다.", true};
    try {
        const auto *calendar = &p;
        if (baseline) {
            if (!values->baselineDurationCalendar)
                return {{}, "기준선의 동결 프로젝트 달력이 없습니다.", true};
            calendar = values->baselineDurationCalendar.get();
        }
        std::optional<std::string> begin, end;
        std::set<std::string> seen;
        const auto add = [&](const std::string &a, const std::string &b) {
            const auto start = normalizeFieldDateTime(a), finish = normalizeFieldDateTime(b);
            if (start > finish)
                throw AppError(ErrorCode::FORMULA_INVALID, {"요약 기간의 시작/완료 순서를 확인하세요."});
            if (!begin || start < *begin)
                begin = start;
            if (!end || finish > *end)
                end = finish;
        };
        for (const auto &id : ids) {
            calculationCheckpoint();
            if (!seen.insert(id).second)
                return {{}, "요약 기간 대상 작업이 중복됩니다.", false};
            if (baseline) {
                const auto it = values->baselineTasks.find(id);
                if (it == values->baselineTasks.end())
                    return {{}, "포함 작업의 동결 기준선 날짜가 없습니다.", true};
                add(it->second.first, it->second.second);
            } else if (base == "builtin:remainingduration") {
                if (!values->remainingDurations.contains(id))
                    return {{}, "포함 작업의 잔여 예측이 없습니다.", true};
                if (values->remainingCompleted.contains(id))
                    continue;
                const auto it = values->remainingTasks.find(id);
                if (it == values->remainingTasks.end())
                    return {{}, "미완료 작업의 잔여 날짜가 없습니다.", true};
                add(it->second.first, it->second.second);
            } else if (base == "builtin:actualduration") {
                const auto it = values->actualTasks.find(id);
                if (it == values->actualTasks.end())
                    return {{}, "포함 작업의 상태일 기준 실적 보고가 없습니다.", true};
                if (!it->second.first)
                    continue; // Explicit not-started/zero report; no dates are inferred.
                if (!it->second.second && !values->actualStatusAt)
                    return {{}, "진행 중 실제 요약 기간에는 상태일이 필요합니다.", true};
                add(*it->second.first, it->second.second.value_or(values->actualStatusAt.value_or("")));
            } else
                return {{}, "지원하지 않는 요약 기간 참조입니다.", false};
        }
        const auto minutes =
            begin ? xmlCalendarDifference(*calendar, FieldScope::Project, nullptr, nullptr, "", *begin, *end) : 0;
        if (minutes < 0 || minutes > 5000000LL * fieldNumberScale)
            return {{}, "요약 기간의 범위를 초과했습니다.", false};
        return {formatFieldNumber(minutes), {}, false};
    } catch (const CalculationCancelled &) {
        throw;
    } catch (const AppError &e) {
        return {{}, e.diagnostic().arguments.empty() ? e.code() : e.diagnostic().arguments.front(), false};
    }
}
} // namespace ppm
