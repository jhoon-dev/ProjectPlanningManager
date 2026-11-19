#pragma once
#include "XmlActualFixture.h"
#include "persistence/ProjectXml.h"
namespace ppm::test {
inline QByteArray xmlProgressFixture(bool fields = true) {
    Project p;
    p.documentId = "xml-progress-source";
    p.name = "작업 진척 채택";
    p.startDate = "2026-11-12";
    p.tasks = {{"A", "완료-A", 120, {}},   {"B", "진행-B", 240, {}},         {"C", "미착수-C", 60, {}},
               {"M", "완료점-M", 0, {}},   {"BAD", "기간누락-BAD", 120, {}}, {"F", "미래-F", 120, {}},
               {"S", "초정밀-S", 120, {}}, {"D", "소수기간-D", 120, {}},     {"N", "미보고-N", 60, {}}};
    p.wbsNodes = {{"G", "공사", {}}};
    for (auto &t : p.tasks)
        t.wbsId = "G";
    Resource r;
    r.id = "R";
    r.name = "인력";
    p.resources = {r};
    for (const auto &t : p.tasks)
        p.assignments.push_back({t.id, "R", 100});
    if (fields) {
        actualField(p, FieldScope::Task, "AS", CustomFieldType::Start, "[Actual Start]");
        actualField(p, FieldScope::Task, "AF", CustomFieldType::Finish, "[Actual Finish]");
        actualField(p, FieldScope::Task, "AD", CustomFieldType::Number, "[Actual Duration]");
        actualField(p, FieldScope::Task, "AW", CustomFieldType::Number, "[Actual Work]");
    }
    auto xml = exportProjectXml(p).xml;
    xml.replace("<StartDate>2026-11-12T09:00:00</StartDate>",
                "<StartDate>2026-11-12T09:00:00</StartDate><StatusDate>2026-11-12T12:00:00</StatusDate>");
    const auto add = [&](const QByteArray &name, const QByteArray &data) {
        const auto tag = "<Name>" + name + "</Name>";
        xml.replace(tag, tag + data);
    };
    add(QString("완료-A").toUtf8(),
        "<ActualStart>2026-11-12T09:00:00</ActualStart><ActualFinish>2026-11-12T11:00:00</"
        "ActualFinish><ActualDuration>PT2H0M0S</ActualDuration><RemainingDuration>PT0H0M0S</"
        "RemainingDuration><ActualWork>PT9H0M0S</ActualWork><ActualCost>999</ActualCost>");
    add(QString("진행-B").toUtf8(),
        "<ActualStart>2026-11-12T11:00:00</ActualStart><ActualDuration>PT1H0M0S</"
        "ActualDuration><RemainingDuration>PT3H0M0S</RemainingDuration><PercentComplete>25</PercentComplete>");
    add(QString("미착수-C").toUtf8(),
        "<ActualStart>NA</ActualStart><ActualFinish>NA</ActualFinish><ActualDuration>PT0H0M0S</"
        "ActualDuration><RemainingDuration>PT1H0M0S</RemainingDuration>");
    add(QString("완료점-M").toUtf8(),
        "<ActualStart>2026-11-12T11:30:00</ActualStart><ActualFinish>2026-11-12T11:30:00</"
        "ActualFinish><ActualDuration>PT0H0M0S</ActualDuration><RemainingDuration>PT0H0M0S</RemainingDuration>");
    add(QString("기간누락-BAD").toUtf8(),
        "<ActualStart>2026-11-12T11:00:00</ActualStart><RemainingDuration>PT1H0M0S</RemainingDuration>");
    add(QString("미래-F").toUtf8(), "<ActualStart>2026-11-12T13:00:00</ActualStart><ActualDuration>PT0H0M0S</"
                                    "ActualDuration><RemainingDuration>PT2H0M0S</RemainingDuration>");
    add(QString("초정밀-S").toUtf8(), "<ActualStart>2026-11-12T09:00:15</ActualStart><ActualDuration>PT1H0M0S</"
                                      "ActualDuration><RemainingDuration>PT1H0M0S</RemainingDuration>");
    add(QString("소수기간-D").toUtf8(),
        "<ActualDuration>PT1S</ActualDuration><RemainingDuration>PT1H0M0S</RemainingDuration>");
    return xml;
}
inline XmlProgressOptions xmlProgressOptions(const ProjectXmlExchange &e) {
    XmlProgressOptions o;
    o.statusAt = "2026-11-12 12:00";
    o.reporter = "PM";
    o.reason = "선택한 원본 진척 확인";
    o.recordedAt = "2026-11-12T03:00:00Z";
    o.confirmed = true;
    for (std::size_t i = 0; i < 4; ++i)
        o.taskIds.push_back(e.progressCandidates.at(i).taskId);
    return o;
}
} // namespace ppm::test
