#pragma once
#include "application/Document.h"
#include "persistence/JsonProjectStore.h"
#include "calculation/PeriodEvm.h"
#include <tuple>
namespace period_evm_fixture {
using namespace ppm;
inline CostEntry cost(const std::string &id, Money amount, std::optional<int> physical) {
    CostEntry e;
    e.taskId = id;
    e.actualMinor = amount;
    e.physicalBasisPoints = physical;
    e.reporter = "기간 EVM 검증";
    e.reason = "관측 누계 / 정정";
    e.recordedAt = "2026-11-08T01:00:00Z";
    return e;
}
inline Project make(JsonProjectStore &store, const std::string &path) {
    Document d(store);
    d.create("native 기간 EVM 검증", "2026-10-26", {"설계", "시공"});
    d.setCurrency("USD");
    d.setDuration(0, 960);
    d.setDuration(1, 960);
    const auto a = d.project()->tasks[0].id, b = d.project()->tasks[1].id;
    auto root = d.addWbs("전체 단계"), child = d.addWbs("설계 단계", root);
    d.assignWbs(0, child);
    d.assignWbs(1, root);
    d.setTaskCost({a, 1000, "uniform", "physical"});
    d.setTaskCost({b, 3000, "uniform", "physical"});
    d.captureBaseline("승인 예산", "PM", "기간별 EVM", "2026-10-25T00:00:00Z");
    auto report = [&](const std::string &at, std::int64_t acA, int evA, std::int64_t acB, int evB) {
        d.setStatusAt(at);
        d.recordCost(cost(a, acA, evA));
        d.recordCost(cost(b, acB, evB));
    };
    report("2026-10-25 23:59", 0, 0, 0, 0);
    report("2026-10-26 12:00", 100, 1000, 200, 2000);
    report("2026-10-26 18:00", 600, 4000, 1200, 5000);
    d.setStatusAt("2026-10-27 00:00");
    d.recordCost(cost(a, 650, 4500));
    report("2026-10-27 18:00", 800, 6000, 1700, 7000);
    report("2026-10-28 10:00", 700, 5000, 1700, 7000);
    d.recordCost(cost(a, 750, 5500));
    d.setStatusAt("2026-10-26 09:00");
    d.recordCost(cost(a, 1, 1)); // Out-of-order append must not supersede later status.
    d.setStatusAt("2026-10-29 12:00");
    PeriodActualSubmission s;
    for (const auto &[id, date, amount] :
         std::vector<std::tuple<std::string, std::string, std::int64_t>>{{a, "2026-10-26", 400},
                                                                         {a, "2026-10-27", 100},
                                                                         {a, "2026-10-28", 50},
                                                                         {b, "2026-10-26", 1000},
                                                                         {b, "2026-10-27", 500}}) {
        PeriodActualEntry e;
        e.taskId = id;
        e.date = date;
        e.actualMinor = amount;
        s.entries.push_back(std::move(e));
    }
    s.totals = {{a, 750}, {b, 1700}};
    s.reporter = "날짜 배분 담당";
    s.reason = "이미 보고한 누계 안에서 날짜 명세 연결";
    s.recordedAt = "2026-11-08T02:00:00Z";
    d.recordPeriodActuals(std::move(s));
    d.saveAs(path);
    return *d.project();
}
inline ReportOptions options(const Project &p) {
    ReportOptions o;
    o.baselineId = p.baselines.front().id;
    o.fromDate = "2026-10-26";
    o.toDate = "2026-10-30";
    o.reportId = "native-period-evm-qa";
    o.generatedAt = "2026-11-08T03:00:00Z";
    return o;
}
} // namespace period_evm_fixture
