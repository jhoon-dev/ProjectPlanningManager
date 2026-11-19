#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include "ui/MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>

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
    throw std::runtime_error("expected rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write evidence");
}
Project fixture() {
    Project p;
    p.documentId = "enterprise-udf";
    p.name = "기업·프로젝트·배정 필드";
    p.startDate = "2026-11-10";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "설계", 480, {}}, {"B", "검토", 480, {}}};
    p.taskCosts = {{"T", 0}, {"B", 0}};
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    p.resources = {r};
    p.assignments = {{"T", "R", 100}, {"B", "R", 100}};
    p.customFields = {{"enterprise-text",
                       "기업 작업명",
                       CustomFieldType::Text,
                       {},
                       {},
                       {},
                       XmlFieldIdentity{"188776449", "0000e8d9-65f1-4769-9bd2-819d38036fcc", "Enterprise label"}}};
    p.tasks[0].attributes = {{"field:enterprise-text", "서울 & <설계>"}};
    p.tasks[1].attributes = {{"field:enterprise-text", ""}};
    p.resourceFields = {{"enterprise-number",
                         "기업 자원 점수",
                         CustomFieldType::Number,
                         {},
                         {},
                         {},
                         XmlFieldIdentity{"205553665", "11111111-65f1-4769-9bd2-819d38036fcc", "Enterprise rating"}}};
    p.resources[0].attributes = {{"field:enterprise-number", "-2.123456"}};
    p.projectFields = {{"project", "계약 번호", CustomFieldType::Text, {}, {}},
                       {"date", "프로젝트 검토일", CustomFieldType::Date, {}, {}}};
    p.projectAttributes = {{"field:project", "P-2026-서울"}, {"field:date", "2026-11-11"}};
    p.assignmentFields = {{"number", "배정 점수", CustomFieldType::Number, {}, {}},
                          {"text", "배정 메모", CustomFieldType::Text, {}, {}},
                          {"date", "배정 검토일", CustomFieldType::Date, {}, {}}};
    p.assignments[0].attributes = {{"field:number", "0"}, {"field:text", ""}, {"field:date", "2026-11-12"}};
    p.assignments[1].attributes = {{"field:number", "-12.000001"}};
    return p;
}
CustomField field(const Project &p, FieldScope scope, const std::string &name) {
    for (const auto &f : customFields(p, scope))
        if (f.name == name)
            return f;
    throw std::runtime_error("missing field: " + name);
}
void verify(const Project &p) {
    check(p.tasks.size() == 2 && p.assignments.size() == 2 && p.resources.size() == 1, "entity ownership");
    const auto &t = field(p, FieldScope::Task, "기업 작업명");
    check(t.xmlIdentity->fieldId == "188776449" && t.xmlIdentity->guid == "0000e8d9-65f1-4769-9bd2-819d38036fcc",
          "enterprise identity");
    check(attributeValue(p.tasks[0].attributes, "field:" + t.id) == "서울 & <설계>", "enterprise scalar");
    check(attributeValue(p.tasks[1].attributes, "field:" + t.id) == "", "enterprise empty");
    const auto &n = field(p, FieldScope::Assignment, "배정 점수");
    check(attributeValue(p.assignments[0].attributes, "field:" + n.id) == "0" &&
              attributeValue(p.assignments[1].attributes, "field:" + n.id) == "-12.000001",
          "assignment zero/precision/ownership");
    const auto &text = field(p, FieldScope::Assignment, "배정 메모");
    check(attributeValue(p.assignments[0].attributes, "field:" + text.id) == "" &&
              !attributeValue(p.assignments[1].attributes, "field:" + text.id),
          "assignment empty/missing");
    const auto &project = field(p, FieldScope::Project, "계약 번호");
    check(attributeValue(p.projectAttributes, "field:" + project.id) == "P-2026-서울", "project summary value");
    const auto &date = field(p, FieldScope::Project, "프로젝트 검토일");
    check(attributeValue(p.projectAttributes, "field:" + date.id) == "2026-11-11", "project date");
}
void contract() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto original = fixture();
    const auto xml = exportProjectXml(original);
    check(xml.project == original && xml.xml.contains("<b408001>") && xml.xml.contains("<b608000>") &&
              xml.xml.contains("<f408000>") && xml.xml.contains("<UID>0</UID>"),
          "official enterprise representation");
    check(projectXmlPreview(xml).contains("UDF ASSIGNMENT") && projectXmlPreview(xml).contains("UDF PROJECT"),
          "preview scopes");
    auto imported = importProjectXml(xml.xml, "enterprise.xml");
    verify(imported.project);
    check(nativeSchemaFor(imported.project) == 37, "conditional schema37");
    const auto replay = exportProjectXml(imported.project);
    verify(importProjectXml(replay.xml, "replay.xml").project);
    check(replay.xml.contains("<Guid>0000e8d9-65f1-4769-9bd2-819d38036fcc</Guid>"), "GUID re-export");
    auto mutate = [&](const QByteArray &from, const QByteArray &to) {
        auto bad = xml.xml;
        check(bad.contains(from), "mutation target absent");
        bad.replace(from, to);
        rejects([&] { importProjectXml(bad, "malformed.xml"); });
    };
    mutate("<b408001>", "<b408002>");
    mutate("<ElemType>23</ElemType>", "<ElemType>21</ElemType>");
    mutate("<f408000>0</f408000>", "<f408000>1e3</f408000>");
    mutate("<f408000>0</f408000>", "<f408000>0.0000001</f408000>");
    mutate("<f408000>0</f408000>", "<f408000>0</f408000><f408000>1</f408000>");
    mutate("T00:00:00</b608001>", "T24:00:00</b608001>");
    mutate("11111111-65f1-4769-9bd2-819d38036fcc", "0000e8d9-65f1-4769-9bd2-819d38036fcc");
    mutate("<b608000>P-2026-서울</b608000>", "<b608000>P-2026-서울</b608000><b608000>other</b608000>");
    auto repeated = xml.xml;
    const auto firstTask = repeated.indexOf("<UID>1</UID>", repeated.indexOf("<Tasks>"));
    const auto taskEnd = repeated.indexOf("</Task>", firstTask);
    repeated.insert(taskEnd, "<b608000>P-2026-서울</b608000>");
    verify(importProjectXml(repeated, "repeated.xml").project);
    repeated.replace(taskEnd, QByteArray("<b608000>P-2026-서울</b608000>").size(), "<b608000>conflict</b608000>");
    rejects([&] { importProjectXml(repeated, "conflict.xml"); });
    auto local = xml.xml;
    local.replace("255885312", "255868928");
    local.replace("f408000", "f404000");
    verify(importProjectXml(local, "local-assignment.xml").project);
    auto direct = xml.xml;
    direct.replace("<f408000>0</f408000>",
                   "<ExtendedAttribute><FieldID>255885312</FieldID><Value>0</Value></ExtendedAttribute>");
    verify(importProjectXml(direct, "direct-assignment.xml").project);
    auto secondary = xml.xml;
    secondary.replace("<Alias>기업 작업명</Alias>", "<Alias>기업 작업명</Alias><SecondaryPID>255869028</SecondaryPID>");
    secondary.insert(secondary.indexOf("</Assignment>", secondary.indexOf("<Assignments>")),
                     "<f404064>보조 배정 값</f404064>");
    const auto secondaryImported = importProjectXml(secondary, "secondary.xml").project;
    const auto secondaryKey = "field:" + field(secondaryImported, FieldScope::Assignment, "기업 작업명").id;
    check(attributeValue(secondaryImported.assignments[0].attributes, secondaryKey) == "보조 배정 값" &&
              importProjectXml(exportProjectXml(secondaryImported).xml, "secondary-replay.xml")
                      .project.assignmentFields.size() == 4,
          "SecondaryPID assignment ownership/re-export");
    auto formula = xml.xml;
    formula.replace("<Alias>기업 작업명</Alias>", "<Alias>기업 작업명</Alias><Formula>1/0</Formula>");
    verify(importProjectXml(formula, "formula.xml").project);
    const auto formulaImported = importProjectXml(formula, "formula.xml").project;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(formulaImported, scope))
            check(f.expression.empty(), "external expression executed");
    auto lookup = xml.xml;
    lookup.replace("<Alias>기업 작업명</Alias>",
                   "<Alias>기업 작업명</Alias><Ltuid>22222222-65f1-4769-9bd2-819d38036fcc</Ltuid>");
    rejects([&] { importProjectXml(lookup, "lookup.xml"); });
    auto body = json::encodeProject(imported.project);
    check(json::decodeProject(body, 37) == imported.project, "native exact roundtrip");
    rejects([&] { json::decodeProject(body, 36); });
    auto badNative = imported.project;
    badNative.assignments[0].attributes.push_back(badNative.assignments[0].attributes[0]);
    rejects([&] { validate(badNative); });
    badNative = imported.project;
    badNative.assignmentFields[0].expression = "1";
    rejects([&] { validate(badNative); });
    const auto path = temp.filePath("native.ppm").toStdString();
    store.save(path, imported.project, {});
    Document d(store);
    d.open(path);
    const auto assignmentKey = "field:" + field(*d.project(), FieldScope::Assignment, "배정 점수").id;
    const std::vector<std::string> ids{assignmentAttributeId(d.project()->assignments[0]),
                                       assignmentAttributeId(d.project()->assignments[1])};
    const auto before = *d.project();
    rejects([&] { d.setAssignmentAttribute({ids[0], "missing"}, assignmentKey, "3"); });
    check(*d.project() == before, "partial assignment update");
    auto prepared = Document::prepareEdit(
        d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, ids, assignmentKey, "7.25"}),
        std::make_shared<CalculationControl>());
    check(d.applyPreparedEdit(*prepared) == EditState::Applied && d.dirty(), "worker assignment apply/dirty");
    d.undo();
    check(!d.dirty() && d.project()->assignments == before.assignments, "assignment undo/savepoint");
    d.redo();
    d.undo();
    auto control = std::make_shared<CalculationControl>();
    prepared = Document::prepareEdit(
        d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, ids, assignmentKey, "7"}), control);
    control->cancel();
    check(d.applyPreparedEdit(*prepared) == EditState::Cancelled && !d.dirty(), "assignment cancel");
    prepared =
        Document::prepareEdit(d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, ids, assignmentKey, "7"}),
                              std::make_shared<CalculationControl>());
    d.open(path);
    check(d.applyPreparedEdit(*prepared) == EditState::Stale, "same document new session");
    d.captureBaseline("기업 필드 동결", "PM", "검토", "2026-11-10T01:00:00Z");
    const auto frozen = d.project()->baselines[0].input;
    d.saveScenario("필드 동결", "task-order", 260, "2026-11-10T01:10:00Z", d.project()->documentId,
                   d.project()->revision);
    d.setAssignmentAttribute(ids, assignmentKey, "9");
    check(d.project()->baselines[0].input == frozen, "baseline mutated");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-10T02:00:00Z"));
    session.reset();
    const auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema37 recovery");
    store.save(temp.filePath("frozen.ppm").toStdString(), *d.project(), {});
    check(store.load(temp.filePath("frozen.ppm").toStdString()).project == *d.project(), "archived native roundtrip");
    auto archivedOnly = *d.project();
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource})
        for (auto &f : customFields(archivedOnly, scope))
            f.xmlIdentity.reset();
    archivedOnly.assignmentFields.clear();
    for (auto &a : archivedOnly.assignments)
        a.attributes.clear();
    check(nativeSchemaFor(archivedOnly) == 37 &&
              json::decodeProject(json::encodeProject(archivedOnly), 37) == archivedOnly,
          "archived-only schema37");
    rejects([&] { json::decodeProject(json::encodeProject(archivedOnly), 36); });
    const auto retained = d.project()->assignments[0].attributes;
    const auto existing = d.project()->assignments[0];
    d.setAssignments(existing.taskId, {{existing.taskId, existing.resourceId, 50}});
    check(d.project()->assignments.back().attributes == retained, "rate editor erased assignment fields");
    d.undo();
    CurrencyConversionSubmission fx;
    fx.rate = {"KRW", "USD", "2026-11-10", "필드 보존 검증"};
    fx.reporter = "PM";
    fx.reason = "통화와 무관한 필드·동결 사본 보존";
    fx.recordedAt = "2026-11-10T02:10:00Z";
    fx.confirmed = true;
    d.convertCurrency(fx);
    check(json::decodeProject(json::encodeProject(*d.project()), 37) == *d.project() &&
              d.project()->currencyConversions[0].before->assignmentFields == frozen.assignmentFields,
          "currency transition schema37 snapshots");
    Document reader(store);
    reader.open(path, true);
    rejects([&] { reader.setAssignmentAttribute(ids, assignmentKey, "8"); });
    rejects([&] {
        reader.removeCustomField(field(*reader.project(), FieldScope::Assignment, "배정 점수").id,
                                 FieldScope::Assignment);
    });
    const auto package = publishProjectXml(xml, temp.path() + "/package");
    check(store.load((package + "/source.ppm").toStdString()).project == original, "source snapshot changed");
    auto legacy = original;
    legacy.assignmentFields.clear();
    for (auto &a : legacy.assignments)
        a.attributes.clear();
    for (auto scope : {FieldScope::Task, FieldScope::Resource})
        for (auto &f : customFields(legacy, scope))
            f.xmlIdentity.reset();
    check(nativeSchemaFor(legacy) == 16, "unnecessary schema migration");
    const auto output = QDir::currentPath() + "/enterprise-udf-qa";
    check(QDir().mkpath(output), "evidence directory");
    for (const auto *name : {"source.ppm", "fields.xlsx", "fields.pdf"})
        if (QFile::exists(output + '/' + name))
            check(QFile::remove(output + '/' + name), "replace generated evidence");
    write(output + "/export.xml", xml.xml);
    write(output + "/reexport.xml", replay.xml);
    write(output + "/mapping.json", QJsonDocument(xml.report).toJson());
    write(output + "/native.json", QJsonDocument(json::encodeProject(imported.project)).toJson());
    store.save((output + "/source.ppm").toStdString(), original, {});
    ReportOptions options;
    options.reportId = "enterprise-udf";
    options.generatedAt = "2026-11-10T02:00:00Z";
    const auto report = buildReport(original, options);
    exportXlsx(report, output + "/fields.xlsx");
    check(exportPdf(report, output + "/fields.pdf", ReportFamily::All) > 0, "PDF export");
    const auto xer = exportPrimaveraXer(original);
    const auto xerProject = importPrimaveraXer(xer.xer, "assignment.xer").project;
    const auto &xerNumber = field(xerProject, FieldScope::Assignment, "배정 점수");
    check(attributeValue(xerProject.assignments[1].attributes, "field:" + xerNumber.id) == "-12.000001",
          "XER assignment value");
    std::vector<PrimaveraXerExchange> batch{xer, xer};
    const auto combined = combinePrimaveraXer(batch);
    for (const auto &info : listPrimaveraXerProjects(combined)) {
        const auto selected = importPrimaveraXer(combined, "combined.xer", XerEncoding::Utf8, info.id).project;
        const auto &n = field(selected, FieldScope::Assignment, "배정 점수");
        check(attributeValue(selected.assignments[0].attributes, "field:" + n.id) == "0" &&
                  attributeValue(selected.assignments[1].attributes, "field:" + n.id) == "-12.000001",
              "XER selected assignment ownership");
    }
    write(output + "/export.xer", xer.xer);
    write(output + "/combined.xer", combined);
    std::cout << "PASS enterprise UDF: all scopes, typed values/identity, malformed references, "
                 "native/archives/recovery, atomic edits, reports\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    store.save(temp.filePath("ui.ppm").toStdString(), fixture(), {});
    Document d(store);
    d.open(temp.filePath("ui.ppm").toStdString());
    MainWindow window(d);
    window.show();
    QTest::qWait(30);
    auto *action = window.findChild<QAction *>("editAssignmentAttributes");
    check(action && window.findChild<QAction *>("manageAssignmentFields"), "assignment menus missing");
    QTimer::singleShot(0, [&] {
        try {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(dialog && dialog->objectName() == "assignmentAttributesDialog", "assignment dialog");
            auto *combo = dialog->findChild<QComboBox *>("objectFieldKey");
            combo->setCurrentIndex(combo->findData("field:number"));
            auto *table = dialog->findChild<QTableView *>("objectFieldValues");
            check(table->model()->rowCount() == 2, "assignment UI grain");
            check(table->model()->index(0, 0).data().toString() == "설계 / 설계 인력", "assignment names missing");
            table->selectAll();
            dialog->findChild<QLineEdit *>("objectFieldInput")->setText("7.250001");
            auto *apply = dialog->findChild<QPushButton *>("applyObjectField");
            apply->click();
            QElapsedTimer timer;
            timer.start();
            while (!apply->isEnabled() && timer.elapsed() < 15000)
                QTest::qWait(10);
            check(apply->isEnabled(), "assignment UI worker stalled");
            for (const auto &a : d.project()->assignments)
                check(attributeValue(a.attributes, "field:number") == "7.250001", "assignment UI missed values");
            const auto output = QDir::currentPath() + "/enterprise-udf-qa";
            QDir().mkpath(output);
            check(dialog->grab().save(output + "/assignment.png"), "UI screenshot");
            dialog->resize(800, 600);
            QTest::qWait(10);
            check(dialog->grab().save(output + "/assignment-small.png"), "small UI screenshot");
            d.renameTask(0, "다른 수정");
            check(!apply->isEnabled(), "stale assignment form enabled");
            dialog->reject();
        } catch (const std::exception &e) {
            std::cerr << e.what();
            std::exit(2);
        }
    });
    action->trigger();
    std::cout << "PASS enterprise UDF UI: assignment ownership/batch editing, small layout, stale protection\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    check(font >= 0, "Windows report font");
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
