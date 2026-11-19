#pragma once
#include "XmlActualFixture.h"
namespace ppm::test {
inline PeriodActualEntry quantityEntry(const Project &p, const std::string &id, const std::string &task,
                                       const std::string &resource, const std::string &day, const std::string &status,
                                       std::optional<std::int64_t> work, const std::string &previous = {}) {
    PeriodActualEntry e;
    e.id = id;
    e.taskId = task;
    e.resourceId = resource;
    e.resourceType = "labor";
    e.date = day;
    e.statusAt = status;
    e.recordedAt = "2026-11-18T09:00:00Z";
    e.reporter = "PM";
    e.reason = "보고 공수 / 정정";
    e.previousEntryId = previous;
    e.sourceDocumentId = p.documentId;
    e.currency = p.currency;
    e.recordedRevision = p.revision;
    e.workUnitMinutes = work;
    return e;
}
inline Project xmlQuantityFixture() {
    auto p = xmlActualFixture();
    p.documentId = "xml-quantities";
    // Retain the existing progress source identifiers as provenance.
    p.resources.push_back(p.resources[0]);
    p.resources.back().id = "S";
    p.resources.back().name = "지원";
    p.assignments.push_back({"A", "S", 100});
    p.periodActuals = {quantityEntry(p, "W0", "A", "R", "2026-11-11", "2026-11-11 18:00", 6001),
                       quantityEntry(p, "W1", "A", "R", "2026-11-11", "2026-11-12 18:00", 4501, "W0"),
                       quantityEntry(p, "W2", "A", "R", "2026-11-11", "2026-11-17 18:00", 9000, "W1"),
                       quantityEntry(p, "W3", "A", "R", "2026-11-11", "2026-11-11 18:00", 1100, "W2"),
                       quantityEntry(p, "S0", "A", "S", "2026-11-12", "2026-11-12 18:00", 3000),
                       quantityEntry(p, "B0", "B", "R", "2026-11-12", "2026-11-12 18:00", 0)};
    actualField(p, FieldScope::Task, "AD", CustomFieldType::Duration, "[Actual Duration]");
    actualField(p, FieldScope::Task, "DN", CustomFieldType::Number, "[ActualDuration]");
    actualField(p, FieldScope::Task, "AW", CustomFieldType::Number, "[Actual Work]");
    actualField(p, FieldScope::Task, "hours", CustomFieldType::Number, "[ActualWork]/60");
    actualField(p, FieldScope::Project, "AW", CustomFieldType::Number, "[Actual Work]");
    actualField(p, FieldScope::Resource, "AW", CustomFieldType::Number, "[ActualWork]");
    actualField(p, FieldScope::Assignment, "AW", CustomFieldType::Number, "[Actual Work]");
    actualField(p, FieldScope::Assignment, "TD", CustomFieldType::Number, "[Task.Actual Duration]");
    actualField(p, FieldScope::Assignment, "TW", CustomFieldType::Number, "[Task.ActualWork]");
    actualField(p, FieldScope::Assignment, "RW", CustomFieldType::Number, "[Resource.Actual Work]");
    actualField(p, FieldScope::Assignment, "PW", CustomFieldType::Number, "[Project.Actual Work]");
    for (auto scope : {FieldScope::Task, FieldScope::Resource})
        for (auto &f : customFields(p, scope))
            if (f.id == "AW" || f.id == "AD")
                f.xmlRollUp = XmlRollUp{2, 0};
    return p;
}
} // namespace ppm::test
