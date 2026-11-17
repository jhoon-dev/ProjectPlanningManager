#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/JsonProjectStore.h"
#include "domain/Classification.h"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QTemporaryDir>
#include <iostream>
namespace {
using namespace jobs;
void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected UDF rejection");
}
Project fixture() {
    Project p;
    p.documentId = "udf-contract";
    p.name = "UDF 검증";
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "설계", 480, {}}, {"B", "검토", 0, {}}};
    p.taskCosts = {{"T", 0}, {"B", 0}};
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    p.resources = {r};
    p.assignments = {{"T", "R", 100}};
    p.customFields = {{"text", "설명", CustomFieldType::Text, {}, {}},
                      {"n", "점수", CustomFieldType::Number, "점", {}},
                      {"date", "검토일", CustomFieldType::Date, {}, {}},
                      {"choice", "지역", CustomFieldType::Choice, {}, {{"seoul", "서울"}, {"busan", "부산"}}},
                      {"computed", "환산 점수", CustomFieldType::Number, "점", {}, "[field:n]*2"},
                      {"bad", "오류 결과", CustomFieldType::Number, {}, {}, "1/0"}};
    p.tasks[0].attributes = {{"field:text", "한글 & <상세>"},
                             {"field:n", "-2.123456"},
                             {"field:date", "2026-11-06"},
                             {"field:choice", "seoul"}};
    p.tasks[1].attributes = {{"field:text", ""}, {"field:n", "0"}};
    p.resourceFields = {{"score", "자원 점수", CustomFieldType::Number, {}, {}},
                        {"date", "자원 날짜", CustomFieldType::Date, {}, {}}};
    p.resources[0].attributes = {{"field:score", "0"}, {"field:date", "2026-11-09"}};
    p.projectFields = {{"score", "프로젝트 점수", CustomFieldType::Number, {}, {}}};
    p.projectAttributes = {{"field:score", "123.000001"}};
    return p;
}
std::optional<std::string> value(const Project &p, FieldScope s, const std::vector<TaskAttribute> &v,
                                 const std::string &name) {
    for (const auto &f : customFields(p, s))
        if (f.name == name) {
            const auto raw = attributeValue(v, "field:" + f.id);
            return raw && f.type == CustomFieldType::Choice ? std::optional{attributeText(p, "field:" + f.id, *raw, s)}
                                                            : raw;
        }
    throw std::runtime_error("missing field alias: " + name);
}
void verify(const Project &p, bool xml, bool selected = false) {
    check(p.tasks.size() == 2 && p.resources.size() == 1, "UDF topology");
    auto &t = p.tasks[0];
    auto &b = p.tasks[1];
    check(value(p, FieldScope::Task, t.attributes, "설명") == "한글 & <상세>", "text escaping");
    check(value(p, FieldScope::Task, b.attributes, "설명") == "", "explicit empty text");
    check(value(p, FieldScope::Task, t.attributes, "점수") == "-2.123456", "signed precision");
    check(value(p, FieldScope::Task, b.attributes, "점수") == "0", "zero");
    check(value(p, FieldScope::Task, t.attributes, "환산 점수") == "-4.246912", "formula snapshot");
    if (!selected)
        check(!value(p, FieldScope::Task, t.attributes, "오류 결과"), "error became zero");
    check(value(p, FieldScope::Task, t.attributes, "지역") == "서울", "choice label");
    check(value(p, FieldScope::Task, t.attributes, "검토일") == "2026-11-06" &&
              !value(p, FieldScope::Task, b.attributes, "검토일"),
          "date/missing");
    check(value(p, FieldScope::Resource, p.resources[0].attributes, "자원 점수") == "0", "resource numeric");
    check(value(p, FieldScope::Resource, p.resources[0].attributes, "자원 날짜") == "2026-11-09", "resource date");
    check(value(p, FieldScope::Project, p.projectAttributes, "프로젝트 점수") == "123.000001", "project numeric");
    (void)xml;
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource})
        for (const auto &f : customFields(p, s))
            check(f.expression.empty(), "external expression executed");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write output");
}
void run() {
    const auto p = fixture(), before = p;
    const auto x = exportProjectXml(p);
    const auto y = exportPrimaveraXer(p);
    check(p == before, "export mutated input");
    verify(importProjectXml(x.xml, "mapped.xml").project, true);
    verify(importPrimaveraXer(y.xer, "mapped.xer").project, false);
    check(projectXmlPreview(x).contains("UDF TASK · 환산 점수") &&
              primaveraXerPreview(y).contains("UDF PROJECT · 프로젝트 점수"),
          "reviewable mapping preview");
    check(!x.xml.contains("<Formula>") && !y.xer.contains("[field:n]"), "external executable expression");
    auto second = exportProjectXml(importProjectXml(x.xml, "mapped.xml").project);
    auto replay = importProjectXml(second.xml, "replay.xml").project;
    verify(replay, true);
    check(replay.customFields[0].id == importProjectXml(x.xml, "mapped.xml").project.customFields[0].id,
          "stable XML FieldID");
    auto malformed = x.xml;
    malformed.replace("<Value>-2.123456</Value>", "<Value>1e3</Value>");
    rejects([&] { importProjectXml(malformed, "bad.xml"); });
    malformed = x.xml;
    malformed.replace("T00:00:00</Value>", "T24:00:00</Value>");
    rejects([&] { importProjectXml(malformed, "bad.xml"); });
    malformed = x.xml;
    malformed.replace("<CFType>5</CFType>", "<CFType>7</CFType>");
    rejects([&] { importProjectXml(malformed, "bad.xml"); });
    malformed = x.xml;
    auto start = malformed.indexOf("<ExtendedAttribute>", malformed.indexOf("<Tasks>"));
    auto end = malformed.indexOf("</ExtendedAttribute>", start) + 20;
    malformed.insert(end, malformed.mid(start, end - start));
    rejects([&] { importProjectXml(malformed, "duplicate.xml"); });
    auto broken = y.xer;
    broken.replace("FT_FLOAT", "FT_FLAG");
    auto unsupported = importPrimaveraXer(broken, "unsupported.xer");
    check(unsupported.xer == broken && !unsupported.report["diagnostics"].toArray().isEmpty(),
          "unsupported UDF preservation");
    broken = y.xer;
    broken.replace("\t-2.123456\t", "\tNaN\t");
    rejects([&] { importPrimaveraXer(broken, "bad.xer"); });
    broken = y.xer;
    broken.replace("FT_FLOAT", "FT_INTEGER");
    rejects([&] { importPrimaveraXer(broken, "fractional-integer.xer"); });
    broken.replace("-2.123456", "-2");
    broken.replace("-4.246912", "-4");
    broken.replace("123.000001", "123");
    const auto integerProject = importPrimaveraXer(broken, "integer.xer").project;
    check(value(integerProject, FieldScope::Task, integerProject.tasks[0].attributes, "점수") == "-2",
          "integer UDF import");
    broken = y.xer;
    const auto valueTable = broken.indexOf("%T\tUDFVALUE");
    const auto textRow = broken.indexOf("%R\t", valueTable);
    const auto textEnd = broken.indexOf('\n', textRow);
    auto conflicting = broken.mid(textRow, textEnd - textRow).split('\t');
    conflicting[5] = "1";
    broken.replace(textRow, textEnd - textRow, conflicting.join('\t'));
    rejects([&] { importPrimaveraXer(broken, "mixed-value-columns.xer"); });
    auto many = p;
    many.customFields.clear();
    for (auto &t : many.tasks)
        t.attributes.clear();
    for (int i = 0; i < 31; ++i)
        many.customFields.push_back(
            {"text" + std::to_string(i), "텍스트" + std::to_string(i), CustomFieldType::Text, {}, {}});
    rejects([&] { exportProjectXml(many); });
    auto controls = p;
    controls.tasks[0].attributes[0].value = "탭\t값";
    rejects([&] { exportPrimaveraXer(controls); });
    std::vector<PrimaveraXerExchange> batch{y, exportPrimaveraXer(p)};
    const auto combined = combinePrimaveraXer(batch);
    check(combined.count("user_field_1\t") == 1, "combined UDF internal name collision");
    auto inventory = listPrimaveraXerProjects(combined);
    check(inventory.size() == 2, "combined UDF inventory");
    for (const auto &info : inventory)
        verify(importPrimaveraXer(combined, "batch.xer", XerEncoding::Utf8, info.id).project, false, true);
    auto badBatch = combined;
    auto pos = badBatch.indexOf("%T\tUDFVALUE");
    auto row = badBatch.indexOf("%R\t", pos);
    auto cells = badBatch.mid(row, badBatch.indexOf('\n', row) - row).split('\t');
    cells[2] = "999999";
    badBatch.replace(row, badBatch.indexOf('\n', row) - row, cells.join('\t'));
    rejects([&] { importPrimaveraXer(badBatch, "bad-batch.xer", XerEncoding::Utf8, inventory.back().id); });
    QTemporaryDir tmp;
    JsonProjectStore store;
    store.save(tmp.filePath("udf.ppm").toStdString(), replay, {});
    const auto reopened = store.load(tmp.filePath("udf.ppm").toStdString());
    verify(reopened.project, true);
    const auto output = QDir::currentPath() + "/dated-udf-qa";
    QDir().mkpath(output);
    write(output + "/mapped.xml", x.xml);
    write(output + "/mapped.xer", y.xer);
    write(output + "/combined.xer", combined);
    write(output + "/xml-mapping.json", QJsonDocument(x.report).toJson());
    write(output + "/xer-mapping.json", QJsonDocument(y.report).toJson());
    write(output + "/xml-preview.txt", projectXmlPreview(x).toUtf8());
    write(output + "/xer-preview.txt", primaveraXerPreview(y).toUtf8());
    auto package = publishProjectXml(x, output + "/xml-package-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    check(QFile::exists(package + "/source.ppm"), "native original preservation");
    std::cout << "PASS UDF: typed scopes, zero/empty/missing, static formula/choice, stable XML IDs, malformed/limits, "
                 "multi-project foreign keys, native save and reviewable package\n";
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        run();
    } catch (const AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << " " << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
