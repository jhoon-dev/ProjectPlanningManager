#pragma once
#include "calculation/Tracking.h"
namespace usage_fixture {
inline ppm::Project project() {
    ppm::Project p;
    p.documentId = "usage-fixture";
    p.name = "시간축 Usage 검증";
    p.startDate = "2026-11-02";
    p.wbsNodes = {{"W", "기초", {}}, {"W2", "마무리", {}}};
    p.tasks = {{"A", "기초 A", 960, {}, "W"}, {"B", "후속 B", 480, {"A"}, "W2"}, {"C", "검사 C", 0, {}, "W"}};
    p.resources = {{"R", "인력 팀", "labor", 100, {}}, {"E", "장비 팀", "equipment", 100, {}}};
    p.resources[0].rates = {{"2026-11-02", 3}, {"2026-11-03", 5}};
    p.resources[1].rates = {{"2026-11-02", 7}};
    p.assignments = {{"A", "R", 50}, {"A", "E", 25}, {"B", "R", 100}};
    p.taskCosts = {{"A", 101, "uniform", "physical"}, {"B", 0, "start", "physical"}, {"C", 11, "finish", "physical"}};
    p.baselines.push_back(ppm::prepareBaseline(p, "승인 계획", "PM", "검증", "2026-11-02T00:00:00Z"));
    p.baselines.back().id = "approved-plan";
    p.baselines.back().version = 1;
    ++p.revision;
    return p;
}
inline void progress(ppm::Project &p) {
    p.statusAt = "2026-11-02 18:00";
    ppm::ProgressEntry e;
    e.id = "progress-A";
    e.taskId = "A";
    e.statusAt = *p.statusAt;
    e.recordedAt = "2026-11-03T00:00:00Z";
    e.reporter = "PM";
    e.reason = "실적";
    e.sourceDocumentId = p.documentId;
    e.recordedRevision = p.revision;
    e.planDurationMinutes = 960;
    e.actualStart = "2026-11-02 09:00";
    e.actualDurationMinutes = 480;
    e.remainingMinutes = 480;
    p.progress.push_back(e);
    for (const auto &id : {"A", "B", "C"}) {
        ppm::CostEntry c;
        c.id = "cost-" + std::string(id);
        c.taskId = id;
        c.statusAt = *p.statusAt;
        c.recordedAt = "2026-11-03T00:00:00Z";
        c.reporter = "PM";
        c.reason = "원가";
        c.sourceDocumentId = p.documentId;
        c.recordedRevision = p.revision;
        c.currency = p.currency;
        c.actualMinor = std::string(id) == "A" ? 80 : 0;
        c.physicalBasisPoints = 0;
        p.costEntries.push_back(c);
    }
}
} // namespace usage_fixture
