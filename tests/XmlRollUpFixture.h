#pragma once
#include "domain/Classification.h"
#include "domain/XmlRollUp.h"
namespace jobs::test {
inline Project xmlRollUpFixture() {
    Project p;
    p.documentId = "xml-roll-up";
    p.name = "XML 요약 roll-up";
    p.startDate = "2026-11-11";
    p.currency = "USD";
    p.calculationProfile = pdmProfile;
    p.wbsNodes = {{"G", "상위", {}}, {"H", "하위", "G"}};
    p.tasks = {{"A", "상위 직접 작업", 480, {}},
               {"B", "하위 작업 1", 480, {}},
               {"C", "하위 작업 2", 480, {}},
               {"D", "미분류 작업", 480, {}}};
    p.tasks[0].wbsId = "G";
    p.tasks[1].wbsId = p.tasks[2].wbsId = "H";
    for (int m = 0; m < 8; ++m) {
        CustomField f{"n" + std::to_string(m), "수치 " + std::to_string(m), CustomFieldType::Number, "점", {}};
        f.xmlRollUp = XmlRollUp{1, m};
        p.customFields.push_back(f);
        for (std::size_t i = 0; i < 4; ++i)
            p.tasks[i].attributes.push_back({"field:" + f.id, std::to_string(2 << i)});
    }
    for (auto [id, type] : std::vector<std::pair<std::string, CustomFieldType>>{{"cost", CustomFieldType::Cost},
                                                                                {"duration", CustomFieldType::Duration},
                                                                                {"date", CustomFieldType::Date},
                                                                                {"start", CustomFieldType::Start},
                                                                                {"finish", CustomFieldType::Finish},
                                                                                {"or", CustomFieldType::Flag},
                                                                                {"and", CustomFieldType::Flag}}) {
        CustomField f{id, id, type, {}, {}};
        f.xmlRollUp = XmlRollUp{1, id == "and" || id == "start" ? 1 : id == "cost" || id == "duration" ? 3 : 0};
        p.customFields.push_back(f);
        for (std::size_t i = 0; i < 4; ++i) {
            std::string v = type == CustomFieldType::Cost       ? "1.01"
                            : type == CustomFieldType::Duration ? "1.5 em?"
                            : type == CustomFieldType::Flag     ? (i == 0 ? "0" : "1")
                                                                : "2026-11-" + std::to_string(11 + i) +
                                                                  (type == CustomFieldType::Date ? "" : "T12:34:56");
            p.tasks[i].attributes.push_back({"field:" + id, v});
        }
    }
    CustomField snapshot{"snapshot", "명시 요약 메모", CustomFieldType::Text, {}, {}};
    snapshot.xmlRollUp = XmlRollUp{0, 0};
    p.customFields.push_back(snapshot);
    p.taskSummaryAttributes = {{"field:snapshot", "루트 메모"}, {"field:n3", "999"}, {"field:cost", "20.01"}};
    p.wbsNodes[0].attributes = {{"field:snapshot", "상위 메모"}, {"field:cost", "10.01"}};
    p.projectFields = {{"project", "프로젝트 별도 값", CustomFieldType::Text, {}, {}}};
    p.projectAttributes = {{"field:project", "프로젝트 범위"}};
    Resource r;
    r.id = "R";
    r.name = "자원 1";
    r.attributes = {{"field:resource", "2"}};
    p.resources = {r};
    r.id = "S";
    r.name = "자원 2";
    r.attributes[0].value = "6";
    p.resources.push_back(r);
    CustomField rf{"resource", "자원 수치", CustomFieldType::Number, "점", {}};
    rf.xmlRollUp = XmlRollUp{1, 3};
    p.resourceFields = {rf};
    return p;
}
} // namespace jobs::test
