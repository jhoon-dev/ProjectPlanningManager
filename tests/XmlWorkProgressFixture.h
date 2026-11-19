#pragma once
#include "persistence/ProjectXml.h"
namespace ppm::test {
inline QByteArray xmlWorkProgressFixture() {
    Project p;
    p.documentId = "xml-work-progress-source";
    p.name = "공수 작업 기간 수신";
    p.startDate = "2026-11-13";
    p.tasks = {{"A", "진행 공수", 153, {}},
               {"B", "완료 공수", 40, {}},
               {"C", "기간 기준", 30, {}},
               {"Z", "미착수 공수0", 0, {}},
               {"U", "미선택", 30, {}}};
    auto xml = exportProjectXml(p).xml;
    xml.replace("<StartDate>2026-11-13T09:00:00</StartDate>",
                "<StartDate>2026-11-13T09:00:00</StartDate><StatusDate>2026-11-16T11:00:00</StatusDate>");
    const auto add = [&](const char *name, const QByteArray &data) {
        const auto tag = "<Name>" + QString::fromUtf8(name).toUtf8() + "</Name>";
        xml.replace(tag, tag + data);
    };
    add("진행 공수", "<ActualStart>2026-11-13T09:00:00</ActualStart><ActualDuration>PT0H30M0S</"
                     "ActualDuration><RemainingDuration>PT2H3M0S</RemainingDuration><RemainingWork>PT0H35M0.6S</"
                     "RemainingWork><ActualWork>PT99H0M0S</ActualWork><ActualCost>99999</ActualCost>");
    add("완료 공수", "<ActualStart>2026-11-13T09:00:00</ActualStart><ActualFinish>2026-11-13T09:40:00</"
                     "ActualFinish><ActualDuration>PT0H40M0S</ActualDuration><RemainingDuration>PT0H0M0S</"
                     "RemainingDuration><RemainingWork>PT0H0M0S</RemainingWork>");
    add("기간 기준",
        "<ActualStart>2026-11-13T09:20:00</ActualStart><ActualDuration>PT0H10M0S</"
        "ActualDuration><RemainingDuration>PT0H20M0S</RemainingDuration><RemainingWork>PT0H0M1S</RemainingWork>");
    add("미착수 공수0", "<ActualDuration>PT0H0M0S</ActualDuration><RemainingDuration>PT0H0M0S</"
                        "RemainingDuration><RemainingWork>PT0H0M0S</RemainingWork>");
    return xml;
}
inline Project xmlWorkProgressTarget() {
    Project p;
    p.documentId = "native-work-progress";
    p.name = "공수 기반 기간 병합";
    p.startDate = "2026-11-13";
    p.calculationProfile = effortProfile;
    p.revision = 5;
    p.statusAt = "2026-11-16 11:00";
    p.calendars = {{"T", "작업 한 시간", {1, 2, 3, 4, 5}, {{540, 600}}, {}},
                   {"R1", "앞 40분", {1, 2, 3, 4, 5}, {{540, 580}}, {}},
                   {"R2", "뒤 40분", {1, 2, 3, 4, 5}, {{560, 600}}, {}}};
    p.tasks = {{"NA", "공수 고정", 999, {}},
               {"NB", "배정률 고정 공수", 333, {}},
               {"NC", "배정률 고정 기간", 45, {}},
               {"NZ", "공수0", 0, {}},
               {"NU", "미선택 공수", 500, {}}};
    for (auto &t : p.tasks)
        t.calendarId = "T";
    for (int i : {0, 1, 3, 4}) {
        p.tasks[i].workDriven = true;
        p.tasks[i].taskType = i == 1 ? TaskType::FixedUnits : TaskType::FixedWork;
    }
    p.tasks[0].workUnitMinutes = 9000;
    p.tasks[1].workUnitMinutes = 5001;
    p.tasks[3].workUnitMinutes = 0;
    p.tasks[4].workUnitMinutes = 500;
    p.tasks[2].taskType = TaskType::FixedUnits;
    Resource r;
    r.id = "NR1";
    r.name = "선행 인력";
    r.calendarId = "R1";
    p.resources.push_back(r);
    r.id = "NR2";
    r.name = "후행 인력";
    r.calendarId = "R2";
    p.resources.push_back(r);
    p.assignments = {{"NA", "NR1", 100, {{0, 20, 50}}},
                     {"NA", "NR2", 50},
                     {"NB", "NR1", 75},
                     {"NC", "NR2", 100},
                     {"NZ", "NR1", 100},
                     {"NU", "NR2", 100}};
    ProgressEntry e;
    e.id = "old-work-report";
    e.taskId = "NA";
    e.statusAt = "2026-11-16 10:00";
    e.recordedAt = "2026-11-13T03:00:00Z";
    e.reporter = "현장";
    e.reason = "기존 공수 보고";
    e.sourceDocumentId = p.documentId;
    e.recordedRevision = 4;
    e.planDurationMinutes = 94;
    e.actualStart = "2026-11-13 09:30";
    e.actualDurationMinutes = 10;
    e.remainingMinutes = 500;
    e.remainingWorkUnitMinutes = 7001;
    p.progress.push_back(e);
    e.id = "future-work-report";
    e.previousEntryId = p.progress.back().id;
    e.statusAt = "2026-11-18 11:00";
    e.recordedRevision = 5;
    e.actualDurationMinutes = 11;
    e.remainingMinutes = 501;
    e.remainingWorkUnitMinutes = 8001;
    p.progress.push_back(e);
    PeriodActualEntry actual;
    actual.id = "preserved-actual-work";
    actual.taskId = "NA";
    actual.resourceId = "NR1";
    actual.resourceType = "labor";
    actual.date = "2026-11-13";
    actual.statusAt = "2026-11-16 10:00";
    actual.recordedAt = "2026-11-13T03:00:00Z";
    actual.reporter = "현장";
    actual.reason = "보존할 실제 공수";
    actual.sourceDocumentId = p.documentId;
    actual.recordedRevision = 4;
    actual.currency = p.currency;
    actual.workUnitMinutes = 123;
    p.periodActuals.push_back(actual);
    validate(p);
    return p;
}
inline XmlProgressOptions xmlWorkProgressOptions() {
    return {"2026-11-16 11:00", "PM", "공수 기반 기간/잔여 검토", "2026-11-13T03:00:00Z", {}, true};
}
inline std::vector<XmlProgressMapping> xmlWorkProgressMappings() {
    return {{"1", "NA"}, {"2", "NB"}, {"3", "NC"}, {"4", "NZ"}};
}
} // namespace ppm::test
