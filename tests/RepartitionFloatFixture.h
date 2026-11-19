#pragma once
#include "domain/Project.h"
namespace repartition_fixture {
inline ppm::Project project() {
    ppm::Project p;
    p.documentId = "repartition-float";
    p.name = "구간 소멸과 재분할";
    p.startDate = "2026-11-02";
    p.calculationProfile = ppm::effortProfile;
    p.tasks = {{"A", "분할 A", 480, {}}, {"B", "공유 B", 240, {}}, {"C", "독립 C", 1440, {}}};
    p.tasks[0].breaks = {{"2026-11-02 12:00", "2026-11-03 09:00"}};
    ppm::Resource r;
    r.id = "R";
    r.name = "공유 인력";
    r.rates = {{"2026-11-02", 60}};
    p.resources = {r};
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    for (const auto &t : p.tasks)
        p.taskCosts.push_back({t.id, 0});
    return p;
}
} // namespace repartition_fixture
