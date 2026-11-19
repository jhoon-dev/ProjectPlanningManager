#pragma once
#include <algorithm>
#include <tuple>
#include "domain/Classification.h"
#include "domain/XmlFormula.h"
namespace ppm::test {
inline Project xmlFormulaFixture() {
    Project p;
    p.documentId = "xml-formula";
    p.name = "XML 외부 계산식";
    p.startDate = "2026-11-11";
    p.currency = "USD";
    p.calculationProfile = pdmProfile;
    p.wbsNodes = {{"G", "요약", {}}};
    p.tasks = {{"A", "설계", 60, {}}, {"B", "검토", 120, {}}};
    for (auto &t : p.tasks)
        t.wbsId = "G";
    p.projectFields = {{"factor", "계수", CustomFieldType::Number, "", {}},
                       {"projectResult", "프로젝트 계산", CustomFieldType::Number, "", {}}};
    p.projectAttributes = {{"field:factor", "2"}, {"field:projectResult", "999"}};
    p.customFields = {{"q", "수량", CustomFieldType::Number, "", {}},
                      {"baseCost", "기준비용", CustomFieldType::Cost, "", {}},
                      {"label", "이름값", CustomFieldType::Text, "", {}},
                      {"baseDate", "기준날짜", CustomFieldType::Date, "", {}},
                      {"empty", "미정 입력", CustomFieldType::Number, "", {}}};
    for (std::size_t i = 0; i < 2; ++i)
        p.tasks[i].attributes = {{"field:q", i ? "4" : "2"},
                                 {"field:baseCost", i ? "2" : "12.34"},
                                 {"field:label", i ? "검토" : "설계"},
                                 {"field:baseDate", "2026-11-11T09:00:00"}};
    const std::vector<std::tuple<std::string, std::string, CustomFieldType, std::string>> fields = {
        {"n", "합산값", CustomFieldType::Number, "([수량]+1)*[Project.계수]"},
        {"flag", "선택 여부", CustomFieldType::Flag, "[수량]>=3 And Not False"},
        {"text", "구분", CustomFieldType::Text, "IIf([수량]>=3,\"대형\",\"소형\") & \":\" & [이름값]"},
        {"cost", "환산비용", CustomFieldType::Cost, "[기준비용]*[Project.계수]"},
        {"duration", "환산기간", CustomFieldType::Duration, "[Duration]*3"},
        {"date", "후속일시", CustomFieldType::Date, "DateAdd(\"d\",2,[기준날짜])"},
        {"start", "시작계산", CustomFieldType::Start, "DateAdd(\"h\",1,[기준날짜])"},
        {"finish", "종료계산", CustomFieldType::Finish, "CDate(\"2026-11-15T17:30:06\")"},
        {"mixed", "혼합계산", CustomFieldType::Number, "[nativeChain]+1"},
        {"summary", "요약표시", CustomFieldType::Text, "IIf([Summary],\"요약\",\"말단\")"},
        {"summaryCost", "요약비용", CustomFieldType::Cost, "[합산값]*10"},
        {"error", "계산오류", CustomFieldType::Number, "IIf(True,5,1/0)"},
        {"missing", "미설정 계산", CustomFieldType::Number, "[미정 입력]+1"},
        {"nullFlag", "누락여부", CustomFieldType::Flag, "IsNull([미정 입력])"},
        {"unknown", "미지원 함수", CustomFieldType::Number, "Now()"}};
    for (const auto &[id, name, type, source] : fields) {
        (void)source;
        CustomField f{id, name, type, "", {}};
        if (id == "n" || id == "duration")
            f.xmlRollUp = XmlRollUp{1, 3};
        if (id == "summary" || id == "summaryCost")
            f.xmlRollUp = XmlRollUp{2, 0};
        p.customFields.push_back(f);
    }
    p.customFields.push_back({"nativeChain", "nativeChain", CustomFieldType::Number, "", {}, "[field:n]*2"});
    Resource r;
    r.id = "R";
    r.name = "인력";
    r.attributes = {{"field:skill", "3"}, {"field:r", "99"}};
    p.resources = {r};
    p.resourceFields = {{"skill", "숙련", CustomFieldType::Number, "", {}},
                        {"r", "자원계산", CustomFieldType::Number, "", {}},
                        {"rText", "자원표시", CustomFieldType::Text, "", {}}};
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    p.assignmentFields = {{"manual", "배정입력", CustomFieldType::Number, "", {}},
                          {"a", "배정계산", CustomFieldType::Number, "", {}},
                          {"aText", "배정표시", CustomFieldType::Text, "", {}},
                          {"aDuration", "배정기간", CustomFieldType::Duration, "", {}}};
    p.assignments[0].attributes = {{"field:manual", "1"}, {"field:a", "999"}};
    p.assignments[1].attributes = {{"field:manual", "2"}};
    auto set = [&](FieldScope scope, const std::string &id, const std::string &source, bool enabled = true) {
        auto &list = customFields(p, scope);
        auto f = std::find_if(list.begin(), list.end(), [&](const auto &v) { return v.id == id; });
        f->xmlFormula = bindXmlFormula(p, scope, source);
        f->xmlFormula->enabled = enabled;
    };
    set(FieldScope::Project, "projectResult", "[계수]*3");
    for (const auto &[id, name, type, source] : fields) {
        (void)name;
        (void)type;
        set(FieldScope::Task, id, source, id != "unknown");
    }
    set(FieldScope::Resource, "r", "[숙련]*2");
    set(FieldScope::Resource, "rText", "[Name] & \" 담당\"");
    set(FieldScope::Assignment, "a", "[배정입력]+[Task.합산값]+[Project.계수]");
    set(FieldScope::Assignment, "aText", "[Resource.자원표시] & \":\" & [Task.이름값]");
    auto duration =
        std::find_if(p.customFields.begin(), p.customFields.end(), [](const auto &f) { return f.id == "duration"; });
    duration->xmlRollDown = XmlRollDown{"aDuration", true};
    p.tasks[0].attributes.push_back({"field:n", "999"});
    p.tasks[0].attributes.push_back({"field:error", "999"});
    p.tasks[0].attributes.push_back({"field:unknown", "777"});
    p.taskSummaryAttributes = {{"field:n", "999"}, {"field:summary", "옛 요약"}, {"field:summaryCost", "999.99"}};
    return p;
}
} // namespace ppm::test
