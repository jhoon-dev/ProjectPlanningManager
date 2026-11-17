#pragma once
#include "XmlActualFixture.h"
#include "calculation/Tracking.h"
namespace jobs::test {
inline Project xmlBaselineFixture() {
    Project p;
    p.documentId = "xml-baseline";
    p.name = "최초 기준선 참조";
    p.startDate = "2026-11-12";
    p.calculationProfile = effortProfile;
    p.calendars = {{"C", "8분", {1, 2, 3, 4, 5}, {{540, 548}}, {}},
                   {"R", "부분 6분", {1, 2, 3, 4, 5}, {{541, 547}}, {}}};
    p.wbsNodes = {{"G", "시공", {}}, {"H", "이정표", {}}};
    p.tasks = {{"A", "중단·곡선", 8, {}}, {"B", "고정 공수", 8, {}}, {"M", "이정표", 0, {}}, {"D", "미배정", 3, {}}};
    for (auto &t : p.tasks) {
        t.calendarId = "C";
        t.wbsId = t.id == "M" ? "H" : "G";
    }
    p.tasks[0].breaks = {{"2026-11-12 09:04", "2026-11-12 09:05"}};
    p.tasks[1].taskType = TaskType::FixedWork;
    p.tasks[1].workDriven = true;
    p.tasks[1].workUnitMinutes = 501;
    Resource r;
    r.id = "R";
    r.name = "부분 인력";
    r.calendarId = "R";
    p.resources = {r};
    r.id = "S";
    r.name = "지원";
    r.calendarId = "C";
    p.resources.push_back(r);
    r.id = "Z";
    r.name = "미사용";
    p.resources.push_back(r);
    p.assignments = {{"A", "R", 100, {{2, 6, 50}}}, {"A", "S", 33}, {"B", "S", 100}};
    actualField(p, FieldScope::Task, "BS", CustomFieldType::Start, "[Baseline Start]");
    actualField(p, FieldScope::Task, "BF", CustomFieldType::Finish, "[BaselineFinish]");
    actualField(p, FieldScope::Task, "BD", CustomFieldType::Duration, "[Baseline Duration]");
    actualField(p, FieldScope::Task, "DN", CustomFieldType::Number, "[BaselineDuration]");
    actualField(p, FieldScope::Task, "BW", CustomFieldType::Number, "[Baseline Work]");
    actualField(p, FieldScope::Task, "hours", CustomFieldType::Number, "[BaselineWork]/60");
    actualField(p, FieldScope::Project, "BS", CustomFieldType::Start, "[BaselineStart]");
    actualField(p, FieldScope::Project, "BF", CustomFieldType::Finish, "[Baseline Finish]");
    actualField(p, FieldScope::Project, "BW", CustomFieldType::Number, "[Baseline Work]");
    actualField(p, FieldScope::Resource, "BW", CustomFieldType::Number, "[BaselineWork]");
    actualField(p, FieldScope::Assignment, "BW", CustomFieldType::Number, "[Baseline Work]");
    actualField(p, FieldScope::Assignment, "TS", CustomFieldType::Start, "[Task.Baseline Start]");
    actualField(p, FieldScope::Assignment, "TF", CustomFieldType::Finish, "[Task.BaselineFinish]");
    actualField(p, FieldScope::Assignment, "TD", CustomFieldType::Number, "[Task.Baseline Duration]");
    actualField(p, FieldScope::Assignment, "TW", CustomFieldType::Number, "[Task.Baseline Work]");
    actualField(p, FieldScope::Assignment, "RW", CustomFieldType::Number, "[Resource.Baseline Work]");
    actualField(p, FieldScope::Assignment, "PW", CustomFieldType::Number, "[Project.Baseline Work]");
    for (auto &f : p.customFields)
        if (f.id != "hours" && f.id != "DN")
            f.xmlRollUp = XmlRollUp{2, 0};
    p.resourceFields[0].xmlRollUp = XmlRollUp{2, 0};
    auto first = prepareBaseline(p, "원래 계획", "PM", "승인", "2026-11-12T09:00:00Z");
    first.id = "BASE-1";
    first.version = 1;
    p.baselines.push_back(first);
    ++p.revision;
    p.startDate = "2026-11-16";
    p.calendars[0].periods = {{540, 552}};
    p.tasks[0].durationMinutes = 4;
    p.tasks[0].breaks.clear();
    p.assignments[0].periods.clear();
    p.assignments[0].units = 25;
    auto second = prepareBaseline(p, "수정 계획", "PM", "재승인", "2026-11-12T09:01:00Z");
    second.id = "BASE-2";
    second.version = 2;
    p.baselines.push_back(second);
    ++p.revision;
    p.tasks.push_back({"X", "이후 추가", 1, {}});
    p.tasks.back().calendarId = "C";
    r.id = "NEW";
    r.name = "이후 자원";
    p.resources.push_back(r);
    p.assignments.push_back({"X", "NEW", 100});
    return p;
}
} // namespace jobs::test
