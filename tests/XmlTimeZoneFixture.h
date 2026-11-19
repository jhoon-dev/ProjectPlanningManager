#pragma once
#include "domain/XmlFormula.h"
#include "domain/Classification.h"
#include <algorithm>
namespace ppm::test {
inline Project xmlTimeZoneFixture() {
    Project p;
    p.documentId = "xml-timezone-functions";
    p.name = "XML 시간대 변환 검토";
    p.startDate = "2026-11-13";
    p.currency = "USD";
    p.calculationProfile = effortProfile;
    p.wbsNodes = {{"G", "요약", {}}};
    p.tasks = {{"A", "서울 입력", 60, {}}, {"B", "명시 오프셋 입력", 90, {}}};
    p.customFields = {{"civil", "시각", CustomFieldType::Start, "", {}},
                      {"from", "출발", CustomFieldType::Text, "", {}},
                      {"to", "도착", CustomFieldType::Text, "", {}},
                      {"missing", "미보고 시각", CustomFieldType::Start, "", {}}};
    for (std::size_t i = 0; i < p.tasks.size(); ++i) {
        p.tasks[i].wbsId = "G";
        p.tasks[i].attributes = {{"field:civil", "2026-11-13T09:30:12.123456"},
                                 {"field:from", i ? "+05:45" : "2026f:Asia/Seoul"},
                                 {"field:to", i ? "2026f:Asia/Seoul" : "2026f:America/New_York"}};
    }
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "utc", "UTC 결과", CustomFieldType::Start, "PPMTimeZone([시각],[출발],\"UTC\")");
    add(FieldScope::Task, "local", "대상 시각", CustomFieldType::Finish, "PPMTimeZone([시각],[출발],[도착])");
    add(FieldScope::Task, "wire", "오프셋 문자열", CustomFieldType::Text, "PPMTimeZoneText([시각],[출발],[도착])");
    add(FieldScope::Task, "offset", "오프셋 초", CustomFieldType::Number, "PPMTimeZoneOffset([시각],[출발])");
    add(FieldScope::Task, "parsed", "문자열 UTC", CustomFieldType::Start, "PPMParseUtc([오프셋 문자열])");
    add(FieldScope::Task, "date", "UTC 날짜", CustomFieldType::Date, "DateValue([UTC 결과])");
    add(FieldScope::Task, "elapsed", "왕복 차이 초", CustomFieldType::Number,
        "DateDiff(\"s\",[UTC 결과],[문자열 UTC])");
    add(FieldScope::Task, "flag", "양의 오프셋", CustomFieldType::Flag, "PPMTimeZoneOffset([시각],[출발])>0");
    add(FieldScope::Task, "error", "없는 시각", CustomFieldType::Start,
        "PPMTimeZone(#2026-03-08T02:30:00#,\"2026f:America/New_York\",\"UTC\")");
    add(FieldScope::Task, "null", "미설정 결과", CustomFieldType::Start, "PPMTimeZone([미보고 시각],\"UTC\",\"UTC\")");
    p.customFields.push_back({"native", "오프셋 분", CustomFieldType::Number, "", {}, "[field:offset]/60"});
    add(FieldScope::Task, "summary", "요약 변환", CustomFieldType::Text,
        "IIf([Summary],PPMTimeZoneText(#2026-11-13T00:00:00#,\"UTC\",\"+09:00\"),\"leaf\")");
    p.customFields.back().xmlRollUp = XmlRollUp{2, 0};
    add(FieldScope::Project, "project", "프로젝트 변환", CustomFieldType::Finish,
        "PPMTimeZone(#2026-11-13T00:00:00#,\"UTC\",\"2026f:Australia/Lord_Howe\")");
    Resource r;
    r.id = "R";
    r.name = "장비";
    r.type = "equipment";
    p.resources = {r};
    add(FieldScope::Resource, "resource", "자원 여름 오프셋", CustomFieldType::Number,
        "PPMTimeZoneOffset(#2026-07-01T12:00:00#,\"2026f:America/New_York\")");
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    p.assignmentFields = {{"received", "수신 오프셋 문자열", CustomFieldType::Text, "", {}}};
    add(FieldScope::Assignment, "assignment", "배정 대상 시각", CustomFieldType::Start,
        "PPMTimeZone([Task.UTC 결과],\"UTC\",[Task.도착])");
    add(FieldScope::Assignment, "text", "배정 오프셋 문자열", CustomFieldType::Text,
        "PPMTimeZoneText([Task.UTC 결과],\"UTC\",[Task.도착])");
    auto f = std::find_if(p.customFields.begin(), p.customFields.end(), [](const auto &v) { return v.id == "wire"; });
    f->xmlRollDown = XmlRollDown{"received", true};
    p.tasks[0].attributes.push_back({"field:utc", "1999-01-01T00:00:00"});
    p.tasks[0].attributes.push_back({"field:error", "2000-01-01T00:00:00"});
    return p;
}
} // namespace ppm::test
