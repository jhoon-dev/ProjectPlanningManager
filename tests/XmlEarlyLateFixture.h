#pragma once
#include "XmlActualFixture.h"
namespace jobs::test {
inline Project xmlEarlyLateFixture(bool forecast = true) {
    Project p;
    p.documentId = "xml-early-late";
    p.name = "배치·후진 날짜 참조";
    p.startDate = "2026-11-12";
    p.calculationProfile = pdmProfile;
    p.calendars = {{"C", "8분", {1, 2, 3, 4, 5}, {{540, 548}}, {}},
                   {"L", "중간 4분", {1, 2, 3, 4, 5}, {{542, 546}}, {}}};
    p.tasks = {
        {"A", "선행", 6, {}}, {"B", "후행·진행", 4, {"A"}}, {"C", "독립·다른 달력", 2, {}}, {"M", "이정표", 0, {"A"}}};
    p.wbsNodes = {{"G", "시공", {}}, {"H", "이정표", {}}};
    for (auto &t : p.tasks) {
        t.wbsId = t.id == "M" ? "H" : "G";
        t.calendarId = t.id == "C" ? "L" : "C";
    }
    Resource r;
    r.id = "R";
    r.name = "담당";
    r.maxUnits = 200;
    p.resources = {r};
    for (const auto &t : p.tasks)
        p.assignments.push_back({t.id, "R", t.id == "C" ? 50 : 100});
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment}) {
        const auto prefix = scope == FieldScope::Assignment ? "Task." : scope == FieldScope::Resource ? "Project." : "";
        for (const auto &[id, name] : std::vector<std::pair<std::string, std::string>>{
                 {"ES", "Early Start"}, {"EF", "EarlyFinish"}, {"LS", "Late Start"}, {"LF", "LateFinish"}})
            actualField(p, scope, id, id.ends_with("S") ? CustomFieldType::Start : CustomFieldType::Finish,
                        "[" + std::string(prefix) + name + "]");
    }
    actualField(p, FieldScope::Task, "delay", CustomFieldType::Number, "ProjDateDiff([Early Start],[Late Start])");
    actualField(p, FieldScope::Assignment, "PF", CustomFieldType::Finish, "[Project.Late Finish]");
    for (std::size_t i = 0; i < 4; ++i)
        p.customFields[i].xmlRollUp = XmlRollUp{2, 0};
    p.tasks[0].attributes = {{"field:ES", "2000-01-01T00:00:00"}};
    if (forecast) {
        p.statusAt = "2026-11-12 09:07";
        p.progress = {actualEntry(p, "A0", "A", *p.statusAt, "2026-11-12 09:00", "2026-11-12 09:06", 6, 0),
                      actualEntry(p, "B0", "B", *p.statusAt, "2026-11-12 09:06", {}, 1, 4)};
        p.progress[0].planDurationMinutes = 6;
        p.progress[1].planDurationMinutes = 4;
    }
    return p;
}
} // namespace jobs::test
