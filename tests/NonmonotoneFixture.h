#pragma once
#include "domain/Project.h"
namespace nonmonotone_fixture {
using namespace jobs;
inline Project base(int minutes) {
    Project p;
    p.documentId = "nonmonotone-propagation";
    p.name = "비단조 수요 전파";
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    TaskCalendar cal;
    cal.id = "task";
    cal.name = "작업 달력";
    cal.periods = {{540, 540 + minutes}};
    p.calendars = {cal};
    return p;
}
inline void finish(Project &p) {
    for (auto &t : p.tasks) {
        t.calendarId = "task";
        p.taskCosts.push_back({t.id, 0});
    }
}
inline Resource resource(const std::string &id, const std::string &calendar, int units = 100) {
    Resource r;
    r.id = id;
    r.name = id;
    r.calendarId = calendar;
    r.maxUnits = units;
    r.rates = {{"2026-11-02", 60}};
    return r;
}
inline Project holes(int count, int minutes = 120) {
    auto p = base(minutes);
    auto cal = p.calendars[0];
    cal.id = "resource";
    cal.periods = {{540, 542}, {544, 540 + minutes}};
    p.calendars.push_back(cal);
    p.resources = {resource("R", "resource")};
    p.tasks = {{"A", "고정 A", minutes, {}}, {"B", "고정 B", minutes, {}}};
    for (auto &t : p.tasks) {
        t.constraintType = ConstraintType::MSO;
        t.constraintAt = "2026-11-02 09:00";
        p.assignments.push_back({t.id, "R", 40});
    }
    for (int i = 0; i < count; ++i) {
        const auto id = "C" + std::to_string(i);
        p.tasks.push_back({id, id, 1, {}});
        p.assignments.push_back({id, "R", 40});
    }
    p.tasks.push_back({"H", "완료일", minutes, {}});
    finish(p);
    return p;
}
inline Project fractional(int horizon = 8) {
    auto p = base(8);
    auto cal = p.calendars[0];
    cal.id = "partial";
    cal.periods = {{540, 542}, {544, 546}};
    p.calendars.push_back(cal);
    p.resources = {resource("R0", "partial"), resource("R1", "task", 200)};
    p.tasks = {{"A", "공수 A", 2, {}}, {"B", "공수 B", 2, {}}, {"C", "공수 C", 2, {}}, {"H", "완료일", horizon, {}}};
    for (int i = 0; i < 3; ++i) {
        p.tasks[i].taskType = TaskType::FixedWork;
        p.tasks[i].workDriven = true;
        p.tasks[i].workUnitMinutes = 151;
        p.assignments.push_back({p.tasks[i].id, "R0", 50});
        p.assignments.push_back({p.tasks[i].id, "R1", 100});
    }
    finish(p);
    return p;
}
inline Project varying() {
    auto p = base(4);
    p.resources = {resource("R", "task")};
    p.resources[0].exceptions = {{"2026-11-03", 50}, {"2026-11-04", 0}, {"2026-11-13", 50}};
    p.tasks = {{"A", "A", 2, {}}, {"B", "B", 2, {}}, {"C", "C", 2, {}}, {"H", "완료일", 40, {}}};
    p.assignments = {{"A", "R", 50}, {"B", "R", 50}, {"C", "R", 50}};
    finish(p);
    return p;
}
} // namespace nonmonotone_fixture
