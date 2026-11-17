#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include "ui/ClassificationDialogs.h"
#include "ui/ResourceViewDialog.h"
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QTableWidget>
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
#include <QListWidget>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
namespace {
using namespace jobs;
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
    throw std::runtime_error("expected lookup rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write evidence");
}
std::string guid(int n) {
    return "11111111-2222-4333-8444-" + QString::number(n).rightJustified(12, '0').toStdString();
}
CustomField lookup(std::string id, std::string name, CustomFieldType type, int base, bool enterprise = false,
                   int maximum = 1) {
    CustomField f{id, name, CustomFieldType::Choice, {}, {{"one", "첫 항목"}, {"two", "둘째 항목"}}};
    XmlLookup l;
    l.valueType = type;
    l.maxValues = maximum;
    if (enterprise) {
        l.tableGuid = guid(base);
        l.tableName = name;
    }
    const std::vector<std::string> raw = type == CustomFieldType::Number ? std::vector<std::string>{"0", "-2.123456"}
                                         : type == CustomFieldType::Date
                                             ? std::vector<std::string>{"2026-11-10", "2026-11-11"}
                                             : std::vector<std::string>{"서울 & <설계>", "부산"};
    for (int i = 0; i < 2; ++i)
        l.values.push_back({i ? "two" : "one",
                            std::to_string(base + i + 1),
                            enterprise ? guid(base + i + 1) : "",
                            raw[i],
                            i ? "둘째 설명" : "첫 설명",
                            {},
                            true});
    l.defaultOptionId = "two";
    f.xmlLookup = l;
    return f;
}
Project fixture() {
    Project p;
    p.documentId = "xml-lookup";
    p.name = "목록·GUID 참조";
    p.startDate = "2026-11-10";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "설계", 480, {}}, {"B", "검토", 480, {}}};
    p.taskCosts = {{"T", 0}, {"B", 0}};
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    p.resources = {r};
    p.assignments = {{"T", "R", 100}, {"B", "R", 100}};
    p.customFields = {lookup("local", "지역", CustomFieldType::Text, 10),
                      lookup("multi", "기업 다중 지역", CustomFieldType::Text, 20, true, 2)};
    auto &multi = p.customFields[1];
    multi.xmlIdentity = XmlFieldIdentity{"188776449", guid(100), "Enterprise regions"};
    multi.xmlLookup->fieldGuid = guid(100);
    multi.xmlLookup->fieldName = "Enterprise regions";
    multi.options.insert(multi.options.begin(), {"root", "국내"});
    multi.xmlLookup->values.insert(multi.xmlLookup->values.begin(),
                                   {"root", "20", guid(23), "국내", "상위 계층", {}, true});
    multi.xmlLookup->leafOnly = true;
    multi.xmlLookup->values[1].parentId = "root";
    multi.xmlLookup->values[2].parentId = "root";
    p.tasks[0].attributes = {{"field:local", "one"}, {"field:multi", choiceValue(multi, {"two", "one"})}};
    p.tasks[1].attributes = {{"field:multi", choiceValue(multi, {"two"})}};
    p.projectFields = {lookup("number", "프로젝트 등급", CustomFieldType::Number, 30, true)};
    p.projectAttributes = {{"field:number", "one"}};
    p.resourceFields = {lookup("date", "자원 검토일", CustomFieldType::Date, 40),
                        lookup("xml-udf-RSRC-205521174", "자원 계층", CustomFieldType::Text, 50, true)};
    p.resources[0].attributes = {{"field:date", "two"}, {"field:xml-udf-RSRC-205521174", "one"}};
    auto shared = multi;
    shared.id = "shared";
    shared.name = "배정 다중 지역";
    shared.xmlIdentity.reset();
    shared.xmlLookup->fieldGuid.clear();
    shared.xmlLookup->fieldName.clear();
    auto open = lookup("open", "배정 열린 목록", CustomFieldType::Text, 60, true);
    open.xmlLookup->restrictValues = false;
    open.options.push_back({"free", "직접 입력"});
    open.xmlLookup->values.push_back({"free", {}, {}, "목록 밖 직접 입력", {}, {}, false});
    p.assignmentFields = {shared, open};
    p.assignments[0].attributes = {{"field:shared", choiceValue(shared, {"one", "two"})}, {"field:open", "free"}};
    p.assignments[1].attributes = {{"field:shared", choiceValue(shared, {"two"})}};
    return p;
}
const CustomField &field(const Project &p, FieldScope scope, const std::string &name) {
    for (const auto &f : customFields(p, scope))
        if (f.name == name)
            return f;
    throw std::runtime_error("missing field");
}
std::string display(const Project &p, FieldScope scope, const std::vector<TaskAttribute> &v, const std::string &name) {
    const auto &f = field(p, scope, name);
    auto raw = attributeValue(v, "field:" + f.id);
    return raw ? attributeText(p, "field:" + f.id, *raw, scope) : "MISSING";
}
void verify(const Project &p) {
    check(display(p, FieldScope::Task, p.tasks[0].attributes, "지역") == "서울 & <설계>", "local list selection");
    check(display(p, FieldScope::Task, p.tasks[1].attributes, "지역") == "MISSING", "default became explicit value");
    const auto f = field(p, FieldScope::Task, "기업 다중 지역");
    auto selected = choiceSelection(f, *attributeValue(p.tasks[0].attributes, "field:" + f.id));
    check(selected.size() == 2 && f.xmlLookup->maxValues == 2 && f.xmlLookup->leafOnly &&
              f.xmlLookup->tableGuid == guid(20),
          "multi hierarchy identity");
    check(f.xmlLookup->defaultOptionId == "xml-choice-" + guid(22), "default identity");
    check(display(p, FieldScope::Task, p.tasks[0].attributes, "기업 다중 지역").find("국내 › ") != std::string::npos,
          "hierarchy labels");
    check(display(p, FieldScope::Project, p.projectAttributes, "프로젝트 등급") == "0", "numeric zero");
    check(display(p, FieldScope::Resource, p.resources[0].attributes, "자원 검토일") == "2026-11-11", "date list");
    check(display(p, FieldScope::Resource, p.resources[0].attributes, "자원 계층") == "서울 & <설계>",
          "outline selection");
    check(display(p, FieldScope::Assignment, p.assignments[0].attributes, "배정 열린 목록") == "목록 밖 직접 입력",
          "unlisted literal");
    check(display(p, FieldScope::Assignment, p.assignments[1].attributes, "배정 열린 목록") == "MISSING",
          "missing assignment");
    check(choiceSelection(field(p, FieldScope::Assignment, "배정 다중 지역"), p.assignments[0].attributes[0].value)
                  .size() == 2,
          "assignment multi");
}
void contract() {
    const auto original = fixture();
    validate(original);
    const auto xml = exportProjectXml(original);
    const auto imported = importProjectXml(xml.xml, "lookup.xml");
    verify(imported.project);
    const auto replay = exportProjectXml(imported.project);
    const auto restored = importProjectXml(replay.xml, "lookup-again.xml");
    verify(restored.project);
    check(restored.project.customFields == imported.project.customFields &&
              restored.project.assignmentFields == imported.project.assignmentFields,
          "lookup dictionary roundtrip");
    check(original == fixture(), "export mutation");
    auto bad = [&](QByteArray from, QByteArray to) {
        auto bytes = xml.xml;
        check(bytes.contains(from), "mutation missing");
        bytes.replace(from, to);
        rejects([&] { importProjectXml(bytes, "bad.xml"); });
    };
    bad("<FieldValueGUID>" + QByteArray::fromStdString(guid(21)) + "</FieldValueGUID>",
        "<FieldValueGUID>" + QByteArray::fromStdString(guid(31)) + "</FieldValueGUID>");
    bad("<Ltuid>" + QByteArray::fromStdString(guid(20)) + "</Ltuid>",
        "<Ltuid>" + QByteArray::fromStdString(guid(999)) + "</Ltuid>");
    bad("<ParentValueID>20</ParentValueID>", "<ParentValueID>21</ParentValueID>");
    bad("<MaxMultiValues>2</MaxMultiValues>", "<MaxMultiValues>1</MaxMultiValues>");
    bad("<ValueID>22</ValueID>", "<ValueID>21</ValueID>");
    bad("<DefaultGuid>" + QByteArray::fromStdString(guid(22)) + "</DefaultGuid>",
        "<DefaultGuid>" + QByteArray::fromStdString(guid(31)) + "</DefaultGuid>");
    bad("<Value>-2.123456</Value>", "<Value>1e3</Value>");
    bad("2026-11-11T00:00:00", "2026-11-11T24:00:00");
    bad("<Value>목록 밖 직접 입력</Value>",
        "<ValueGUID>" + QByteArray::fromStdString(guid(61)) + "</ValueGUID><Value>목록 밖 직접 입력</Value>");
    auto direct = xml.xml;
    const auto selectedAt = direct.indexOf("<Value>서울 &amp; &lt;설계&gt;</Value>", direct.indexOf("<Tasks>"));
    check(selectedAt >= 0, "local selection mutation");
    direct.replace(selectedAt, QByteArray("<Value>서울 &amp; &lt;설계&gt;</Value>").size(), "<Value>직접 대체</Value>");
    rejects([&] { importProjectXml(direct, "closed.xml"); });
    auto compatible = xml.xml;
    const auto selectionStart =
        compatible.indexOf("<Value>서울 &amp; &lt;설계&gt;</Value>", compatible.indexOf("<Tasks>"));
    compatible.replace(selectionStart, QByteArray("<Value>서울 &amp; &lt;설계&gt;</Value>").size(),
                       "<ValueID>11</ValueID>");
    verify(importProjectXml(compatible, "value-id.xml").project);
    compatible = xml.xml;
    const auto outlineStart = compatible.indexOf("<ValueID>51</ValueID>", compatible.indexOf("<Resources>"));
    check(outlineStart >= 0, "outline reference fixture");
    compatible.replace(outlineStart, QByteArray("<ValueID>51</ValueID>").size(), "<ValueID>31</ValueID>");
    rejects([&] { importProjectXml(compatible, "foreign-id.xml"); });
    auto small = original;
    small.customFields = {{"empty-choice", "빈 선택 사전", CustomFieldType::Choice, {}, {}}};
    for (auto &t : small.tasks)
        t.attributes.clear();
    const auto emptyLookup = importProjectXml(exportProjectXml(small).xml, "empty-list.xml").project;
    check(emptyLookup.customFields.size() == 1 && emptyLookup.customFields[0].type == CustomFieldType::Choice &&
              emptyLookup.customFields[0].options.empty(),
          "empty choice dictionary");
    small = original;
    auto &empty = small.customFields[0];
    empty.options[0].name = "빈 텍스트";
    empty.xmlLookup->values[0].value = "";
    auto emptyText = importProjectXml(exportProjectXml(small).xml, "empty-value.xml").project;
    check(display(emptyText, FieldScope::Task, emptyText.tasks[0].attributes, "지역") == "(빈 텍스트)",
          "explicit empty list value");
    small = original;
    auto &openMulti = small.assignmentFields[1];
    openMulti.xmlLookup->maxValues = 0;
    small.assignments[0].attributes[1].value = choiceValue(openMulti, {"free", "one"});
    const auto mixed = importProjectXml(exportProjectXml(small).xml, "open-mixed.xml").project;
    const auto mixedField = field(mixed, FieldScope::Assignment, "배정 열린 목록");
    check(choiceSelection(mixedField, *attributeValue(mixed.assignments[0].attributes, "field:" + mixedField.id))
                  .size() == 2,
          "mixed listed and direct multi values");
    small = original;
    auto &large = small.assignmentFields[1];
    large.options.clear();
    large.xmlLookup->values.clear();
    large.xmlLookup->maxValues = 0;
    large.xmlLookup->defaultOptionId.clear();
    std::vector<std::string> selectedMany;
    for (int i = 0; i < 451; ++i) {
        auto id = "option-" + std::to_string(i);
        selectedMany.push_back(id);
        large.options.push_back({id, id});
        large.xmlLookup->values.push_back({id, std::to_string(1000 + i), guid(1000 + i), id, {}, {}, true});
    }
    small.assignments[0].attributes[1].value = choiceValue(large, selectedMany);
    validate(small);
    rejects([&] { exportProjectXml(small); });
    selectedMany.resize(448);
    small.assignments[0].attributes[1].value = choiceValue(large, selectedMany);
    const auto boundary = importProjectXml(exportProjectXml(small).xml, "enterprise-boundary.xml").project;
    const auto boundaryField = field(boundary, FieldScope::Assignment, "배정 열린 목록");
    check(
        choiceSelection(boundaryField, *attributeValue(boundary.assignments[0].attributes, "field:" + boundaryField.id))
                .size() == 448,
        "enterprise element limit boundary");
    const auto &multi = original.customFields[1];
    rejects([&] { choiceValue(multi, {"root"}); });
    rejects([&] { choiceValue(multi, {"one", "one"}); });
    rejects([&] { choiceValue(multi, {}); });
    rejects([&] { choiceSelection(multi, "@choices:999:one"); });
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("lookup.ppm").toStdString();
    store.save(path, imported.project, {});
    check(nativeSchemaFor(imported.project) == 38 && store.load(path).project == imported.project, "native schema38");
    const auto body = json::encodeProject(imported.project);
    check(json::decodeProject(body, 38) == imported.project, "native lookup metadata");
    rejects([&] { json::decodeProject(body, 37); });
    auto invalid = imported.project;
    invalid.customFields[0].xmlLookup->values[0].optionId = "foreign";
    rejects([&] { validate(invalid); });
    invalid = imported.project;
    invalid.customFields[1].xmlLookup->values[1].parentId = invalid.customFields[1].xmlLookup->values[1].optionId;
    rejects([&] { validate(invalid); });
    Document d(store);
    d.open(path);
    const auto af = field(*d.project(), FieldScope::Assignment, "배정 다중 지역");
    const auto key = "field:" + af.id;
    const auto one = choiceValue(af, {af.xmlLookup->values[1].optionId});
    const auto before = *d.project();
    const std::vector<std::string> ids{assignmentAttributeId(before.assignments[0]),
                                       assignmentAttributeId(before.assignments[1])};
    rejects([&] { d.setAssignmentAttribute({ids[0], "missing"}, key, one); });
    check(*d.project() == before, "atomic batch");
    auto prepared = Document::prepareEdit(d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, ids, key, one}),
                                          std::make_shared<CalculationControl>());
    check(d.applyPreparedEdit(*prepared) == EditState::Applied, "prepared multi edit");
    d.undo();
    check(!d.dirty() && d.project()->assignments == before.assignments, "undo savepoint");
    d.redo();
    d.undo();
    auto changed = af;
    changed.xmlLookup->maxValues = 1;
    rejects([&] { d.putCustomField(changed, FieldScope::Assignment); });
    changed = af;
    changed.name = "새 표시 별칭";
    d.putCustomField(changed, FieldScope::Assignment);
    d.undo();
    auto control = std::make_shared<CalculationControl>();
    prepared =
        Document::prepareEdit(d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, ids, key, one}), control);
    control->cancel();
    check(d.applyPreparedEdit(*prepared) == EditState::Cancelled, "cancel multi edit");
    prepared = Document::prepareEdit(d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, ids, key, one}),
                                     std::make_shared<CalculationControl>());
    d.open(path);
    check(d.applyPreparedEdit(*prepared) == EditState::Stale, "stale multi edit");
    d.captureBaseline("목록 동결", "PM", "검토", "2026-11-10T03:00:00Z");
    d.saveScenario("목록 사본", "task-order", 260, "2026-11-10T03:00:00Z", d.project()->documentId,
                   d.project()->revision);
    const auto frozen = d.project()->baselines[0].input;
    d.setAssignmentAttribute(ids, key, one);
    check(d.project()->baselines[0].input == frozen, "frozen lookup");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-10T03:00:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "schema38 recovery");
    auto archived = *d.project();
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(archived, scope))
            f.xmlLookup.reset();
    // Current scalar Choice values remain valid; the archive alone still requires schema38.
    check(nativeSchemaFor(archived) == 38, "archive-only lookup schema");
    check(json::decodeProject(json::encodeProject(*d.project()), 38) == *d.project(), "frozen schema38 roundtrip");
    CurrencyConversionSubmission fx;
    fx.rate = {"KRW", "USD", "2026-11-10", "목록 보존 검증"};
    fx.reporter = "PM";
    fx.reason = "목록·동결 사본 보존";
    fx.recordedAt = "2026-11-10T03:10:00Z";
    fx.confirmed = true;
    d.convertCurrency(fx);
    check(nativeSchemaFor(*d.project()) == 38 &&
              json::decodeProject(json::encodeProject(*d.project()), 38) == *d.project(),
          "currency transition lookup snapshots");
    Document readonly(store);
    readonly.open(path, true);
    rejects([&] { readonly.setAssignmentAttribute(ids, key, one); });
    auto xer = exportPrimaveraXer(imported.project);
    const auto xerRead = importPrimaveraXer(xer.xer, "lookup.xer").project;
    check(
        display(xerRead, FieldScope::Assignment, xerRead.assignments[0].attributes, "배정 다중 지역").find("국내 › ") !=
            std::string::npos,
        "XER label projection");
    const auto output = QDir::currentPath() + "/xml-lookup-qa";
    QDir().mkpath(output);
    write(output + "/export.xml", xml.xml);
    write(output + "/reexport.xml", replay.xml);
    write(output + "/mapping.json", QJsonDocument(xml.report).toJson());
    write(output + "/native.json", QJsonDocument(body).toJson());
    write(output + "/export.xer", xer.xer);
    for (const auto *name : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(output + '/' + name);
    store.save((output + "/source.ppm").toStdString(), imported.project, {});
    ReportOptions options;
    options.reportId = "xml-lookup";
    options.generatedAt = "2026-11-10T03:00:00Z";
    const auto report = buildReport(imported.project, options);
    exportXlsx(report, output + "/fields.xlsx");
    exportPdf(report, output + "/fields.pdf");
    std::cout << "PASS XML lookup: four scopes, typed lists/GUID/hierarchy/default/multi/open values, malformed "
                 "references, native38/archives/recovery/undo, XER/report projection\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    auto p = importProjectXml(exportProjectXml(fixture()).xml, "ui.xml").project;
    const auto path = temp.filePath("ui.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    const auto output = QDir::currentPath() + "/xml-lookup-qa";
    QDir().mkpath(output);
    QTimer::singleShot(0, [&] {
        try {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(dialog, "object dialog");
            auto *key = dialog->findChild<QComboBox *>("objectFieldKey");
            const auto f = field(*d.project(), FieldScope::Assignment, "배정 다중 지역");
            key->setCurrentIndex(key->findData(QString::fromStdString("field:" + f.id)));
            auto *table = dialog->findChild<QTableView *>("objectFieldValues");
            table->selectAll();
            auto *choices = dialog->findChild<QListWidget *>("objectFieldChoices");
            check(choices && choices->isVisible() && choices->count() == 3, "multi selector");
            choices->clearSelection();
            choices->item(2)->setSelected(true);
            auto *apply = dialog->findChild<QPushButton *>("applyObjectField");
            apply->click();
            QElapsedTimer timer;
            timer.start();
            while (!apply->isEnabled() && timer.elapsed() < 15000)
                QTest::qWait(10);
            check(apply->isEnabled(), "multi worker stalled");
            for (const auto &a : d.project()->assignments)
                check(choiceSelection(f, *attributeValue(a.attributes, "field:" + f.id)).size() == 1, "multi UI apply");
            check(dialog->grab().save(output + "/assignment.png"), "screenshot");
            dialog->resize(800, 600);
            QTest::qWait(15);
            check(dialog->grab().save(output + "/assignment-small.png"), "small screenshot");
            d.renameTask(0, "다른 수정");
            check(!apply->isEnabled(), "stale form");
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
            const auto f = field(*d.project(), FieldScope::Task, "기업 다중 지역");
            auto *key = dialog->findChild<QComboBox *>("taskAttributeKey");
            key->setCurrentIndex(key->findData(QString::fromStdString("field:" + f.id)));
            auto *choices = dialog->findChild<QListWidget *>("taskAttributeChoices");
            check(choices && choices->isVisible(), "task multi selector");
            choices->clearSelection();
            choices->item(1)->setSelected(true);
            dialog->findChild<QPushButton *>("applyTaskAttribute")->click();
            check(choiceSelection(f, *attributeValue(d.project()->tasks[0].attributes, "field:" + f.id)).size() == 1,
                  "task selection apply");
            check(dialog->grab().save(output + "/task.png"), "task screenshot");
            dialog->reject();
        } catch (const std::exception &e) {
            std::cerr << e.what();
            std::exit(2);
        }
    });
    editTaskAttributes(d, {d.project()->tasks[0].id}, nullptr);
    const auto taskField = field(*d.project(), FieldScope::Task, "기업 다중 지역");
    TaskViewSettings taskSettings;
    taskSettings.attributeFilters = {{"field:" + taskField.id, "equals",
                                      choiceValue(taskField, {taskField.options[1].id, taskField.options[2].id})}};
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        auto *filters = dialog->findChild<QTableWidget *>("viewAttributeFilters");
        check(static_cast<QComboBox *>(filters->cellWidget(0, 2))->currentData().toString().toStdString() ==
                  taskSettings.attributeFilters[0].value,
              "task multi filter restored");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    const auto taskRestored = editTaskViewSettings(*d.project(), taskSettings, {}, nullptr);
    check(taskRestored && taskRestored->attributeFilters == taskSettings.attributeFilters, "task multi filter save");
    auto resourceMulti = field(*d.project(), FieldScope::Assignment, "배정 다중 지역");
    resourceMulti.id = "resource-multi";
    resourceMulti.name = "자원 다중 지역";
    resourceMulti.xmlIdentity.reset();
    resourceMulti.xmlLookup->fieldGuid.clear();
    d.putCustomField(resourceMulti, FieldScope::Resource);
    const auto resourceValue = choiceValue(resourceMulti, {resourceMulti.options[1].id, resourceMulti.options[2].id});
    d.setResourceAttribute({d.project()->resources[0].id}, "field:resource-multi", resourceValue);
    ResourceViewSettings settings;
    settings.filters = {{"field:resource-multi", "equals", resourceValue}};
    const auto savedId = d.putSavedResourceView({{}, "다중 자원 보기", settings});
    {
        ResourceViewDialog view(d);
        view.show();
        auto *saved = view.findChild<QComboBox *>("savedResourceViews");
        saved->setCurrentIndex(saved->findData(QString::fromStdString(savedId)));
        auto *save = view.findChild<QPushButton *>("saveResourceView");
        save->click();
        check(d.project()->savedResourceViews[0].settings.filters == settings.filters,
              "resource multi filter retained");
        auto *filters = view.findChild<QTableWidget *>("resourceViewFilters");
        static_cast<QLineEdit *>(filters->cellWidget(0, 2))
            ->setText(QString::fromStdString(resourceMulti.options[1].name));
        save->click();
        check(d.project()->savedResourceViews[0].settings.filters[0].value ==
                  choiceValue(resourceMulti, {resourceMulti.options[1].id}),
              "resource single member filter canonical");
        view.reject();
    }
    std::cout << "PASS XML lookup UI: task/assignment multi selection, batch worker, saved multi filters, small "
                 "layout, stale protection\n";
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
