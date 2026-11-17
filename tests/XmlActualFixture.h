#pragma once
#include "domain/Classification.h"
#include "domain/XmlFormula.h"
namespace jobs::test {
inline void actualField(Project &p, FieldScope scope, const std::string &id, CustomFieldType type,
                        const std::string &source) {
    CustomField f{id, id, type, "", {}};
    customFields(p, scope).push_back(f);
    auto &v = customFields(p, scope).back();
    v.xmlFormula = bindXmlFormula(p, scope, source);
    v.xmlFormula->enabled = true;
}
inline ProgressEntry actualEntry(const Project &p, const std::string &id, const std::string &task,
                                 const std::string &status, std::optional<std::string> start,
                                 std::optional<std::string> finish, std::int64_t actual, std::int64_t remaining,
                                 const std::string &previous = {}) {
    ProgressEntry e;
    e.id = id;
    e.taskId = task;
    e.statusAt = status;
    e.recordedAt = "2026-11-18T09:00:00Z";
    e.reporter = "PM";
    e.reason = "실적 / 정정";
    e.previousEntryId = previous;
    e.sourceDocumentId = p.documentId;
    e.recordedRevision = p.revision;
    e.planDurationMinutes = task == "M" ? 0 : 480;
    e.actualStart = std::move(start);
    e.actualFinish = std::move(finish);
    e.actualDurationMinutes = actual;
    e.remainingMinutes = remaining;
    return e;
}
inline Project xmlActualFixture() {
    Project p;
    p.documentId = "xml-actual";
    p.name = "실적 일정 참조";
    p.startDate = "2026-11-11";
    p.calculationProfile = pdmProfile;
    p.statusAt = "2026-11-13 18:00";
    p.tasks = {{"A", "완료", 480, {}}, {"B", "진행", 240, {}}, {"C", "미보고", 120, {}}, {"M", "이정표", 0, {}}};
    p.wbsNodes = {{"G", "시공", {}}, {"H", "검수", {}}};
    for (auto &t : p.tasks)
        t.wbsId = t.id == "M" ? "H" : "G";
    Resource r;
    r.id = "R";
    r.name = "담당";
    p.resources = {r};
    for (const auto &t : p.tasks)
        p.assignments.push_back({t.id, "R", 100});
    p.progress = {actualEntry(p, "A0", "A", "2026-11-11 18:00", "2026-11-11 09:00", "2026-11-11 15:00", 300, 0),
                  actualEntry(p, "A1", "A", "2026-11-12 18:00", "2026-11-11 10:00", "2026-11-11 16:00", 300, 0, "A0"),
                  actualEntry(p, "A2", "A", "2026-11-17 18:00", "2026-11-13 09:00", "2026-11-13 17:00", 420, 0, "A1"),
                  actualEntry(p, "A3", "A", "2026-11-12 18:00", "2026-11-11 10:30", "2026-11-11 16:30", 300, 0, "A2"),
                  actualEntry(p, "B0", "B", "2026-11-13 18:00", "2026-11-12 10:00", {}, 60, 180),
                  actualEntry(p, "M0", "M", "2026-11-13 18:00", {}, {}, 0, 0)};
    actualField(p, FieldScope::Task, "AS", CustomFieldType::Start, "[Actual Start]");
    actualField(p, FieldScope::Task, "AF", CustomFieldType::Finish, "[Actual Finish]");
    actualField(p, FieldScope::Task, "work", CustomFieldType::Number, "ProjDateDiff([Actual Start],[Actual Finish])");
    actualField(p, FieldScope::Task, "wall", CustomFieldType::Number, "DateDiff(\"n\",[ActualStart],[ActualFinish])");
    actualField(p, FieldScope::Task, "state", CustomFieldType::Text,
                "IIf(IsNull([Actual Start]),\"미착수\",IIf(IsNull([Actual Finish]),\"진행\",\"완료\"))");
    actualField(p, FieldScope::Task, "globalAS", CustomFieldType::Start, "[Project.Actual Start]");
    actualField(p, FieldScope::Task, "globalAF", CustomFieldType::Finish, "[Project.ActualFinish]");
    p.customFields.push_back({"native", "native", CustomFieldType::Number, "", {}, "[field:work]*2"});
    for (auto &f : p.customFields)
        if (f.id == "AS" || f.id == "AF" || f.id == "work")
            f.xmlRollUp = XmlRollUp{2, 0};
    actualField(p, FieldScope::Project, "AS", CustomFieldType::Start, "[ActualStart]");
    actualField(p, FieldScope::Project, "AF", CustomFieldType::Finish, "[ActualFinish]");
    actualField(p, FieldScope::Resource, "AS", CustomFieldType::Start, "[Project.ActualStart]");
    actualField(p, FieldScope::Resource, "AF", CustomFieldType::Finish, "[Project.Actual Finish]");
    actualField(p, FieldScope::Assignment, "AS", CustomFieldType::Start, "[Task.Actual Start]");
    actualField(p, FieldScope::Assignment, "AF", CustomFieldType::Finish, "[Task.Actual Finish]");
    actualField(p, FieldScope::Assignment, "closed", CustomFieldType::Flag, "Not IsNull([Task.ActualFinish])");
    p.assignmentFields.push_back({"copy", "copy", CustomFieldType::Start, "", {}});
    p.customFields[0].xmlRollDown = XmlRollDown{"copy", true};
    p.tasks[0].attributes = {
        {"field:AS", "2000-01-01T00:00:00"}, {"field:AF", "2000-01-02T00:00:00"}, {"field:work", "999"}};
    p.taskSummaryAttributes = {{"field:AS", "1999-01-01T00:00:00"}};
    return p;
}
inline Project actualComplete(Project p) {
    p.progress.push_back(
        actualEntry(p, "B1", "B", "2026-11-13 18:00", "2026-11-12 10:00", "2026-11-13 11:00", 180, 0, "B0"));
    p.progress.push_back(actualEntry(p, "C0", "C", "2026-11-13 18:00", "2026-11-13 09:00", "2026-11-13 12:00", 180, 0));
    p.progress.push_back(
        actualEntry(p, "M1", "M", "2026-11-13 18:00", "2026-11-13 15:00", "2026-11-13 15:00", 0, 0, "M0"));
    return p;
}
} // namespace jobs::test
