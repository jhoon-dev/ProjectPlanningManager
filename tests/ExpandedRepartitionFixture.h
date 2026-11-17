#pragma once
#include "domain/Project.h"
#include "calculation/WorkCalendar.h"
namespace expanded_fixture {
inline jobs::Project chain(int count, int slack = 2, bool separate = false) {
    jobs::Project p;
    p.documentId = "expanded-repartition";
    p.name = "128개 초과 재분할";
    p.startDate = "2026-11-02";
    p.calculationProfile = jobs::constraintProfile;
    jobs::Resource r;
    r.id = "R";
    r.name = "공유 인력";
    r.rates = {{p.startDate, 60}};
    r.exceptions = {{"2026-11-30", 100}};
    p.resources = {r};
    jobs::WorkCalendar clock(p, jobs::Task{});
    const int step = separate ? 3 : 1;
    for (int i = 0; i < count; ++i) {
        const auto id = "T" + std::to_string(i);
        jobs::Task t{id, id, 1, i ? std::vector<std::string>{"T" + std::to_string(i - 1)} : std::vector<std::string>{}};
        if (separate) {
            t.constraintType = jobs::ConstraintType::SNET;
            t.constraintAt = clock.boundary(i * step, false);
        }
        p.tasks.push_back(t);
        p.assignments.push_back({id, "R", 100});
        p.taskCosts.push_back({id, 0});
    }
    p.tasks.push_back({"H", "독립 완료일", (count - 1) * step + 1 + slack, {}});
    p.taskCosts.push_back({"H", 0});
    if (separate)
        for (int i = 0; i < count; ++i) {
            const auto id = "M" + std::to_string(i);
            jobs::Task marker{id, "고정 완료 경계", 0, {"T" + std::to_string(i)}};
            marker.constraintType = jobs::ConstraintType::MSO;
            marker.constraintAt = clock.boundary(i * step + slack + 1, true);
            p.tasks.push_back(marker);
            p.taskCosts.push_back({id, 0});
        }
    return p;
}
} // namespace expanded_fixture
