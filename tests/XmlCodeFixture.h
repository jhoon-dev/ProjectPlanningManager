#pragma once
#include "domain/Classification.h"
#include "domain/XmlCodes.h"
namespace ppm::test {
inline std::string codeGuid(int n) {
    auto digits = std::to_string(n);
    return "55555555-6666-4777-8888-" + std::string(12 - digits.size(), '0') + digits;
}
inline Project xmlCodeFixture() {
    Project p;
    p.documentId = "xml-code-masks";
    p.name = "XML 코드 마스크·생성";
    p.startDate = "2026-11-11";
    p.currency = "USD";
    p.calculationProfile = pdmProfile;
    p.wbsNodes = {{"G", "설계", {}}, {"H", "상세 설계", "G"}};
    p.tasks = {{"T", "설계 작업", 480, {}, "H"}, {"B", "검토", 480, {}, "G"}};
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    p.resources = {r};
    p.assignments = {{"T", "R", 100}, {"B", "R", 100}};
    p.taskCosts = {{"T", 0}, {"B", 0}};
    XmlLookup shared;
    shared.tableGuid = codeGuid(1);
    shared.tableName = "공정 코드";
    shared.masks = {{1, 1, 2, "."}, {2, 0, 2, "/"}, {3, 3, 0, ""}};
    shared.allLevelsRequired = true;
    shared.leafOnly = true;
    shared.values = {{"root", "101", codeGuid(11), "AA", {}, {}, true},
                     {"branch", "102", codeGuid(12), "01", {}, "root", true},
                     {"leaf", "103", codeGuid(13), "공정", "기존 코드", "branch", true}};
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment}) {
        const auto i = int(scope);
        CustomField f{"code" + std::to_string(i), "공정 코드 " + std::to_string(i), CustomFieldType::Choice, {}, {}};
        f.xmlIdentity = XmlFieldIdentity{std::to_string((scope == FieldScope::Task       ? 0x0b408080u
                                                         : scope == FieldScope::Project  ? 0x0b608080u
                                                         : scope == FieldScope::Resource ? 0x0c408080u
                                                                                         : 0x0f408080u)),
                                         codeGuid(101 + i), f.name};
        f.xmlLookup = shared;
        if (scope == FieldScope::Resource || scope == FieldScope::Assignment) {
            auto &l = *f.xmlLookup;
            l.tableGuid = codeGuid(2 + i);
            l.tableName = "분류 코드 " + std::to_string(i);
            l.leafOnly = false;
            l.masks = {{1, scope == FieldScope::Resource ? 0 : 2, scope == FieldScope::Resource ? 2 : 1, "."}};
            l.values = {{"only" + std::to_string(i),
                         std::to_string(104 + i),
                         codeGuid(14 + i),
                         scope == FieldScope::Resource ? "01" : "a",
                         {},
                         {},
                         true}};
        }
        rebuildXmlLookupOptions(f);
        customFields(p, scope).push_back(f);
    }
    p.tasks[0].attributes = {{"field:code0", "leaf"}};
    p.projectAttributes = {{"field:code1", "leaf"}};
    p.resources[0].attributes = {{"field:code2", "only2"}};
    p.assignments[0].attributes = {{"field:code3", "only3"}};
    p.xmlWbs = XmlWbsCodes{
        "PR-",
        true,
        true,
        {{1, 1, 2, "."}, {2, 0, 3, "-"}, {3, 2, 1, "."}},
        {{"wbs", "G", "PR-AA"}, {"wbs", "H", "PR-AA.001"}, {"task", "T", "PR-AA.001-a"}, {"task", "B", "PR-AA.002"}}};
    return p;
}
} // namespace ppm::test
