#pragma once
#include "domain/Classification.h"
#include "domain/XmlFormula.h"
namespace jobs::test {
inline void calendarField(Project &p, FieldScope scope, const std::string &id, CustomFieldType type,
                          const std::string &source) {
    CustomField f{id, id, type, "", {}};
    customFields(p, scope).push_back(f);
    auto &v = customFields(p, scope).back();
    v.xmlFormula = bindXmlFormula(p, scope, source);
    v.xmlFormula->enabled = true;
}
inline Project xmlCalendarFixture() {
    Project p;
    p.documentId = "xml-calendar";
    p.name = "근무 달력 계산식";
    p.startDate = "2026-11-11";
    p.calculationProfile = effortProfile;
    p.holidays = {"2026-11-12"};
    TaskCalendar am;
    am.id = "AM";
    am.name = "오전";
    am.periods = {{540, 720}};
    am.holidays = {"2026-11-13"};
    TaskCalendar night;
    night.id = "NIGHT";
    night.name = "야간";
    night.weekdays = {1, 2, 3, 4, 5, 6, 7};
    night.periods = {{0, 120}, {1320, 1440}};
    night.holidays = {"2026-11-14"};
    TaskCalendar full;
    full.id = "ALL";
    full.name = "24시간";
    full.weekdays = {1, 2, 3, 4, 5, 6, 7};
    full.periods = {{0, 1440}};
    p.calendars = {am, night, full};
    p.wbsNodes = {{"G", "그룹", {}}};
    p.tasks = {{"A", "표준 작업", 480, {}}, {"B", "오전 작업", 360, {"A"}}};
    for (auto &t : p.tasks)
        t.wbsId = "G";
    p.tasks[1].calendarId = "AM";
    Resource r;
    r.id = "R";
    r.name = "오전 담당";
    r.calendarId = "AM";
    p.resources = {r};
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    const std::string dates = "#2026-11-11T09:00:00#, #2026-11-13T18:00:00#";
    calendarField(p, FieldScope::Project, "p", CustomFieldType::Number, "ProjDateDiff(" + dates + ")");
    calendarField(p, FieldScope::Project, "pf", CustomFieldType::Finish, "ProjDateAdd([Finish],\"1h\")");
    calendarField(p, FieldScope::Task, "work", CustomFieldType::Number, "ProjDateDiff([Start],[Finish])");
    calendarField(p, FieldScope::Task, "wall", CustomFieldType::Number, "DateDiff(\"n\",[Start],[Finish])");
    calendarField(p, FieldScope::Task, "next", CustomFieldType::Finish, "ProjDateAdd([Finish],\"1d\")");
    calendarField(p, FieldScope::Task, "prev", CustomFieldType::Start, "ProjDateSub([Start],\"1d\")");
    calendarField(p, FieldScope::Task, "explicit", CustomFieldType::Number, "ProjDateDiff([Start],[Finish],\"오전\")");
    calendarField(p, FieldScope::Task, "literal", CustomFieldType::Number, "ProjDateDiff(" + dates + ")");
    calendarField(p, FieldScope::Task, "first", CustomFieldType::Date, "ProjDateAdd(#2026-11-11T11:30:00#,\"1h\")");
    calendarField(p, FieldScope::Task, "elapsed", CustomFieldType::Finish, "ProjDateAdd([Finish],\"1ed\")");
    calendarField(p, FieldScope::Task, "scope", CustomFieldType::Number, "[project:p]+[field:literal]");
    p.customFields.push_back({"native", "native", CustomFieldType::Number, "", {}, "[field:work]*2"});
    for (auto &f : p.customFields)
        if (f.id == "work" || f.id == "literal" || f.id == "explicit")
            f.xmlRollUp = XmlRollUp{2, 0};
    calendarField(p, FieldScope::Resource, "r", CustomFieldType::Number, "ProjDateDiff(" + dates + ")");
    calendarField(p, FieldScope::Resource, "nested", CustomFieldType::Number, "[project:p]+[field:r]");
    calendarField(p, FieldScope::Assignment, "a", CustomFieldType::Number, "ProjDateDiff(" + dates + ")");
    calendarField(p, FieldScope::Assignment, "mix", CustomFieldType::Number, "[resource:r]+[task:literal]+[project:p]");
    p.assignmentFields.push_back({"copy", "copy", CustomFieldType::Number, "", {}});
    p.customFields[0].xmlRollDown = XmlRollDown{"copy", true};
    p.tasks[0].attributes = {{"field:work", "999"}};
    return p;
}
} // namespace jobs::test
