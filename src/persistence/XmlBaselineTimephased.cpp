#include "persistence/XmlBaselineTimephased.h"
#include "persistence/XmlBaselineExchange.h"
#include "calculation/Resources.h"
#include "calculation/Costs.h"
#include "domain/Classification.h"
#include "domain/XmlBaselines.h"
#include "domain/CalculationControl.h"
#include <QDate>
#include <QJsonObject>
#include <algorithm>
#include <chrono>
#include <set>
namespace ppm {
namespace {
[[noreturn]] void bad(const std::string &s) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"기준선 시간분포: " + s});
}
std::int64_t minute(const std::string &s) {
    using namespace std::chrono;
    if (!validStamp(s))
        bad("동결 구간 날짜 오류");
    const auto d = year{std::stoi(s.substr(0, 4))} / month{unsigned(std::stoi(s.substr(5, 2)))} /
                   day{unsigned(std::stoi(s.substr(8, 2)))};
    return sys_days{d}.time_since_epoch().count() * 1440 + std::stoi(s.substr(11, 2)) * 60 + std::stoi(s.substr(14, 2));
}
struct Series {
    std::map<std::string, std::int64_t> work;
    std::map<std::string, Money> cost;
};
void add(Money &a, Money b) {
    if (!a || !b) {
        a.reset();
        return;
    }
    if (*b < 0 || *b > maxMoney || *a > maxMoney - *b)
        bad("일별 원가 합계 한도");
    *a += *b;
}
} // namespace
void addNativeBaselineTimephased(const Project &current, const Baseline &b, std::vector<XmlBaselineRow> &rows,
                                 std::size_t first, std::size_t &periodCount, QJsonArray &diagnostics) {
    calculationCheckpoint();
    Project frozen;
    static_cast<PlanInput &>(frozen) = b.input;
    frozen.documentId = b.sourceDocumentId;
    frozen.revision = b.inputRevision;
    ScheduleResult schedule;
    schedule.documentId = frozen.documentId;
    schedule.inputRevision = frozen.revision;
    schedule.profile = frozen.calculationProfile;
    schedule.engineVersion = b.engineVersion;
    schedule.status = CalculationStatus::Valid;
    schedule.tasks = b.schedule;
    schedule.projectFinish = b.projectFinish;
    std::map<std::string, std::size_t> index;
    std::map<std::size_t, Series> series;
    for (std::size_t i = first; i < rows.size(); ++i)
        index.emplace(xmlBaselineRowKey(rows[i].kind, rows[i].id, rows[i].resourceId), i);
    std::map<std::string, std::string> parents, taskGroups;
    for (const auto &w : current.wbsNodes)
        parents[w.id] = w.parentId;
    for (const auto &t : current.tasks)
        taskGroups[t.id] = t.wbsId;
    auto destinations = [&](const std::string &task, const std::string &resource) {
        std::vector<std::size_t> result;
        auto push = [&](const std::string &kind, const std::string &id, const std::string &rid = "") {
            const auto it = index.find(xmlBaselineRowKey(kind, id, rid));
            if (it != index.end() && (kind != "wbs" || rows[it->second].work))
                result.push_back(it->second);
        };
        push("project", "");
        push("task", task);
        if (!resource.empty()) {
            push("resource", resource);
            push("assignment", task, resource);
        }
        auto group = taskGroups.contains(task) ? taskGroups.at(task) : "";
        int depth = 0;
        while (!group.empty()) {
            if (++depth > 64 || !parents.contains(group))
                bad("현재 WBS 연결 오류");
            push("wbs", group);
            group = parents.at(group);
        }
        return result;
    };
    std::size_t eventCount = 0;
    for (const auto &span : resourceOccupancies(frozen, schedule)) {
        calculationCheckpoint();
        if (span.units < 0 || minute(span.finish) <= minute(span.start))
            bad("동결 공수 구간 오류");
        for (auto i : destinations(span.taskId, span.resourceId)) {
            auto &events = series[i].work;
            eventCount += events.try_emplace(span.start, 0).second;
            eventCount += events.try_emplace(span.finish, 0).second;
            if (eventCount > 400000)
                bad("구간 경계 한도400,000개 초과 · 상세 자료를 나눠 내보내세요.");
            events.at(span.start) += span.units;
            events.at(span.finish) -= span.units;
        }
    }
    if (b.input.currency == current.currency && !b.schedule.empty()) {
        auto start = b.schedule.front().start.substr(0, 10), finish = b.projectFinish.substr(0, 10);
        for (const auto &r : b.schedule)
            start = std::min(start, r.start.substr(0, 10));
        for (const auto &day : plannedCostDays(frozen, schedule, start, finish))
            for (auto i : destinations(day.taskId, day.resourceId)) {
                auto [it, fresh] = series[i].cost.try_emplace(day.date, 0);
                if (fresh && ++eventCount > 400000)
                    bad("일별 원가/공수 경계 한도 초과");
                add(it->second, day.amount);
            }
    }
    std::size_t workBlocks = 0, costBlocks = 0, unknownCosts = 0;
    for (std::size_t i = first; i < rows.size(); ++i) {
        calculationCheckpoint();
        auto &r = rows[i];
        if (r.kind == "wbs" && !r.work) {
            diagnostics.append(QJsonObject{
                {"path", "/Baseline/" + QString::number(r.number) + "/" + QString::fromStdString(r.id)},
                {"message", "현재 WBS에 동결 기준선이 없는 구성원이 있어 요약 시간분포를 생성하지 않습니다."}});
            continue;
        }
        auto append = [&](bool cost, const std::string &start, const std::string &finish,
                          std::optional<std::string> value) {
            if (++periodCount > 100000)
                bad("전체 기준선 시간분포 한도100,000개 초과 · 자료를 나눠 내보내세요.");
            r.timephased.push_back(
                {xmlBaselineTimeType(r.number, r.kind, cost), "native", start, finish, value, cost ? 2 : 0});
            r.timephasedOutside = true;
            if (cost)
                ++costBlocks;
            else
                ++workBlocks;
        };
        const auto found = series.find(i);
        std::int64_t totalWork = 0;
        Money totalCost = 0;
        if (found != series.end()) {
            auto &s = found->second;
            std::int64_t rate = 0;
            std::string previous;
            for (const auto &[stamp, delta] : s.work) {
                calculationCheckpoint();
                if (!previous.empty() && previous != stamp) {
                    const auto length = minute(stamp) - minute(previous);
                    if (rate < 0 || (rate && length > maxWork / rate))
                        bad("공수 구간 범위 초과");
                    const auto amount = length * rate;
                    if (amount > maxWork - totalWork)
                        bad("공수 합계 범위 초과");
                    totalWork += amount;
                    append(false, normalizeFieldDateTime(previous), normalizeFieldDateTime(stamp),
                           xmlBaselineDurationText(amount * 10000));
                }
                rate += delta;
                previous = stamp;
            }
            if (rate != 0)
                bad("공수 시작/완료 경계 대사 오류");
            for (const auto &[date, amount] : s.cost) {
                const auto next = QDate::fromString(QString::fromStdString(date), Qt::ISODate)
                                      .addDays(1)
                                      .toString(Qt::ISODate)
                                      .toStdString();
                if (!validDate(next))
                    bad("원가 다음 날짜 범위 초과");
                append(true, date + "T00:00:00", next + "T00:00:00",
                       amount ? std::optional{xmlBaselineMoneyText(*amount, current.currency)} : std::nullopt);
                if (!amount)
                    ++unknownCosts;
                add(totalCost, amount);
            }
        }
        if (r.work && *r.work != totalWork * 10000)
            bad("동결 총공수와 상세 공수가 다릅니다.");
        if (b.input.currency == current.currency && r.cost && (!totalCost || r.cost != totalCost))
            bad("동결 총원가와 일별 원가가 다릅니다.");
    }
    diagnostics.append(QJsonObject{{"path", "/Baseline/" + QString::number(b.version - 1) + "/TimephasedData"},
                                   {"message", "동결 실행 공수 구간과 native 일별 반올림/고정비·자재/경비 원가를 표준 "
                                               "시간분포로 출력했습니다. 미확정 일별 원가는 Value를 생략합니다."},
                                   {"workBlocks", int(workBlocks)},
                                   {"costBlocks", int(costBlocks)},
                                   {"unknownCostBlocks", int(unknownCosts)}});
}
} // namespace ppm
