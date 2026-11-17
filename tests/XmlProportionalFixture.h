#pragma once
#include "XmlPeriodActualFixture.h"
#include "persistence/XmlPeriodAllocation.h"
namespace jobs::test {
inline QByteArray xmlProportionalFixture() {
    Project p;
    p.documentId = "xml-proportional-source";
    p.name = "다일 실적 총량";
    p.startDate = "2026-11-13";
    p.currency = "USD";
    p.tasks = {{"A", "범위 A", 960, {}}, {"B", "범위 B", 480, {}}};
    Resource r;
    r.id = "R1";
    r.name = "R1";
    p.resources.push_back(r);
    r.id = "R2";
    r.name = "R2";
    p.resources.push_back(r);
    p.assignments = {{"A", "R1", 100}, {"B", "R2", 100}};
    auto xml = exportProjectXml(p).xml;
    xml.replace("<Name>범위 A</Name>", "<Name>범위 A</Name><ActualCost>15000</ActualCost>");
    xml.replace("<Name>범위 B</Name>", "<Name>범위 B</Name><ActualCost>10000</ActualCost>");
    xml.replace("<StartDate>2026-11-13T09:00:00</StartDate>",
                "<StartDate>2026-11-13T09:00:00</StartDate><StatusDate>2026-11-17T18:00:00</StatusDate>");
    const auto data = [](int uid, int type, int unit, const QByteArray &start, const QByteArray &end,
                         const QByteArray &value) {
        return "<TimephasedData><Type>" + QByteArray::number(type) + "</Type><UID>" + QByteArray::number(uid) +
               "</UID><Start>" + start + "</Start><Finish>" + end + "</Finish><Unit>" + QByteArray::number(unit) +
               "</Unit><Value>" + value + "</Value></TimephasedData>";
    };
    injectAssignment(xml, 1,
                     data(1, 2, 2, "2026-11-13T11:00:00", "2026-11-16T14:00:00", "PT0H1M0.6S") +
                         data(1, 6, 3, "2026-11-13T11:00:00", "2026-11-16T14:00:00", "5001"));
    injectAssignment(xml, 2,
                     data(2, 2, 1, "2026-11-15T00:00:00", "2026-11-17T00:00:00", "PT0H0M1.8S") +
                         data(2, 6, 5, "2026-11-15T00:00:00", "2026-11-17T00:00:00", "1"));
    return xml;
}
inline Project xmlProportionalTarget() {
    Project p;
    p.documentId = "native-proportional";
    p.name = "다일 실적 비례 검토";
    p.startDate = "2026-11-13";
    p.statusAt = "2026-11-17 18:00";
    p.calculationProfile = effortProfile;
    p.currency = "USD";
    p.revision = 5;
    p.tasks = {{"NA", "native A", 960, {}}, {"NB", "native B", 480, {}}};
    p.calendars = {{"T", "작업", {1, 2, 3, 4, 5}, {{540, 720}, {780, 1020}}, {}},
                   {"R1", "장비", {1, 2, 3, 4, 5}, {{600, 720}, {780, 960}}, {}},
                   {"R2", "인력", {1, 2, 3, 4, 5}, {{540, 720}, {780, 1020}}, {}}};
    for (auto &t : p.tasks)
        t.calendarId = "T";
    p.tasks[0].breaks = {{"2026-11-13 13:00", "2026-11-13 14:00"}};
    Resource r;
    r.id = "NR1";
    r.name = "장비";
    r.type = "equipment";
    r.calendarId = "R1";
    r.rates = {{"2026-11-13", 99999}};
    r.exceptions = {{"2026-11-13", 0}};
    p.resources.push_back(r);
    r.id = "NR2";
    r.name = "인력";
    r.type = "labor";
    r.calendarId = "R2";
    r.exceptions.clear();
    p.resources.push_back(r);
    p.assignments = {{"NA", "NR1", 250}, {"NB", "NR2", 75}};
    CostEntry ac;
    ac.id = "original-ac";
    ac.taskId = "NA";
    ac.statusAt = "2026-11-17 17:00";
    ac.recordedAt = "2026-11-13T03:00:00Z";
    ac.reporter = "현장";
    ac.reason = "보존할 누계";
    ac.sourceDocumentId = p.documentId;
    ac.recordedRevision = 4;
    ac.currency = p.currency;
    ac.actualMinor = 20000;
    p.costEntries.push_back(ac);
    PeriodActualEntry e;
    e.id = "original-a13";
    e.taskId = "NA";
    e.resourceId = "NR1";
    e.resourceType = "equipment";
    e.date = "2026-11-13";
    e.statusAt = ac.statusAt;
    e.recordedAt = ac.recordedAt;
    e.reporter = ac.reporter;
    e.reason = ac.reason;
    e.sourceDocumentId = p.documentId;
    e.recordedRevision = 4;
    e.currency = p.currency;
    e.costEntryId = ac.id;
    e.workUnitMinutes = 111;
    e.actualMinor = 500;
    p.periodActuals.push_back(e);
    e.id = "original-a14";
    e.date = "2026-11-14";
    e.workUnitMinutes = 222;
    e.actualMinor = 700;
    p.periodActuals.push_back(e);
    e.id = "original-b16";
    e.taskId = "NB";
    e.resourceId = "NR2";
    e.resourceType = "labor";
    e.date = "2026-11-16";
    e.workUnitMinutes = 333;
    e.actualMinor.reset();
    e.costEntryId.clear();
    p.periodActuals.push_back(e);
    ac.id = "future-ac";
    ac.previousEntryId = "original-ac";
    ac.statusAt = "2026-11-18 18:00";
    ac.recordedRevision = 5;
    ac.actualMinor = 50000;
    p.costEntries.push_back(ac);
    e = p.periodActuals.front();
    e.id = "future-a13";
    e.previousEntryId = p.periodActuals.front().id;
    e.statusAt = ac.statusAt;
    e.recordedRevision = 5;
    e.costEntryId = ac.id;
    e.workUnitMinutes = 999;
    e.actualMinor = 6000;
    p.periodActuals.push_back(e);
    validate(p);
    return p;
}
inline XmlProgressOptions xmlProportionalOptions() {
    return {"2026-11-17 18:00", "PM", "구간 총량과 비례 추정 확인", "2026-11-13T03:00:00Z", {}, true};
}
inline std::vector<XmlPeriodMapping> xmlProportionalMappings() {
    return {{"1/record-1", "NA", "NR1"},
            {"1/record-2", "NA", "NR1"},
            {"2/record-1", "NB", "NR2"},
            {"2/record-2", "NB", "NR2"}};
}
} // namespace jobs::test
