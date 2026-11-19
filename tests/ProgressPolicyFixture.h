#pragma once
#include "domain/Project.h"
namespace progress_fixture {
inline ppm::ProgressEntry entry(const ppm::Project &p, const std::string &task, std::int64_t planned,
                                std::int64_t actual, std::int64_t remaining, const std::string &start,
                                std::optional<std::string> finish = {}) {
    ppm::ProgressEntry e;
    e.id = "fact-" + task;
    e.taskId = task;
    e.statusAt = *p.statusAt;
    e.recordedAt = "2026-11-03T00:00:00Z";
    e.reporter = "tester";
    e.reason = "fixture";
    e.sourceDocumentId = p.documentId;
    e.recordedRevision = p.revision;
    e.planDurationMinutes = planned;
    e.actualDurationMinutes = actual;
    e.remainingMinutes = remaining;
    e.actualStart = start;
    e.actualFinish = finish;
    return e;
}
inline ppm::Project small(const std::string &profile = "native-fs0-workminutes-v1") {
    ppm::Project p;
    p.documentId = "progress-policy-fixture";
    p.name = "진척 정책 A/B";
    p.startDate = "2026-11-02";
    p.calculationProfile = profile;
    p.statusAt = "2026-11-03 09:00";
    p.tasks = {{"A", "선행 A", 960, {}},
               {"B", "진행 B <원문>", 480, {"A"}},
               {"C", "후행 C", 480, {"B"}},
               {"D", "독립 D", 480, {}},
               {"E", "완료 E", 480, {}}};
    p.progress = {entry(p, "A", 960, 480, 480, "2026-11-02 09:00"), entry(p, "B", 480, 240, 240, "2026-11-02 14:00"),
                  entry(p, "E", 480, 480, 0, "2026-11-02 09:00", "2026-11-02 18:00")};
    return p;
}
inline ppm::Project large(int count) {
    auto p = small();
    p.documentId = "progress-policy-large-" + std::to_string(count);
    p.statusAt = "2026-11-02 10:00";
    p.tasks.clear();
    p.progress.clear();
    for (int i = 0; i < count; ++i)
        p.tasks.push_back({"T" + std::to_string(i), "작업 " + std::to_string(i), 1,
                           i ? std::vector<std::string>{"T" + std::to_string(i - 1)} : std::vector<std::string>{}});
    p.progress.push_back(entry(p, "T" + std::to_string(count / 2), 1, 0, 1, "2026-11-02 09:00"));
    return p;
}
} // namespace progress_fixture
