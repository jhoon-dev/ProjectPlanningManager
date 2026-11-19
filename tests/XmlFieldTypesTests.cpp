#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/Classification.h"
#include "domain/ProjectCurrency.h"
#include "domain/ResourceView.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/UdfMapping.h"
#include "reporting/ReportExport.h"
#include "ui/ClassificationDialogs.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <algorithm>
#include <future>
namespace {
using namespace ppm;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected field type rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write evidence");
}
std::string guid(int n) {
    return "22222222-3333-4444-8555-" + QString::number(n).rightJustified(12, '0').toStdString();
}
std::vector<CustomField> fields(const std::string &prefix) {
    return {{prefix + "flag", prefix + " 승인", CustomFieldType::Flag, {}, {}},
            {prefix + "cost", prefix + " 금액", CustomFieldType::Cost, {}, {}},
            {prefix + "duration", prefix + " 기간", CustomFieldType::Duration, {}, {}}};
}
Project fixture() {
    Project p;
    p.documentId = "xml-field-types";
    p.name = "Flag·Cost·Duration";
    p.startDate = "2026-11-10";
    p.currency = "USD";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "설계", 480, {}}, {"B", "검토", 480, {}}};
    p.taskCosts = {{"T", 0}, {"B", 0}};
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    p.resources = {r};
    p.assignments = {{"T", "R", 100}, {"B", "R", 100}};
    p.customFields = fields("작업");
    p.projectFields = fields("프로젝트");
    p.resourceFields = fields("자원");
    p.assignmentFields = fields("배정");
    p.tasks[0].attributes = {{"field:작업flag", "0"}, {"field:작업cost", "-12.34"}, {"field:작업duration", "90.5 em?"}};
    p.tasks[1].attributes = {{"field:작업flag", "1"}, {"field:작업cost", "0"}};
    p.projectAttributes = {
        {"field:프로젝트flag", "0"}, {"field:프로젝트cost", "100.01"}, {"field:프로젝트duration", "-15.25 m?"}};
    p.resources[0].attributes = {{"field:자원flag", "1"}, {"field:자원cost", "0"}, {"field:자원duration", "0 m"}};
    p.assignments[0].attributes = {{"field:배정flag", "0"}, {"field:배정cost", "5.67"}, {"field:배정duration", "0 m"}};
    p.assignments[1].attributes = {{"field:배정duration", "1.5 m"}};
    auto enterprise = fields("기업");
    int slot = 188776450;
    for (auto &f : enterprise) {
        f.xmlIdentity = XmlFieldIdentity{std::to_string(slot), guid(slot), f.name};
        ++slot;
        p.customFields.push_back(f);
    }
    p.tasks[0].attributes.insert(
        p.tasks[0].attributes.end(),
        {{"field:기업flag", "1"}, {"field:기업cost", "42.01"}, {"field:기업duration", "1.000001 m"}});
    for (int i = 0; i < 3; ++i) {
        const auto type = i == 0 ? CustomFieldType::Cost : i == 1 ? CustomFieldType::Flag : CustomFieldType::Duration;
        CustomField f{"lookup" + std::to_string(i),
                      i == 0   ? "금액 목록"
                      : i == 1 ? "승인 목록"
                               : "기간 목록",
                      CustomFieldType::Choice,
                      {},
                      {{"one", "첫 값"}, {"two", "둘째 값"}}};
        XmlLookup l;
        l.valueType = type;
        if (i) {
            l.tableGuid = guid(100 + i);
            l.tableName = f.name;
            f.xmlIdentity = XmlFieldIdentity{std::to_string(188776453 + i), guid(200 + i), f.name};
            l.fieldGuid = f.xmlIdentity->guid;
            l.fieldName = f.name;
        }
        l.values = {{"one",
                     std::to_string(1000 + i * 10),
                     i ? guid(300 + i * 10) : "",
                     i == 2 ? "0 m" : "0",
                     "첫 값",
                     {},
                     true},
                    {"two",
                     std::to_string(1001 + i * 10),
                     i ? guid(301 + i * 10) : "",
                     i == 0   ? "-12.34"
                     : i == 1 ? "1"
                              : "1.5 m",
                     "둘째 값",
                     {},
                     true}};
        l.defaultOptionId = "one";
        f.xmlLookup = l;
        p.tasks[0].attributes.push_back({"field:" + f.id, "two"});
        p.customFields.push_back(f);
    }
    return p;
}
CustomField field(const Project &p, FieldScope scope, const std::string &name) {
    for (const auto &f : customFields(p, scope))
        if (f.name == name)
            return f;
    throw std::runtime_error("missing field");
}
std::optional<std::string> value(const Project &p, FieldScope scope, const std::vector<TaskAttribute> &v,
                                 const std::string &name) {
    return attributeValue(v, "field:" + field(p, scope, name).id);
}
void verify(const Project &p) {
    check(value(p, FieldScope::Task, p.tasks[0].attributes, "작업 승인") == "0" &&
              value(p, FieldScope::Task, p.tasks[1].attributes, "작업 승인") == "1",
          "flag false/true");
    check(value(p, FieldScope::Task, p.tasks[0].attributes, "작업 금액") == "-12.34" &&
              value(p, FieldScope::Task, p.tasks[1].attributes, "작업 금액") == "0",
          "cost signed/zero");
    check(value(p, FieldScope::Task, p.tasks[0].attributes, "작업 기간") == "90.5 em?" &&
              !value(p, FieldScope::Task, p.tasks[1].attributes, "작업 기간"),
          "duration elapsed/estimated/missing");
    check(value(p, FieldScope::Project, p.projectAttributes, "프로젝트 기간") == "-15.25 m?" &&
              value(p, FieldScope::Project, p.projectAttributes, "프로젝트 금액") == "100.01",
          "project values");
    check(value(p, FieldScope::Resource, p.resources[0].attributes, "자원 기간") == "0 m" &&
              value(p, FieldScope::Resource, p.resources[0].attributes, "자원 금액") == "0",
          "resource zero");
    check(value(p, FieldScope::Assignment, p.assignments[0].attributes, "배정 승인") == "0" &&
              !value(p, FieldScope::Assignment, p.assignments[1].attributes, "배정 승인"),
          "assignment false/missing");
    check(value(p, FieldScope::Assignment, p.assignments[1].attributes, "배정 기간") == "1.5 m", "assignment duration");
    check(value(p, FieldScope::Task, p.tasks[0].attributes, "기업 기간") == "1.000001 m" &&
              value(p, FieldScope::Task, p.tasks[0].attributes, "기업 금액") == "42.01",
          "enterprise typed scalars");
    for (int i = 0; i < 3; ++i) {
        auto f = field(p, FieldScope::Task, i == 0 ? "금액 목록" : i == 1 ? "승인 목록" : "기간 목록");
        auto ids = choiceSelection(f, *attributeValue(p.tasks[0].attributes, "field:" + f.id));
        check(ids.size() == 1 && ids[0] == f.xmlLookup->values[1].optionId, "typed lookup reference");
    }
}
void contract() {
    const auto original = fixture();
    validate(original);
    const auto xml = exportProjectXml(original);
    const auto imported = importProjectXml(xml.xml, "types.xml").project;
    verify(imported);
    // Reused XML recognizers must keep independent matches in concurrent imports,
    // including enterprise hex fields and valid/invalid baseline slot numbers.
    auto baselineXml = xml.xml;
    const auto endTask = baselineXml.indexOf("</Task>");
    check(endTask >= 0, "baseline owner missing");
    baselineXml.insert(endTask, "<Baseline><Number>+0</Number></Baseline>");
    auto invalidSlot = baselineXml;
    invalidSlot.replace("<Number>+0</Number>", "<Number>+11</Number>");
    auto parse = [&] {
        for (int iteration = 0; iteration < 3; ++iteration)
            for (const auto *bytes : {&baselineXml, &invalidSlot}) {
                auto result = importProjectXml(*bytes, "concurrent.xml");
                result.project.documentId = imported.documentId;
                check(result.project == imported, "concurrent import changed typed values");
                check(result.baselineCandidates.size() == 1 &&
                          result.baselineCandidates.front().issue.empty() == (bytes == &baselineXml),
                      "concurrent baseline slot validation changed");
            }
    };
    auto first = std::async(std::launch::async, parse);
    auto second = std::async(std::launch::async, parse);
    first.get();
    second.get();
    auto costTable = original;
    auto &lookup = *costTable.customFields[6].xmlLookup;
    lookup.tableGuid = guid(600);
    lookup.tableName = "금액 목록";
    for (std::size_t i = 0; i < lookup.values.size(); ++i)
        lookup.values[i].guid = guid(610 + int(i));
    verify(importProjectXml(exportProjectXml(costTable).xml, "cost-guid-table.xml").project);
    const auto reexport = exportProjectXml(imported);
    const auto replay = importProjectXml(reexport.xml, "replay.xml").project;
    verify(replay);
    check(replay.customFields == imported.customFields, "typed dictionary roundtrip");
    check(nativeSchemaFor(imported) == 39 && nativeSchemaFor(Project{}) < 39, "conditional schema39");
    const auto body = json::encodeProject(imported);
    check(json::decodeProject(body, 39) == imported, "schema39 metadata");
    rejects([&] { json::decodeProject(body, 38); });
    auto bad = [&](QByteArray from, QByteArray to) {
        auto bytes = xml.xml;
        check(bytes.contains(from), "mutation missing");
        bytes.replace(from, to);
        rejects([&] { importProjectXml(bytes, "bad.xml"); });
    };
    bad("<Value>-1234</Value>", "<Value>-1234.5</Value>");
    bad("<DurationFormat>36</DurationFormat>", "<DurationFormat>19</DurationFormat>");
    bad("<Value>PT1H30M30S</Value>", "<Value>P1M</Value>");
    bad("<Value>PT1H30M30S</Value>", "<Value>PT0.5H</Value>");
    bad("<Value>PT1H30M30S</Value>", "<Value>PT0H0M0.000001S</Value>");
    auto inheritedFormat = xml.xml;
    inheritedFormat.replace("<DurationFormat>36</DurationFormat>", "");
    inheritedFormat.insert(inheritedFormat.indexOf("<ExtendedAttributes>"), "<DurationFormat>38</DurationFormat>");
    verify(importProjectXml(inheritedFormat, "default-duration-format.xml").project);
    auto invalid = original;
    invalid.tasks[0].attributes[0].value = "maybe";
    rejects([&] { validate(invalid); });
    invalid = original;
    invalid.tasks[0].attributes[1].value = "1.234";
    rejects([&] { validate(invalid); });
    invalid = original;
    invalid.tasks[0].attributes[2].value = "90 hours";
    rejects([&] { validate(invalid); });
    check(importXmlUdfScalar(original, CustomFieldType::Flag, "true") == "1" &&
              importXmlUdfScalar(original, CustomFieldType::Flag, "false") == "0",
          "XML bool lexical values");
    rejects([&] { importXmlUdfScalar(original, CustomFieldType::Flag, "yes"); });
    check(importXmlUdfScalar(original, CustomFieldType::Duration, "PT2H", 8) == "120 em" &&
              exportXmlUdfScalar(original, CustomFieldType::Duration, "120 em") == "PT2H0M0S",
          "duration normalization");
    rejects([&] { importXmlUdfScalar(original, CustomFieldType::Duration, "PT1H", 21); });
    check(formatFieldCost(parseFieldCost("-90000000000", "USD"), "USD") == "-90000000000", "cost boundary");
    rejects([&] { parseFieldCost("90000000000.01", "USD"); });
    auto jpy = original;
    jpy.currency = "JPY";
    rejects([&] { importXmlUdfScalar(jpy, CustomFieldType::Cost, "1"); });
    check(importXmlUdfScalar(jpy, CustomFieldType::Cost, "100") == "1", "whole currency cent scale");
    QTemporaryDir temp;
    JsonProjectStore store;
    Document costOnly(store);
    costOnly.create("금액 단위 보호", "2026-11-10", {"작업"});
    costOnly.setCurrency("USD");
    const auto costId =
        costOnly.putCustomField({{}, "사용자 금액", CustomFieldType::Cost, {}, {}}, FieldScope::Project);
    costOnly.setProjectAttribute("field:" + costId, "0");
    rejects([&] { costOnly.setCurrency("EUR"); });
    check(costOnly.project()->currency == "USD", "custom cost cannot relabel currency");
    const auto path = temp.filePath("types.ppm").toStdString();
    store.save(path, imported, {});
    check(store.load(path).project == imported, "native file");
    Document d(store);
    d.open(path);
    const auto af = field(imported, FieldScope::Assignment, "배정 금액");
    const auto key = "field:" + af.id;
    std::vector<std::string> ids;
    for (const auto &a : d.project()->assignments)
        ids.push_back(assignmentAttributeId(a));
    const auto before = *d.project();
    rejects([&] { d.setAssignmentAttribute({ids[0], "missing"}, key, "7.89"); });
    check(*d.project() == before, "atomic typed batch");
    auto prepared =
        Document::prepareEdit(d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, ids, key, "7.89"}),
                              std::make_shared<CalculationControl>());
    check(d.applyPreparedEdit(*prepared) == EditState::Applied, "typed worker");
    d.undo();
    check(!d.dirty() && d.project()->assignments == before.assignments, "typed undo savepoint");
    d.redo();
    d.undo();
    auto control = std::make_shared<CalculationControl>();
    prepared = Document::prepareEdit(d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, ids, key, "7.89"}),
                                     control);
    control->cancel();
    check(d.applyPreparedEdit(*prepared) == EditState::Cancelled, "typed cancel");
    prepared = Document::prepareEdit(d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, ids, key, "7.89"}),
                                     std::make_shared<CalculationControl>());
    d.open(path);
    check(d.applyPreparedEdit(*prepared) == EditState::Stale, "typed stale");
    d.captureBaseline("추가 형식 동결", "PM", "검토", "2026-11-10T05:00:00Z");
    d.saveScenario("추가 형식 사본", "task-order", 260, "2026-11-10T05:00:00Z", d.project()->documentId,
                   d.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-10", "금액 필드 환산"};
    fx.rate.numerator = 3;
    fx.rate.denominator = 2;
    fx.reporter = "PM";
    fx.reason = "사용자 금액·동결 목록 보존";
    fx.recordedAt = "2026-11-10T05:10:00Z";
    fx.confirmed = true;
    const auto rows = currencyMoneyRows(*d.project());
    check(std::any_of(rows.begin(), rows.end(),
                      [](const auto &r) { return r.path.find("customCost") != std::string::npos; }),
          "custom cost conversion preview");
    d.convertCurrency(fx);
    check(value(*d.project(), FieldScope::Task, d.project()->tasks[0].attributes, "작업 금액") == "-18.51" &&
              value(*d.project(), FieldScope::Project, d.project()->projectAttributes, "프로젝트 금액") == "150.02",
          "custom cost FX");
    check(d.project()->baselines[0].input.currency == "EUR" &&
              d.project()->baselines[0].input.customFields.back().xmlLookup->valueType == CustomFieldType::Duration,
          "typed frozen FX");
    const auto costLookup = field(*d.project(), FieldScope::Task, "금액 목록");
    check(costLookup.xmlLookup->values[1].value == "-18.51" && costLookup.options[1].name == "-18.51",
          "cost dictionary FX IDs and labels");
    check(json::decodeProject(json::encodeProject(*d.project()), 39) == *d.project(), "transition snapshots schema39");
    check(!d.canUndo(), "currency transition preserves audit barrier");
    d.open(path);
    const auto beforeCollapse = *d.project();
    auto collapse = fx;
    collapse.rate.targetCurrency = "JPY";
    collapse.rate.numerator = 1;
    collapse.rate.denominator = 100;
    rejects([&] { d.convertCurrency(collapse); });
    check(*d.project() == beforeCollapse, "list collision conversion atomic");
    auto whole = fx;
    whole.rate.targetCurrency = "JPY";
    whole.rate.numerator = 100;
    whole.rate.denominator = 1;
    d.convertCurrency(whole);
    check(value(*d.project(), FieldScope::Task, d.project()->tasks[0].attributes, "작업 금액") == "-1234",
          "whole currency custom cost FX");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-10T05:20:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "schema39 recovery");
    auto archived = *d.project();
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment}) {
        for (auto &f : customFields(archived, scope)) {
            f.type = CustomFieldType::Text;
            f.options.clear();
            f.xmlLookup.reset();
        }
    }
    check(nativeSchemaFor(archived) == 39, "archived types schema");
    const auto xer = exportPrimaveraXer(imported);
    const auto xerRead = importPrimaveraXer(xer.xer, "types.xer").project;
    check(value(xerRead, FieldScope::Task, xerRead.tasks[0].attributes, "작업 금액") == "-12.34" &&
              value(xerRead, FieldScope::Task, xerRead.tasks[0].attributes, "작업 승인") == "아니요",
          "XER static projection");
    const auto output = QDir::currentPath() + "/xml-field-types-qa";
    QDir().mkpath(output);
    write(output + "/export.xml", xml.xml);
    write(output + "/reexport.xml", reexport.xml);
    write(output + "/export.xer", xer.xer);
    write(output + "/native.json", QJsonDocument(body).toJson());
    write(output + "/mapping.json", QJsonDocument(xml.report).toJson());
    for (const auto *name : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(output + '/' + name);
    store.save((output + "/source.ppm").toStdString(), imported, {});
    ReportOptions options;
    options.reportId = "xml-field-types";
    options.generatedAt = "2026-11-10T05:00:00Z";
    const auto report = buildReport(imported, options);
    exportXlsx(report, output + "/fields.xlsx");
    check(exportPdf(report, output + "/fields.pdf") > 0, "PDF export");
    std::cout << "PASS XML additional types: Flag/Cost/Duration four scopes, typed lookup/GUID, zero/missing, "
                 "precision/units, schema39/archives/FX/recovery, worker/undo, static XER/report\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto p = importProjectXml(exportProjectXml(fixture()).xml, "ui.xml").project;
    const auto path = temp.filePath("ui.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    const auto output = QDir::currentPath() + "/xml-field-types-qa";
    QDir().mkpath(output);
    QTimer::singleShot(0, [&] {
        try {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(dialog, "typed object dialog");
            auto *key = dialog->findChild<QComboBox *>("objectFieldKey");
            auto *table = dialog->findChild<QTableView *>("objectFieldValues");
            table->selectAll();
            auto *apply = dialog->findChild<QPushButton *>("applyObjectField");
            auto wait = [&] {
                QElapsedTimer t;
                t.start();
                while (!apply->isEnabled() && t.elapsed() < 15000)
                    QTest::qWait(10);
                check(apply->isEnabled(), "typed UI worker stalled");
            };
            const auto flag = field(*d.project(), FieldScope::Assignment, "배정 승인");
            key->setCurrentIndex(key->findData(QString::fromStdString("field:" + flag.id)));
            auto *choice = dialog->findChild<QComboBox *>("objectFieldChoice");
            check(choice->isVisible(), "Flag selector");
            choice->setCurrentIndex(choice->findData("0"));
            apply->click();
            wait();
            for (const auto &a : d.project()->assignments)
                check(attributeValue(a.attributes, "field:" + flag.id) == "0", "false batch UI");
            const auto cost = field(*d.project(), FieldScope::Assignment, "배정 금액");
            key->setCurrentIndex(key->findData(QString::fromStdString("field:" + cost.id)));
            dialog->findChild<QLineEdit *>("objectFieldInput")->setText("-7.89");
            apply->click();
            wait();
            for (const auto &a : d.project()->assignments)
                check(attributeValue(a.attributes, "field:" + cost.id) == "-7.89", "cost batch UI");
            const auto duration = field(*d.project(), FieldScope::Assignment, "배정 기간");
            key->setCurrentIndex(key->findData(QString::fromStdString("field:" + duration.id)));
            dialog->findChild<QLineEdit *>("objectFieldInput")->setText("15.5 em?");
            apply->click();
            wait();
            for (const auto &a : d.project()->assignments)
                check(attributeValue(a.attributes, "field:" + duration.id) == "15.5 em?", "duration batch UI");
            check(dialog->grab().save(output + "/assignment.png"), "typed screenshot");
            dialog->resize(800, 600);
            QTest::qWait(10);
            check(dialog->grab().save(output + "/assignment-small.png"), "typed small screenshot");
            d.renameTask(0, "다른 수정");
            check(!apply->isEnabled(), "typed stale form");
            dialog->reject();
        } catch (const std::exception &e) {
            std::cerr << e.what();
            std::exit(2);
        }
    });
    editObjectAttributes(d, FieldScope::Assignment, nullptr);
    QTimer::singleShot(0, [&] {
        try {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            auto *key = dialog->findChild<QComboBox *>("taskAttributeKey");
            const auto flag = field(*d.project(), FieldScope::Task, "작업 승인");
            key->setCurrentIndex(key->findData(QString::fromStdString("field:" + flag.id)));
            auto *value = dialog->findChild<QComboBox *>("taskAttributeValue");
            check(!value->isEditable(), "task Flag discrete selector");
            value->setCurrentIndex(value->findData("1"));
            dialog->findChild<QPushButton *>("applyTaskAttribute")->click();
            check(attributeValue(d.project()->tasks[0].attributes, "field:" + flag.id) == "1", "task Flag applied");
            check(dialog->grab().save(output + "/task-flag.png"), "Flag screenshot");
            dialog->reject();
        } catch (const std::exception &e) {
            std::cerr << e.what();
            std::exit(2);
        }
    });
    editTaskAttributes(d, {d.project()->tasks[0].id}, nullptr);
    std::cout << "PASS XML additional types UI: discrete Flag, signed Cost, elapsed/estimated Duration batch worker, "
                 "task editor, 800x600, stale\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    check(font >= 0, "Windows font");
    QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        if (app.arguments().contains("--ui"))
            ui();
        else
            contract();
    } catch (const AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << ' ' << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
