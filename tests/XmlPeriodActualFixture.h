#pragma once
#include "persistence/ProjectXml.h"
#include <stdexcept>
namespace ppm::test {
inline QByteArray xmlPeriodData(int assignment, int type, const QByteArray &date, const QByteArray &value, int unit = 2,
                                const QByteArray &finish = {}) {
    return "<TimephasedData><Type>" + QByteArray::number(type) + "</Type><UID>" + QByteArray::number(assignment) +
           "</UID><Start>" + date + "T09:00:00</Start><Finish>" + (finish.isEmpty() ? date + "T17:00:00" : finish) +
           "</Finish><Unit>" + QByteArray::number(unit) + "</Unit><Value>" + value + "</Value></TimephasedData>";
}
inline void injectAssignment(QByteArray &xml, int ordinal, const QByteArray &data) {
    qsizetype pos = 0;
    for (int i = 0; i < ordinal; ++i) {
        pos = xml.indexOf("<Assignment>", pos);
        if (pos < 0)
            throw std::runtime_error("assignment fixture");
        ++pos;
    }
    pos = xml.indexOf("</Assignment>", pos);
    xml.insert(pos, data);
}
inline QByteArray xmlPeriodFixture(bool invalidRows = true, const std::string &currency = "USD") {
    Project p;
    p.documentId = "xml-period-source";
    p.name = "배정 일별 실적";
    p.startDate = "2026-11-10";
    p.currency = currency;
    p.tasks = {{"A", "A", 960, {}}, {"B", "B", 480, {}}};
    Resource r;
    r.id = "R1";
    r.name = "R1";
    p.resources.push_back(r);
    r.id = "R2";
    r.name = "R2";
    p.resources.push_back(r);
    p.assignments = {{"A", "R1", 100}, {"A", "R2", 100}, {"B", "R1", 100}};
    auto xml = exportProjectXml(p).xml;
    xml.replace("<Name>A</Name>", "<Name>A</Name><ActualWork>PT999H0M0S</ActualWork><ActualCost>10000.00</ActualCost>");
    xml.replace("<Name>B</Name>", "<Name>B</Name><ActualCost>4000.00</ActualCost>");
    xml.replace("<StartDate>2026-11-10T09:00:00</StartDate>",
                "<StartDate>2026-11-10T09:00:00</StartDate><StatusDate>2026-11-12T18:00:00</StatusDate>");
    injectAssignment(xml, 1,
                     "<ActualWork>PT99H0M0S</ActualWork><ActualCost>9999.00</ActualCost>" +
                         xmlPeriodData(1, 2, "2026-11-10", "PT1H30M0.6S") +
                         xmlPeriodData(1, 6, "2026-11-10", "1250.00") + xmlPeriodData(1, 2, "2026-11-11", "PT0H0M0S") +
                         xmlPeriodData(1, 6, "2026-11-11", "0.00") + xmlPeriodData(1, 2, "2026-11-12", "PT0H30M0S") +
                         xmlPeriodData(1, 3, "2026-11-10", "PT9H0M0S"));
    injectAssignment(xml, 2,
                     xmlPeriodData(2, 2, "2026-11-10", "PT2H0M0S") + xmlPeriodData(2, 6, "2026-11-10", "2025.00"));
    injectAssignment(xml, 3, xmlPeriodData(3, 6, "2026-11-10", "1000.00"));
    if (invalidRows)
        injectAssignment(
            xml, 3,
            xmlPeriodData(3, 2, "2026-11-13", "PT1H0M0S", 2, "2026-11-14T17:00:00") +
                xmlPeriodData(3, 2, "2026-11-14", "PT1H0M0S", 1) + xmlPeriodData(3, 2, "2026-11-15", "PT1H0M0S") +
                xmlPeriodData(3, 2, "2026-11-15", "PT2H0M0S") + xmlPeriodData(3, 2, "2026-11-16", "PT0H0M1S") +
                xmlPeriodData(3, 6, "2026-11-17", "0.1") + xmlPeriodData(3, 2, "2026-11-20", "PT1H0M0S"));
    return xml;
}
inline Project xmlPeriodTarget(const std::string &currency = "USD") {
    Project p;
    p.documentId = "xml-period-native";
    p.name = "기존 원장 보존";
    p.startDate = "2026-11-10";
    p.currency = currency;
    p.revision = 5;
    p.statusAt = "2026-11-12 18:00";
    p.tasks = {{"TN-A", "native A", 960, {}}, {"TN-B", "native B", 480, {}}};
    Resource r;
    r.id = "NR-1";
    r.name = "장비";
    r.type = "equipment";
    r.rates = {{"2026-11-01", 12345}};
    p.resources.push_back(r);
    r.id = "NR-2";
    r.name = "인력";
    r.type = "labor";
    p.resources.push_back(r);
    p.assignments = {{"TN-A", "NR-1", 100}, {"TN-A", "NR-2", 100}, {"TN-B", "NR-1", 100}};
    CostEntry c;
    c.id = "existing-ac";
    c.taskId = "TN-A";
    c.statusAt = "2026-11-12 17:00";
    c.recordedAt = "2026-11-12T08:00:00Z";
    c.reporter = "현장";
    c.reason = "기존 보고";
    c.sourceDocumentId = p.documentId;
    c.recordedRevision = 4;
    c.currency = currency;
    c.actualMinor = 5000;
    c.physicalBasisPoints = 2500;
    p.costEntries.push_back(c);
    PeriodActualEntry e;
    e.id = "existing-period-a";
    e.taskId = "TN-A";
    e.resourceId = "NR-1";
    e.resourceType = "equipment";
    e.date = "2026-11-12";
    e.statusAt = c.statusAt;
    e.recordedAt = c.recordedAt;
    e.reporter = c.reporter;
    e.reason = c.reason;
    e.sourceDocumentId = p.documentId;
    e.recordedRevision = 4;
    e.currency = currency;
    e.costEntryId = c.id;
    e.workUnitMinutes = 2000;
    e.actualMinor = 500;
    p.periodActuals.push_back(e);
    e.id = "existing-period-b";
    e.taskId = "TN-B";
    e.date = "2026-11-10";
    e.costEntryId.clear();
    e.actualMinor.reset();
    e.workUnitMinutes = 1500;
    p.periodActuals.push_back(e);
    validate(p);
    return p;
}
inline XmlProgressOptions xmlPeriodOptions() {
    XmlProgressOptions o;
    o.statusAt = "2026-11-12 18:00";
    o.reporter = "PM";
    o.reason = "일별 배정 실적 대사";
    o.recordedAt = "2026-11-12T09:00:00Z";
    o.confirmed = true;
    return o;
}
inline std::vector<XmlPeriodMapping> xmlPeriodMappings(const ProjectXmlExchange &source) {
    std::vector<XmlPeriodMapping> out;
    for (const auto &r : source.periodCandidates)
        if (r.issue.empty() && r.finish <= "2026-11-12 18:00")
            out.push_back({r.key, r.taskUid == "1" ? "TN-A" : "TN-B", r.resourceUid == "1" ? "NR-1" : "NR-2"});
    return out;
}
} // namespace ppm::test
