#include "persistence/XmlActualExport.h"
#include "persistence/ProjectJson.h"
#include "domain/PeriodActuals.h"
#include "domain/ActualProvenance.h"
#include "domain/CalculationControl.h"
#include <QCryptographicHash>
#include <QDate>
#include <QJsonDocument>
#include <set>
#include <algorithm>
namespace ppm {
namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::XML_ACTUAL_EXPORT_INVALID, {s});
}
QString qs(const std::string &s) {
    return QString::fromStdString(s);
}
struct Sum {
    std::int64_t total = 0;
    bool known = false, complete = true, overflow = false;
    void add(std::optional<std::int64_t> value, std::int64_t limit) {
        if (!value) {
            complete = false;
            return;
        }
        known = true;
        if (total > limit - *value)
            overflow = true;
        else if (!overflow)
            total += *value;
    }
    std::optional<std::int64_t> value() const {
        if (!known || !complete)
            return std::nullopt;
        if (overflow)
            invalid("표준 실적 합계가 수량/금액 한도를 초과합니다.");
        return total;
    }
};
QJsonValue quantity(std::optional<std::int64_t> v) {
    return v ? QJsonValue(double(*v)) : QJsonValue(QJsonValue::Null);
}
} // namespace
XmlActualProjection projectXmlActuals(const Project &p) {
    if (!p.statusAt)
        throw AppError(ErrorCode::STATUS_DATE_REQUIRED);
    XmlActualProjection out;
    std::set<XmlActualPair> assignments;
    std::map<std::string, std::optional<std::int64_t>> durations;
    for (const auto &t : p.tasks)
        durations[t.id] = t.durationMinutes;
    for (const auto &a : p.assignments)
        assignments.emplace(a.taskId, a.resourceId);
    QJsonArray progressRows, costRows, dayRows, assignmentRows;
    for (const auto &e : p.progress)
        if (e.statusAt <= *p.statusAt) {
            auto &current = out.progress[e.taskId];
            if (!current || current->statusAt <= e.statusAt)
                current = &e;
        }
    for (const auto &[id, e] : out.progress) {
        calculationCheckpoint();
        if (e->actualDurationMinutes > 5000000 - e->remainingMinutes)
            invalid("작업 실제+잔여 기간이 XML 기간 한도를 초과합니다: " + id);
        progressRows.append(QJsonObject{
            {"taskId", qs(id)},
            {"entryId", qs(e->id)},
            {"statusAt", qs(e->statusAt)},
            {"actualStart", e->actualStart ? QJsonValue(qs(*e->actualStart)) : QJsonValue(QJsonValue::Null)},
            {"actualFinish", e->actualFinish ? QJsonValue(qs(*e->actualFinish)) : QJsonValue(QJsonValue::Null)},
            {"actualMinutes", double(e->actualDurationMinutes)},
            {"remainingMinutes", double(e->remainingMinutes)},
            {"projectedDuration", double(e->actualDurationMinutes + e->remainingMinutes)},
            {"nativePlanDuration", quantity(durations.at(id))}});
    }
    for (const auto &e : p.costEntries)
        if (e.statusAt <= *p.statusAt) {
            auto &current = out.cost[e.taskId];
            if (!current || current->statusAt <= e.statusAt)
                current = &e;
        }
    for (const auto &[id, e] : out.cost)
        costRows.append(
            QJsonObject{{"taskId", qs(id)}, {"entryId", qs(e->id)}, {"actualMinor", quantity(e->actualMinor)}});
    const auto selected = periodActualsAsOf(p, *p.statusAt);
    const auto origins = actualDerivations(p);
    bool hasEstimates = false;
    std::set<std::string> originIds;
    std::map<XmlActualPair, std::pair<Sum, Sum>> sums;
    std::set<std::string> unknownTaskWork, unknownResourceWork;
    for (const auto &[key, e] : selected) {
        (void)key;
        calculationCheckpoint();
        QJsonObject day{{"taskId", qs(e->taskId)},
                        {"resourceId", qs(e->resourceId)},
                        {"date", qs(e->date)},
                        {"entryId", qs(e->id)},
                        {"workUnitMinutes", quantity(e->workUnitMinutes)},
                        {"actualMinor", quantity(e->actualMinor)}};
        const auto &origin = origins.at(e->id);
        if (origin.any()) {
            hasEstimates = true;
            day["proportionalWork"] = !origin.workSourceId.empty();
            day["proportionalCost"] = !origin.costSourceId.empty();
            day["workAllocationEntryId"] = qs(origin.workSourceId);
            day["costAllocationEntryId"] = qs(origin.costSourceId);
            day["allocationReason"] = qs(e->reason);
            if (!origin.workSourceId.empty())
                originIds.insert(origin.workSourceId);
            if (!origin.costSourceId.empty())
                originIds.insert(origin.costSourceId);
        }
        dayRows.append(day);
        const XmlActualPair pair{e->taskId, e->resourceId};
        if (assignments.contains(pair)) {
            sums[pair].first.add(e->workUnitMinutes, maxWork);
            sums[pair].second.add(e->actualMinor, maxMoney);
        }
        if (!e->workUnitMinutes && !e->actualMinor) {
            if (!e->resourceId.empty() && !assignments.contains(pair)) {
                unknownTaskWork.insert(e->taskId);
                unknownResourceWork.insert(e->resourceId);
            }
            continue;
        }
        if (e->resourceId.empty())
            invalid("자원 없는 작업 직접 배분은 표준 배정 실적으로 내보낼 수 없습니다: " + e->taskId + " / " + e->date);
        if (!assignments.contains(pair))
            invalid("관측된 배정이 현재 계획에 없습니다. 표준 실적 출력 전에 배정을 명시 복구하세요: " + e->taskId +
                    " / " + e->resourceId);
        const auto nextDay = QDate::fromString(qs(e->date), Qt::ISODate).addDays(1);
        if (!nextDay.isValid() || !validDate(nextDay.toString(Qt::ISODate).toStdString()))
            invalid("다음 날 일별 버킷 경계를 표현할 수 없습니다: " + e->date);
        out.days[pair].push_back(e);
    }
    std::map<std::string, Sum> taskSums, resourceSums;
    for (const auto &a : p.assignments) {
        calculationCheckpoint();
        const XmlActualPair pair{a.taskId, a.resourceId};
        const auto work = sums[pair].first.value(), cost = sums[pair].second.value();
        out.work[pair] = work;
        out.assignmentCost[pair] = cost;
        taskSums[a.taskId].add(work, maxWork);
        resourceSums[a.resourceId].add(work, maxWork);
        assignmentRows.append(QJsonObject{{"taskId", qs(a.taskId)},
                                          {"resourceId", qs(a.resourceId)},
                                          {"actualWork", quantity(work)},
                                          {"actualCost", quantity(cost)}});
    }
    for (const auto &t : p.tasks)
        out.taskWork[t.id] = unknownTaskWork.contains(t.id) ? std::nullopt : taskSums[t.id].value();
    for (const auto &r : p.resources)
        out.resourceWork[r.id] = unknownResourceWork.contains(r.id) ? std::nullopt : resourceSums[r.id].value();
    QJsonArray balances;
    for (const auto &[id, b] : actualCostAllocations(p, *p.statusAt))
        balances.append(QJsonObject{{"taskId", qs(id)},
                                    {"cumulative", quantity(b.cumulative)},
                                    {"datedAllocated", double(b.allocated)},
                                    {"unallocated", quantity(b.unallocated)}});
    const auto digest = QCryptographicHash::hash(QJsonDocument(json::encodeProject(p)).toJson(QJsonDocument::Compact),
                                                 QCryptographicHash::Sha256)
                            .toHex();
    out.report = QJsonObject{{"mode", "observed-actuals-v1"},
                             {"statusAt", qs(*p.statusAt)},
                             {"sourceInputSha256", QString::fromLatin1(digest)},
                             {"taskProgress", progressRows},
                             {"taskCost", costRows},
                             {"assignmentDays", dayRows},
                             {"assignmentTotals", assignmentRows},
                             {"costReconciliation", balances},
                             {"datePolicy", "civil-date-bucket-not-measured-intraday"},
                             {"unknownPolicy", "omit-not-zero"}};
    if (hasEstimates) {
        QJsonArray sources;
        for (const auto &e : p.periodActuals)
            if (originIds.contains(e.id))
                sources.append(QJsonObject{{"entryId", qs(e.id)},
                                           {"reason", qs(e.reason)},
                                           {"workUnitMinutes", quantity(e.workUnitMinutes)},
                                           {"actualMinor", quantity(e.actualMinor)}});
        out.report["allocationSources"] = sources;
    }
    auto note = [&](const char *path, const char *message) {
        out.diagnostics.append(QJsonObject{{"path", path}, {"message", message}});
    };
    note(
        "/Project/StatusDate",
        "현재 상태일의 최신 관측/정정만 표준 실적 요소로 출력합니다. 미래 보고와 전체 원장은 source.ppm에 보존합니다.");
    note("/Project/Assignments/TimephasedData", "일별 실적은 00:00~다음 날00:00 버킷(Type2/6, Unit2)입니다. 실제 "
                                                "착수/완료 시각이 아니며 미보고는 생략하고 명시0은 보존합니다.");
    note("/Project/ActualWork", "일별 관측의 보고 합계입니다. 일부 보고가 미보고이거나 현재 배정에 관측이 없으면 해당 "
                                "scalar 합계는 생략합니다. 보고 없는 날짜를 0으로 추정하지 않습니다.");
    note("/Project/ActualCost", "작업 누계 AC와 배정별 날짜 배분은 다른 범위이며 더하지 않습니다. 미배분 잔액은 "
                                "conversion.json 대사를 확인하세요.");
    note("/Project/unknown", "외부 제품은 생략 값을 기본값이나 재계산 값으로 해석할 수 있습니다. XML 원문과 source.ppm "
                             "및 대사를 함께 확인하세요.");
    note("/Project/Assignments",
         "배정 실제 시작/완료·초과 공수/원가·진척률은 추정하지 않습니다. 계획 Type1 분포는 실적 모드에서 생략합니다.");
    if (hasEstimates)
        note("/Project/Assignments/TimephasedData",
             "비례 추정 자료가 포함됩니다. 표준 XML 요소만으로 측정/추정 근거를 복원하지 못하므로 conversion.json의 "
             "척도별 플래그와 source.ppm의 배분 사유를 함께 보관하세요.");
    note("/Project/Tasks/Duration", "보고 작업의 Duration은 명시 실제+잔여 기간으로 투영합니다. native 계획과 사용자 "
                                    "필드/수식의 계획 기준 결과는 source.ppm에 보존됩니다.");
    return out;
}
} // namespace ppm
