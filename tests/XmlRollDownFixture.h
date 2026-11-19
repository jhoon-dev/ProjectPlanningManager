#pragma once
#include "domain/Classification.h"
#include "domain/XmlRollDown.h"
namespace ppm::test {
inline std::string rollGuid(int n) {
    auto v = std::to_string(n);
    return "66666666-7777-4888-9999-" + std::string(12 - v.size(), '0') + v;
}
inline Project xmlRollDownFixture() {
    Project p;
    p.documentId = "xml-roll-down";
    p.name = "XML 배정 roll-down";
    p.startDate = "2026-11-11";
    p.currency = "USD";
    p.calculationProfile = pdmProfile;
    p.tasks = {{"T", "설계", 480, {}}, {"B", "검토", 480, {}}};
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    p.resources = {r};
    r.id = "S";
    r.name = "검토 인력";
    p.resources.push_back(r);
    p.assignments = {{"T", "R", 100}, {"T", "S", 50}, {"B", "R", 100}};
    p.taskCosts = {{"T", 0}, {"B", 0}};
    for (int n = 0; n < 13; ++n) {
        const auto scope = n < 10 ? FieldScope::Task : FieldScope::Resource;
        const auto type = n < 9     ? CustomFieldType(n)
                          : n == 11 ? CustomFieldType::Cost
                          : n == 10 ? CustomFieldType::Text
                                    : CustomFieldType::Choice;
        CustomField f{"source" + std::to_string(n),
                      (scope == FieldScope::Task ? "작업 " : "자원 ") + std::to_string(n),
                      type,
                      {},
                      {}};
        f.xmlIdentity = XmlFieldIdentity{std::to_string((scope == FieldScope::Task ? 0x0b408000u : 0x0c408000u) + n),
                                         rollGuid(100 + n), f.name};
        if (type == CustomFieldType::Choice) {
            XmlLookup l;
            l.restrictValues = n != 3;
            if (n == 3)
                l.values = {{"one", "1101", {}, "alpha", {}, {}, true}, {"two", "1102", {}, "beta", {}, {}, true}};
            if (n == 9) {
                l.tableGuid = rollGuid(500);
                l.tableName = "다중 코드";
                l.maxValues = 0;
                l.masks = {{1, 1, 1, "."}};
                l.allLevelsRequired = true;
                l.values = {{"one", "1201", rollGuid(501), "A", {}, {}, true},
                            {"two", "1202", rollGuid(502), "B", {}, {}, true}};
            }
            if (n == 12) {
                l.valueType = CustomFieldType::Date;
                l.tableGuid = rollGuid(510);
                l.tableName = "날짜 목록";
                l.values = {{"one", "1301", rollGuid(511), "2026-11-11T15:30:12", {}, {}, true},
                            {"two", "1302", rollGuid(512), "2026-11-12", {}, {}, true}};
            }
            f.xmlLookup = l;
            for (const auto &v : l.values)
                f.options.push_back({v.optionId, v.value});
        }
        auto target = f;
        target.id = "target" + std::to_string(n);
        target.name = "배정 " + std::to_string(n);
        target.xmlIdentity = XmlFieldIdentity{std::to_string(0x0f408000u + n), rollGuid(200 + n), target.name};
        p.assignmentFields.push_back(target);
        f.xmlRollDown = XmlRollDown{target.id, true};
        customFields(p, scope).push_back(f);
    }
    p.assignmentFields.push_back({"independent", "독립 필드", CustomFieldType::Text, {}, {}});
    const std::vector<std::string> values = {"표준",
                                             "12.5",
                                             "2026-11-11T12:34:56.123456",
                                             "two",
                                             "0",
                                             "12.34",
                                             "90.5 em?",
                                             "2026-11-11T09:30:06",
                                             "2026-11-12T00:00:00",
                                             choiceValue(p.customFields[9], {"one", "two"})};
    for (std::size_t i = 0; i < values.size(); ++i)
        p.tasks[0].attributes.push_back({"field:source" + std::to_string(i), values[i]});
    p.tasks[1].attributes = {{"field:source0", ""},
                             {"field:source1", "0"},
                             {"field:source3", "one"},
                             {"field:source4", "1"},
                             {"field:source5", "-5"}};
    p.resources[0].attributes = {{"field:source10", "본부"}, {"field:source11", "20"}, {"field:source12", "one"}};
    p.resources[1].attributes = {{"field:source10", "외부"}, {"field:source11", "0"}};
    p.assignments[0].attributes = {{"field:target0", "직접"},
                                   {"field:target1", "12.5"},
                                   {"field:target4", "1"},
                                   {"field:target5", "0"},
                                   {"field:independent", "독립"}};
    p.assignments[0].rollDownMissing = {"field:target2"};
    p.assignments[2].attributes = {{"field:target6", "0 m"}};
    return p;
}
} // namespace ppm::test
