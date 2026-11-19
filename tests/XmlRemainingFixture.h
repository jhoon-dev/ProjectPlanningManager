#pragma once
#include "XmlActualFixture.h"
namespace ppm::test {
inline Project xmlRemainingFixture() {
    Project p;
    p.documentId = "xml-remaining";
    p.name = "잔여 예측 참조";
    p.startDate = "2026-11-12";
    p.calculationProfile = effortProfile;
    p.statusAt = "2026-11-12 09:02";
    p.calendars = {{"C", "8분", {1, 2, 3, 4, 5}, {{540, 548}}, {}},
                   {"R", "부분 6분", {1, 2, 3, 4, 5}, {{541, 547}}, {}}};
    p.tasks = {{"A", "완료", 2, {}},      {"B", "기간 곡선", 8, {}}, {"C", "미보고", 3, {}},
               {"W", "고정 공수", 8, {}}, {"U", "고정 단위", 8, {}}, {"M", "이정표", 0, {}}};
    p.wbsNodes = {{"G", "공사", {}}, {"H", "이정표", {}}};
    for (auto &t : p.tasks) {
        t.calendarId = "C";
        t.wbsId = t.id == "M" ? "H" : "G";
    }
    p.tasks[1].breaks = {{"2026-11-12 09:04", "2026-11-12 09:05"}};
    p.tasks[3].taskType = TaskType::FixedWork;
    p.tasks[3].workDriven = true;
    p.tasks[3].workUnitMinutes = 1000;
    p.tasks[4].taskType = TaskType::FixedUnits;
    p.tasks[4].workDriven = true;
    p.tasks[4].workUnitMinutes = 300;
    Resource r;
    r.id = "R";
    r.name = "부분 인력";
    r.calendarId = "R";
    r.maxUnits = 300;
    p.resources = {r};
    r.id = "S";
    r.name = "지원";
    r.calendarId = "C";
    p.resources.push_back(r);
    r.id = "Z";
    r.name = "미사용";
    p.resources.push_back(r);
    p.assignments = {{"A", "R", 100}, {"B", "R", 100, {{4, 6, 50}, {6, 8, 150}}},
                     {"B", "S", 33},  {"C", "R", 50},
                     {"W", "R", 100}, {"U", "S", 100}};
    p.progress = {actualEntry(p, "A0", "A", *p.statusAt, "2026-11-12 09:00", "2026-11-12 09:01", 1, 0),
                  actualEntry(p, "B0", "B", *p.statusAt, "2026-11-12 09:00", {}, 1, 4),
                  actualEntry(p, "W0", "W", *p.statusAt, "2026-11-12 09:00", {}, 1, 99)};
    p.progress.back().remainingWorkUnitMinutes = 501;
    actualField(p, FieldScope::Task, "RD", CustomFieldType::Duration, "[Remaining Duration]");
    actualField(p, FieldScope::Task, "DN", CustomFieldType::Number, "[RemainingDuration]");
    actualField(p, FieldScope::Task, "RW", CustomFieldType::Number, "[Remaining Work]");
    actualField(p, FieldScope::Task, "hours", CustomFieldType::Number, "[RemainingWork]/60");
    actualField(p, FieldScope::Task, "start", CustomFieldType::Start, "[Start]");
    actualField(p, FieldScope::Project, "RW", CustomFieldType::Number, "[Remaining Work]");
    actualField(p, FieldScope::Resource, "RW", CustomFieldType::Number, "[RemainingWork]");
    actualField(p, FieldScope::Assignment, "RW", CustomFieldType::Number, "[Remaining Work]");
    actualField(p, FieldScope::Assignment, "TD", CustomFieldType::Number, "[Task.Remaining Duration]");
    actualField(p, FieldScope::Assignment, "TW", CustomFieldType::Number, "[Task.RemainingWork]");
    actualField(p, FieldScope::Assignment, "RWResource", CustomFieldType::Number, "[Resource.Remaining Work]");
    actualField(p, FieldScope::Assignment, "PW", CustomFieldType::Number, "[Project.Remaining Work]");
    p.customFields[0].xmlRollUp = XmlRollUp{2, 0};
    p.customFields[2].xmlRollUp = XmlRollUp{2, 0};
    p.resourceFields[0].xmlRollUp = XmlRollUp{2, 0};
    return p;
}
} // namespace ppm::test
