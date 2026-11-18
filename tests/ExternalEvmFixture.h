#pragma once
#include "application/Document.h"
#include "domain/ExternalEvm.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/JsonProjectStore.h"
#include "calculation/PeriodEvm.h"
#include <QFile>
namespace external_evm_fixture {
using namespace jobs;
inline QByteArray bytes() {
    auto out = readPrimaveraXer(QString::fromUtf8(JOBS_EXTERNAL_EVM_FIXTURE));
    out.replace("9999.00\t8888.00", "12.00\t10.00");
    out.replace("\t2\t30.34\t0\t0\t3.00\t12.00\t10.00", "\t2\t30.34\t0\t0\t3.00\t8.00\t15.00");
    return out;
}
inline Project make(JsonProjectStore &store, const std::string &path) {
    auto p = importPrimaveraXer(bytes(), "외부 EVM 원본.xer").project;
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    const auto resources = d.project()->resources;
    for (auto r : resources) {
        r.rates = {{"2026-01-01", 0}};
        d.putResource(r);
    }
    d.setResourcePlan({});
    const auto tasks = d.project()->tasks;
    for (const auto &t : tasks)
        d.setTaskCost({t.id, t.id == "xer-task-20" ? 10000 : 0, "uniform", "physical"});
    d.captureBaseline("EVM 승인 기준선", "PM", "출처 검토", "2026-11-08T01:00:00Z");
    for (const auto &at : {"2026-10-18 23:59", "2026-10-28 18:00"}) {
        d.setStatusAt(at);
        for (const auto &t : tasks) {
            CostEntry e;
            e.taskId = t.id;
            e.actualMinor = std::string(at) == "2026-10-18 23:59" ? 0 : (t.id == "xer-task-20" ? 12280 : 0);
            e.physicalBasisPoints = std::string(at) == "2026-10-18 23:59" ? 0 : (t.id == "xer-task-20" ? 3725 : 0);
            e.reporter = "native 원장";
            e.reason = "개시/누계 관측";
            e.recordedAt = "2026-11-08T02:00:00Z";
            d.recordCost(e);
        }
    }
    d.save();
    return *d.project();
}
inline ExternalEvmSubmission submission(const Project &p) {
    auto out = externalEvmDraft(p, p.baselines.back().id);
    out.taskIds = {"xer-task-20"};
    out.reporter = "EV 담당";
    out.reason = "재무기간 EV/PV와 개시 누계 확인";
    out.recordedAt = "2026-11-08T03:00:00Z";
    out.confirmed = true;
    const auto *s = activeXerFinancials(p);
    for (std::size_t i = 0; i < s->periods.size(); ++i) {
        const auto &r = s->periods[i];
        if (r.resourceId.empty() && (r.bcwp || r.bcws))
            out.rows.push_back({i, r.start.substr(0, 10) == "2026-10-19" ? 0 : 1200,
                                r.start.substr(0, 10) == "2026-10-19" ? 0 : 1000});
    }
    return out;
}
inline ReportOptions options(const Project &p) {
    ReportOptions o;
    o.baselineId = p.baselines.back().id;
    o.fromDate = "2026-10-19";
    o.toDate = "2026-10-28";
    o.reportId = "external-evm-qa";
    o.generatedAt = "2026-11-08T04:00:00Z";
    return o;
}
} // namespace external_evm_fixture
