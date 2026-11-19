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
#include "ui/TaskViewProjection.h"
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
#include <algorithm>
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
    throw std::runtime_error("expected date/time rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write evidence");
}
std::string guid(int n) {
    return "33333333-4444-4555-8666-" + QString::number(n).rightJustified(12, '0').toStdString();
}
std::vector<CustomField> fields(const std::string &scope) {
    return {{scope + "date", scope + " 날짜", CustomFieldType::Date, {}, {}},
            {scope + "start", scope + " 시작 일시", CustomFieldType::Start, {}, {}},
            {scope + "finish", scope + " 종료 일시", CustomFieldType::Finish, {}, {}}};
}
Project fixture() {
    Project p;
    p.documentId = "xml-date-time";
    p.name = "Start·Finish·날짜와 시각";
    p.startDate = "2026-11-10";
    p.currency = "USD";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "설계", 480, {}}, {"B", "검토", 480, {}}};
    p.taskCosts = {{"T", 0}, {"B", 0}};
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    p.resources = {r};
    r.id = "S";
    r.name = "검토 인력";
    p.resources.push_back(r);
    p.assignments = {{"T", "R", 100}, {"B", "S", 100}};
    p.customFields = fields("작업");
    p.projectFields = fields("프로젝트");
    p.resourceFields = fields("자원");
    p.assignmentFields = fields("배정");
    p.tasks[0].attributes = {{"field:작업date", "2026-11-10T12:34:56.123456"},
                             {"field:작업start", "2026-11-10T09:30:06"},
                             {"field:작업finish", "2026-11-11T00:00:00"}};
    p.tasks[1].attributes = {{"field:작업date", "2026-11-11"}, {"field:작업start", "2026-11-10T10:00:00.5"}};
    p.projectAttributes = {{"field:프로젝트date", "2026-11-10T12:00:00"},
                           {"field:프로젝트start", "2026-11-10T00:00:00"},
                           {"field:프로젝트finish", "2026-11-11T18:30:00"}};
    p.resources[0].attributes = {{"field:자원date", "2026-11-10"},
                                 {"field:자원start", "2026-11-12T09:00:00"},
                                 {"field:자원finish", "2026-11-12T18:15:12"}};
    p.resources[1].attributes = {{"field:자원date", "2026-11-10T01:02:03"}, {"field:자원start", "2026-11-09T11:00:00"}};
    p.assignments[0].attributes = {{"field:배정date", "2026-11-10"},
                                   {"field:배정start", "2026-11-10T08:00:00.123"},
                                   {"field:배정finish", "2026-11-10T19:00:00"}};
    p.assignments[1].attributes = {{"field:배정start", "2026-11-11T09:00:00"}};
    for (int i = 0; i < 2; ++i) {
        CustomField f{"enterprise" + std::to_string(i),
                      i ? "기업 종료 일시" : "기업 시작 일시",
                      i ? CustomFieldType::Finish : CustomFieldType::Start,
                      {},
                      {}};
        f.xmlIdentity = XmlFieldIdentity{std::to_string(188776450 + i), guid(100 + i), f.name};
        p.tasks[0].attributes.push_back({"field:" + f.id, i ? "2026-11-12T00:00:00" : "2026-11-10T23:59:59.999999"});
        p.customFields.push_back(f);
    }
    for (int i = 0; i < 3; ++i) {
        const auto type = i == 0 ? CustomFieldType::Start : i == 1 ? CustomFieldType::Finish : CustomFieldType::Date;
        CustomField f{"lookup" + std::to_string(i),
                      i == 0   ? "시작 목록"
                      : i == 1 ? "종료 목록"
                               : "날짜 목록",
                      CustomFieldType::Choice,
                      {},
                      {{"one", "첫 값"}, {"two", "둘째 값"}}};
        XmlLookup l;
        l.valueType = type;
        if (i) {
            l.tableGuid = guid(200 + i);
            l.tableName = f.name;
            f.xmlIdentity = XmlFieldIdentity{std::to_string(188776452 + i), guid(300 + i), f.name};
            l.fieldGuid = f.xmlIdentity->guid;
            l.fieldName = f.name;
        }
        l.values = {{"one",
                     std::to_string(1000 + i * 10),
                     i ? guid(400 + i * 10) : "",
                     i == 0   ? "2026-11-10T09:00:00.123456"
                     : i == 1 ? "2026-11-10T18:00:00"
                              : "2026-11-10",
                     "첫 값",
                     {},
                     true},
                    {"two",
                     std::to_string(1001 + i * 10),
                     i ? guid(401 + i * 10) : "",
                     i == 0   ? "2026-11-10T11:00:00"
                     : i == 1 ? "2026-11-11T00:00:06"
                              : "2026-11-10T15:30:12",
                     "둘째 값",
                     {},
                     true}};
        l.defaultOptionId = "one";
        f.xmlLookup = l;
        p.tasks[0].attributes.push_back({"field:" + f.id, "two"});
        p.customFields.push_back(f);
    }
    auto shared = p.customFields.back();
    shared.id = "shared-start";
    shared.name = "자원 시작 목록";
    shared.xmlIdentity.reset();
    shared.xmlLookup->fieldGuid.clear();
    shared.xmlLookup->fieldName.clear();
    shared.xmlLookup->valueType = CustomFieldType::Start;
    shared.xmlLookup->values[0].value += "T00:00:00";
    p.resourceFields.push_back(shared);
    p.resources[0].attributes.push_back({"field:shared-start", "two"});
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
    check(value(p, FieldScope::Task, p.tasks[0].attributes, "작업 날짜") == "2026-11-10T12:34:56.123456",
          "Date keeps fractional clock");
    check(value(p, FieldScope::Task, p.tasks[1].attributes, "작업 날짜") == "2026-11-11",
          "legacy Date stays all-day representation");
    check(value(p, FieldScope::Task, p.tasks[0].attributes, "작업 시작 일시") == "2026-11-10T09:30:06" &&
              value(p, FieldScope::Task, p.tasks[0].attributes, "작업 종료 일시") == "2026-11-11T00:00:00" &&
              !value(p, FieldScope::Task, p.tasks[1].attributes, "작업 종료 일시"),
          "Start/Finish clock midnight/missing");
    check(field(p, FieldScope::Task, "작업 시작 일시").type == CustomFieldType::Start &&
              field(p, FieldScope::Task, "작업 종료 일시").type == CustomFieldType::Finish,
          "date roles");
    check(value(p, FieldScope::Project, p.projectAttributes, "프로젝트 날짜") == "2026-11-10T12:00:00" &&
              value(p, FieldScope::Project, p.projectAttributes, "프로젝트 시작 일시") == "2026-11-10T00:00:00",
          "project datetime");
    check(value(p, FieldScope::Resource, p.resources[1].attributes, "자원 날짜") == "2026-11-10T01:02:03",
          "resource datetime");
    check(value(p, FieldScope::Assignment, p.assignments[0].attributes, "배정 시작 일시") ==
                  "2026-11-10T08:00:00.123" &&
              !value(p, FieldScope::Assignment, p.assignments[1].attributes, "배정 종료 일시"),
          "assignment fractional/missing");
    check(value(p, FieldScope::Task, p.tasks[0].attributes, "기업 시작 일시") == "2026-11-10T23:59:59.999999",
          "enterprise clock");
    for (const auto &name : {"시작 목록", "종료 목록", "날짜 목록"}) {
        auto f = field(p, FieldScope::Task, name);
        check(*attributeValue(p.tasks[0].attributes, "field:" + f.id) == f.xmlLookup->values[1].optionId,
              "date lookup selection");
    }
    const auto shared = field(p, FieldScope::Resource, "자원 시작 목록");
    check(shared.xmlLookup->valueType == CustomFieldType::Start &&
              shared.xmlLookup->values[0].value == "2026-11-10T00:00:00" &&
              *attributeValue(p.resources[0].attributes, "field:" + shared.id) == shared.xmlLookup->values[1].optionId,
          "shared Date/Start lookup");
    check(!p.tasks[0].constraintAt && p.tasks[0].durationMinutes == 480,
          "custom dates did not adopt schedule constraints");
}
void contract() {
    auto original = fixture();
    validate(original);
    const auto xml = exportProjectXml(original);
    const auto imported = importProjectXml(xml.xml, "dates.xml").project;
    verify(imported);
    const auto reexport = exportProjectXml(imported);
    const auto replay = importProjectXml(reexport.xml, "replay.xml").project;
    verify(replay);
    check(imported.customFields == replay.customFields && imported.resourceFields == replay.resourceFields,
          "date roles/lookup identities roundtrip");
    check(normalizeFieldDateTime("2024-02-29 09:30") == "2024-02-29T09:30:00" &&
              normalizeFieldDateTime("2026-11-10T12:00:00.120000") == "2026-11-10T12:00:00.12",
          "timestamp canonicalization");
    for (const auto &bad :
         {"2026-02-29T09:00:00", "2026-11-10T24:00:00", "2026-11-10T09:60:00", "2026-11-10T09:00:60",
          "2026-11-10T09:00:00Z", "2026-11-10T09:00:00+09:00", "2026-11-10T09:00:00.1234567", "NA", "2026-11-10"})
        rejects([&] { normalizeFieldDateTime(bad); });
    check(unpackFieldDateTime(0) == "1983-12-31T00:00:00" &&
              unpackFieldDateTime(packFieldDateTime("2026-11-10T09:30:06")) == "2026-11-10T09:30:06",
          "packed epoch/date clock");
    rejects([&] { unpackFieldDateTime(14400); });
    rejects([&] { packFieldDateTime("2026-11-10T09:30:01"); });
    rejects([&] { packFieldDateTime("2026-11-10T09:30:00.1"); });
    rejects([&] { packFieldDateTime("1983-12-30T00:00:00"); });
    auto bad = [&](QByteArray from, QByteArray to) {
        auto bytes = xml.xml;
        check(bytes.contains(from), "mutation missing");
        bytes.replace(from, to);
        rejects([&] { importProjectXml(bytes, "bad.xml"); });
    };
    bad("2026-11-10T09:30:06", "2026-11-10T24:30:06");
    bad("2026-11-10T12:34:56.123456", "2026-11-10T12:34:56Z");
    bad("<CFType>6</CFType>", "<CFType>3</CFType>");
    auto missing = xml.xml;
    missing.replace("<Value>2026-11-11T00:00:00</Value>", "<Value>NA</Value>");
    const auto noFinish = importProjectXml(missing, "na.xml").project;
    check(!value(noFinish, FieldScope::Task, noFinish.tasks[0].attributes, "작업 종료 일시"),
          "NA becomes missing, not midnight");
    missing = xml.xml;
    missing.replace("<Value>2026-11-10T09:30:06</Value>", "<Value></Value>");
    const auto empty = importProjectXml(missing, "empty.xml").project;
    check(!value(empty, FieldScope::Task, empty.tasks[0].attributes, "작업 시작 일시"), "empty becomes missing");
    missing = xml.xml;
    missing.replace("<Value>2026-11-10T11:00:00</Value>",
                    "<Value>NA</Value>"); // includes ordinary list definition: invalid dictionary, not a missing option
    rejects([&] { importProjectXml(missing, "na-in-dictionary.xml"); });
    const auto body = json::encodeProject(imported);
    check(nativeSchemaFor(imported) == 40 && json::decodeProject(body, 40) == imported, "schema40");
    rejects([&] { json::decodeProject(body, 39); });
    auto dateOnly = original;
    dateOnly.customFields = {{"date", "日付", CustomFieldType::Date, {}, {}}};
    for (auto &t : dateOnly.tasks)
        t.attributes.clear();
    dateOnly.tasks[0].attributes = {{"field:date", "2026-11-10T12:00:00"}};
    dateOnly.projectFields.clear();
    dateOnly.projectAttributes.clear();
    dateOnly.resourceFields.clear();
    for (auto &r : dateOnly.resources)
        r.attributes.clear();
    dateOnly.assignmentFields.clear();
    for (auto &a : dateOnly.assignments)
        a.attributes.clear();
    check(nativeSchemaFor(dateOnly) == 40, "Date with clock schema40");
    rejects([&] { json::decodeProject(json::encodeProject(dateOnly), 39); });
    dateOnly.tasks[0].attributes[0].value = "2026-11-10";
    check(nativeSchemaFor(dateOnly) < 40 &&
              json::decodeProject(json::encodeProject(dateOnly), nativeSchemaFor(dateOnly)) == dateOnly,
          "legacy Date minimum writer");
    ResourceViewSettings view;
    view.sortKey = "field:" + field(imported, FieldScope::Resource, "자원 시작 일시").id;
    auto projected = projectResources(imported, view);
    check(projected[0].resourceIndex == 1, "resource chronological sort");
    view.filters = {{view.sortKey, "lt", "2026-11-10T00:00:00"}};
    projected = projectResources(imported, view);
    check(projected.size() == 1 && projected[0].resourceIndex == 1, "resource datetime ordered filter");
    TaskViewSpec taskView;
    taskView.classification.sortKey = "field:" + field(imported, FieldScope::Task, "작업 시작 일시").id;
    taskView.classification.descending = true;
    check(projectTaskView(imported, nullptr, taskView).rows.front() == 1, "task datetime sort");
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("dates.ppm").toStdString();
    store.save(path, imported, {});
    check(store.load(path).project == imported, "native datetime file");
    Document d(store);
    d.open(path);
    const auto key = "field:" + field(imported, FieldScope::Assignment, "배정 시작 일시").id;
    std::vector<std::string> ids;
    for (const auto &a : d.project()->assignments)
        ids.push_back(assignmentAttributeId(a));
    const auto before = *d.project();
    rejects([&] { d.setAssignmentAttribute({ids[0], "missing"}, key, "2026-11-12T01:02:03"); });
    check(*d.project() == before, "atomic datetime batch");
    auto prepared = Document::prepareEdit(
        d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, ids, key, "2026-11-12 01:02:03.120000"}),
        std::make_shared<CalculationControl>());
    check(d.applyPreparedEdit(*prepared) == EditState::Applied, "datetime worker");
    d.undo();
    check(!d.dirty() && d.project()->assignments == before.assignments, "datetime undo");
    d.redo();
    d.undo();
    auto control = std::make_shared<CalculationControl>();
    prepared = Document::prepareEdit(
        d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, ids, key, "2026-11-12T01:02:03"}), control);
    control->cancel();
    check(d.applyPreparedEdit(*prepared) == EditState::Cancelled, "datetime cancel");
    prepared = Document::prepareEdit(
        d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, ids, key, "2026-11-12T01:02:03"}),
        std::make_shared<CalculationControl>());
    d.open(path);
    check(d.applyPreparedEdit(*prepared) == EditState::Stale, "datetime stale");
    d.captureBaseline("일시 동결", "PM", "검토", "2026-11-10T07:00:00Z");
    d.saveScenario("일시 사본", "task-order", 260, "2026-11-10T07:00:00Z", d.project()->documentId,
                   d.project()->revision);
    const auto frozen = d.project()->baselines[0].input;
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-10", "날짜 값 보존"};
    fx.reporter = "PM";
    fx.reason = "통화와 무관한 날짜·시각";
    fx.recordedAt = "2026-11-10T07:10:00Z";
    fx.confirmed = true;
    d.convertCurrency(fx);
    check(d.project()->tasks[0].attributes == before.tasks[0].attributes &&
              d.project()->baselines[0].input.customFields == frozen.customFields,
          "currency transition preserves dates");
    check(json::decodeProject(json::encodeProject(*d.project()), 40) == *d.project(), "datetime transition snapshots");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-10T07:20:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "schema40 recovery");
    auto archived = *d.project();
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(archived, scope)) {
            f.type = CustomFieldType::Text;
            f.xmlLookup.reset();
            f.options.clear();
        }
    check(nativeSchemaFor(archived) == 40, "archive-only datetime schema");
    Document readonly(store);
    readonly.open(path, true);
    rejects([&] { readonly.setAssignmentAttribute(ids, key, "2026-11-12T01:02:03"); });
    const auto xer = exportPrimaveraXer(imported);
    const auto xerRead = importPrimaveraXer(xer.xer, "dates.xer").project;
    check(value(xerRead, FieldScope::Task, xerRead.tasks[0].attributes, "작업 날짜") == "2026-11-10T12:34:56.123456" &&
              value(xerRead, FieldScope::Task, xerRead.tasks[0].attributes, "작업 시작 일시") == "2026-11-10T09:30:06",
          "XER timestamp text preserves seconds");
    const auto output = QDir::currentPath() + "/xml-date-time-qa";
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
    options.reportId = "xml-date-time";
    options.generatedAt = "2026-11-10T07:00:00Z";
    const auto report = buildReport(imported, options);
    exportXlsx(report, output + "/fields.xlsx");
    check(exportPdf(report, output + "/fields.pdf") > 0, "datetime PDF");
    std::cout << "PASS XML dates: Start/Finish/Date clocks in four scopes, midnight/missing, fractional seconds, "
                 "ordinary/packed GUID lookup and shared roles, schema40/archives/recovery/FX, "
                 "sort/filter/worker/undo, XER text\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto p = importProjectXml(exportProjectXml(fixture()).xml, "ui.xml").project;
    const auto path = temp.filePath("ui.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    const auto output = QDir::currentPath() + "/xml-date-time-qa";
    QDir().mkpath(output);
    QTimer::singleShot(0, [&] {
        try {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(dialog, "datetime object form");
            auto *key = dialog->findChild<QComboBox *>("objectFieldKey");
            auto *table = dialog->findChild<QTableView *>("objectFieldValues");
            table->selectAll();
            const auto f = field(*d.project(), FieldScope::Assignment, "배정 시작 일시");
            key->setCurrentIndex(key->findData(QString::fromStdString("field:" + f.id)));
            auto *input = dialog->findChild<QLineEdit *>("objectFieldInput");
            input->setText("2026-11-12 01:02:03.120000");
            auto *apply = dialog->findChild<QPushButton *>("applyObjectField");
            apply->click();
            QElapsedTimer timer;
            timer.start();
            while (!apply->isEnabled() && timer.elapsed() < 15000)
                QTest::qWait(10);
            check(apply->isEnabled(), "datetime worker stalled");
            for (const auto &a : d.project()->assignments)
                check(attributeValue(a.attributes, "field:" + f.id) == "2026-11-12T01:02:03.12", "datetime batch UI");
            check(dialog->grab().save(output + "/assignment.png"), "datetime screenshot");
            dialog->resize(800, 600);
            QTest::qWait(10);
            check(dialog->grab().save(output + "/assignment-small.png"), "small datetime screenshot");
            d.renameTask(0, "다른 수정");
            check(!apply->isEnabled(), "datetime stale form");
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
            const auto f = field(*d.project(), FieldScope::Task, "작업 종료 일시");
            auto *key = dialog->findChild<QComboBox *>("taskAttributeKey");
            key->setCurrentIndex(key->findData(QString::fromStdString("field:" + f.id)));
            auto *input = dialog->findChild<QComboBox *>("taskAttributeValue");
            input->setEditText("2026-11-13 00:00");
            dialog->findChild<QPushButton *>("applyTaskAttribute")->click();
            check(attributeValue(d.project()->tasks[0].attributes, "field:" + f.id) == "2026-11-13T00:00:00",
                  "task midnight UI");
            check(dialog->grab().save(output + "/task-finish.png"), "finish screenshot");
            dialog->reject();
        } catch (const std::exception &e) {
            std::cerr << e.what();
            std::exit(2);
        }
    });
    editTaskAttributes(d, {d.project()->tasks[0].id}, nullptr);
    std::cout << "PASS XML datetime UI: timestamp entry/canonicalization, assignment batch worker, task Finish "
                 "midnight, 800x600, stale\n";
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
