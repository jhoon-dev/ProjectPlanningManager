#pragma once
#include "domain/XmlFormula.h"
#include "domain/Classification.h"
#include <algorithm>
namespace ppm::test {
inline Project xmlAdditionalFunctionFixture() {
    Project p;
    p.documentId = "xml-additional-functions";
    p.name = "XML 추가 함수 검토";
    p.startDate = "2026-11-13";
    p.currency = "USD";
    p.calculationProfile = effortProfile;
    p.wbsNodes = {{"G", "요약", {}}};
    p.tasks = {{"A", "작업 A", 60, {}}, {"B", "작업 B", 90, {}}};
    p.customFields = {{"label", "입력문자", CustomFieldType::Text, "", {}},
                      {"q", "입력수", CustomFieldType::Number, "", {}},
                      {"amount", "입력금액", CustomFieldType::Cost, "", {}},
                      {"when", "입력일시", CustomFieldType::Date, "", {}},
                      {"missing", "미설정", CustomFieldType::Number, "", {}}};
    for (std::size_t i = 0; i < p.tasks.size(); ++i) {
        p.tasks[i].wbsId = "G";
        p.tasks[i].attributes = {{"field:label", i ? "Beta" : "  Alpha가  "},
                                 {"field:q", i ? "3.5" : "2.5"},
                                 {"field:amount", i ? "2" : "12.34"},
                                 {"field:when", "2026-11-13T09:30:12.123456"}};
    }
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "choose", "선택문자", CustomFieldType::Text,
        "Choose([입력수],\"one\",\"two\",\"three\",\"four\")");
    add(FieldScope::Task, "find", "검색위치", CustomFieldType::Number, "InStr([입력문자],\"가\")");
    add(FieldScope::Task, "compare", "문자비교", CustomFieldType::Number, "StrComp(\"A\",\"a\",1)");
    add(FieldScope::Task, "lefttrim", "왼쪽정리", CustomFieldType::Text, "LTrim([입력문자])");
    add(FieldScope::Task, "righttrim", "오른쪽정리", CustomFieldType::Text, "RTrim([입력문자])");
    add(FieldScope::Task, "space", "공백", CustomFieldType::Text, "\"x\" & Space(2) & \"y\"");
    add(FieldScope::Task, "repeat", "반복", CustomFieldType::Text, "String(3,\"가나다\")");
    add(FieldScope::Task, "asc", "문자코드", CustomFieldType::Number, "Asc(\"A\")");
    add(FieldScope::Task, "chr", "코드문자", CustomFieldType::Text, "Chr(65)&\"!\"");
    add(FieldScope::Task, "byte", "바이트변환", CustomFieldType::Number, "CByte([입력수])");
    add(FieldScope::Task, "currency", "통화변환", CustomFieldType::Cost, "CCur([입력금액]/3)");
    add(FieldScope::Task, "date", "날짜추출", CustomFieldType::Date, "DateValue([입력일시])");
    add(FieldScope::Task, "sqrt", "제곱근", CustomFieldType::Number, "Sqr(2)");
    add(FieldScope::Task, "duration", "계산기간", CustomFieldType::Duration, "Sqr(4)*30");
    add(FieldScope::Task, "flag", "검색여부", CustomFieldType::Flag, "InStr([입력문자],\"가\")>0");
    add(FieldScope::Task, "eager", "전체평가오류", CustomFieldType::Number, "Choose(1,10,1/0)");
    add(FieldScope::Task, "null", "미설정결과", CustomFieldType::Number, "Choose(9,1,2)");
    p.customFields.push_back({"native", "혼합계산", CustomFieldType::Number, "", {}, "[field:byte]*3"});
    auto &byteField =
        *std::find_if(p.customFields.begin(), p.customFields.end(), [](const auto &f) { return f.id == "byte"; });
    byteField.xmlRollUp = XmlRollUp{1, 3};
    byteField.xmlRollDown = XmlRollDown{"received", true};
    add(FieldScope::Task, "summary", "요약문자", CustomFieldType::Text, "Choose(IIf([Summary],1,2),\"요약\",\"말단\")");
    p.customFields.back().xmlRollUp = XmlRollUp{2, 0};
    p.projectFields = {{"input", "프로젝트수", CustomFieldType::Number, "", {}}};
    p.projectAttributes = {{"field:input", "16"}};
    add(FieldScope::Project, "project", "프로젝트근", CustomFieldType::Number, "Sqr([프로젝트수])");
    Resource r;
    r.id = "R";
    r.name = "장비";
    r.type = "equipment";
    p.resources = {r};
    add(FieldScope::Resource, "resource", "자원비교", CustomFieldType::Number, "StrComp(\"ABC\",\"abc\",1)");
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    p.assignmentFields = {{"received", "수신변환", CustomFieldType::Number, "", {}}};
    add(FieldScope::Assignment, "assignment", "배정근", CustomFieldType::Number,
        "Sqr([Project.프로젝트근])+[Task.바이트변환]");
    p.tasks[0].attributes.push_back({"field:byte", "99"});
    p.tasks[0].attributes.push_back({"field:eager", "777"});
    return p;
}
} // namespace ppm::test
