#include "XmlRollDownFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/LedgerIdentity.h"
#include "domain/ProjectCurrency.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include "ui/ClassificationDialogs.h"
#include "ui/XmlRollDownDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
namespace {
using namespace ppm;
using namespace ppm::test;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected roll-down rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
const CustomField &field(const PlanInput &p, FieldScope scope, const std::string &name) {
    for (const auto &f : customFields(p, scope))
        if (f.name == name)
            return f;
    throw std::runtime_error("missing field");
}
std::optional<std::string> effective(const PlanInput &p, std::size_t row, const std::string &name) {
    const auto &f = field(p, FieldScope::Assignment, name);
    auto v = RollDownValues(p).value(p.assignments.at(row), "field:" + f.id);
    if (v && f.type == CustomFieldType::Choice)
        return attributeText(p, "field:" + f.id, *v, FieldScope::Assignment);
    return v;
}
void verify(const PlanInput &p) {
    check(effective(p, 0, "배정 0") == "직접" && effective(p, 1, "배정 0") == "표준" && effective(p, 2, "배정 0") == "",
          "text direct/inherit/empty");
    check(effective(p, 0, "배정 1") == "12.5" && effective(p, 1, "배정 1") == "12.5" &&
              effective(p, 2, "배정 1") == "0",
          "number 0/missing");
    check(!effective(p, 0, "배정 2") && effective(p, 1, "배정 2") == "2026-11-11T12:34:56.123456" &&
              !effective(p, 2, "배정 2"),
          "date missing override/clock");
    check(effective(p, 1, "배정 3") == "beta" && effective(p, 2, "배정 3") == "alpha", "local choice mapping");
    check(effective(p, 0, "배정 4") == "1" && effective(p, 1, "배정 4") == "0", "flag override false");
    check(effective(p, 0, "배정 5") == "0" && effective(p, 1, "배정 5") == "12.34" && effective(p, 2, "배정 5") == "-5",
          "cost same value not proration");
    check(effective(p, 1, "배정 6") == "90.5 em?" && effective(p, 2, "배정 6") == "0 m",
          "duration flags and explicit zero");
    check(effective(p, 1, "배정 7") == "2026-11-11T09:30:06" && effective(p, 1, "배정 8") == "2026-11-12T00:00:00",
          "Start Finish roles");
    check(effective(p, 1, "배정 9") == "A, B", "multi lookup");
    check(effective(p, 0, "배정 10") == "본부" && effective(p, 1, "배정 10") == "외부" &&
              effective(p, 2, "배정 10") == "본부",
          "resource inheritance");
    check(effective(p, 0, "배정 11") == "20" && effective(p, 1, "배정 11") == "0", "resource cost");
    check(effective(p, 0, "배정 12") == "2026-11-11T15:30:12" && !effective(p, 1, "배정 12"),
          "typed GUID resource lookup");
}
void contract() {
    auto p = xmlRollDownFixture();
    validate(p);
    verify(p);
    check(nativeSchemaFor(p) == 42, "schema42");
    const auto body = json::encodeProject(p);
    check(json::decodeProject(body, 42) == p, "native exact roundtrip");
    rejects([&] { json::decodeProject(body, 41); });
    auto old = p;
    for (auto scope : {FieldScope::Task, FieldScope::Resource})
        for (auto &f : customFields(old, scope))
            f.xmlRollDown.reset();
    for (auto &a : old.assignments)
        a.rollDownMissing.clear();
    check(nativeSchemaFor(old) < 42, "old minimum writer");
    auto bad = p;
    bad.customFields[0].xmlRollDown->assignmentFieldId = "unknown";
    rejects([&] { validate(bad); });
    bad = p;
    bad.customFields[0].xmlRollDown->assignmentFieldId = "target1";
    rejects([&] { validate(bad); });
    bad = p;
    bad.customFields[1].xmlRollDown->assignmentFieldId = "target0";
    rejects([&] { validate(bad); });
    bad = p;
    bad.customFields[0].xmlRollDown->assignmentFieldId.clear();
    rejects([&] { validate(bad); });
    bad = p;
    bad.assignmentFields[0].xmlRollDown = XmlRollDown{"target0", true};
    rejects([&] { validate(bad); });
    bad = p;
    bad.assignments[0].rollDownMissing.push_back("field:target0");
    rejects([&] { validate(bad); });
    auto xml = exportProjectXml(p);
    check(xml.xml.contains("<AutoRollDown>1</AutoRollDown>") &&
              xml.xml.contains("<SecondaryPID>255885312</SecondaryPID>"),
          "XML settings");
    auto imported = importProjectXml(xml.xml, "roll-down.xml").project;
    validate(imported);
    verify(imported);
    auto replay = exportProjectXml(imported);
    auto again = importProjectXml(replay.xml, "replay.xml").project;
    verify(again);
    check(imported.assignments[1].attributes.empty() && again.assignments[1].attributes.empty(),
          "derived values never materialized by XML roundtrip");
    check(imported.assignments[0].rollDownMissing.size() == 1 && again.assignments[0].rollDownMissing.size() == 1,
          "explicit XML missing preserved");
    auto invalid = xml.xml;
    invalid.replace("<AutoRollDown>1</AutoRollDown>", "<AutoRollDown>2</AutoRollDown>");
    rejects([&] { importProjectXml(invalid, "bad.xml"); });
    invalid = xml.xml;
    invalid.replace("<SecondaryPID>255885312</SecondaryPID>", "<SecondaryPID>188743731</SecondaryPID>");
    rejects([&] { importProjectXml(invalid, "bad-pid.xml"); });
    auto open = xml.xml;
    auto offset = open.indexOf("<Value>beta</Value>", open.indexOf("<Tasks>"));
    check(offset >= 0, "open mutation target");
    open.replace(offset, QByteArray("<Value>beta</Value>").size(), "<Value>외부 관측</Value>");
    auto opened = importProjectXml(open, "open.xml").project;
    check(effective(opened, 1, "배정 3") == "외부 관측", "open lookup receiver extended");
    check(effective(importProjectXml(exportProjectXml(opened).xml, "open-replay.xml").project, 1, "배정 3") ==
              "외부 관측",
          "open lookup replay");
    // A different wire ValueID still maps by typed value, while a shared GUID maps by GUID.
    check(field(imported, FieldScope::Task, "작업 3").xmlLookup->values[0].optionId !=
              field(imported, FieldScope::Assignment, "배정 3").xmlLookup->values[0].optionId,
          "local IDs remapped independently");
    QTemporaryDir temp;
    JsonProjectStore store;
    auto path = temp.filePath("roll.ppm").toStdString();
    store.save(path, p, {});
    check(store.load(path).project == p, "file schema42");
    Document d(store);
    d.open(path);
    d.setTaskAttribute({"T"}, "field:source1", "13.5");
    check(effective(*d.project(), 0, "배정 1") == "12.5" && effective(*d.project(), 1, "배정 1") == "13.5",
          "manual equal values stay manual");
    d.undo();
    verify(*d.project());
    d.redo();
    d.undo();
    d.setResourceAttribute({"R"}, "field:source10", "자원 변경");
    check(effective(*d.project(), 0, "배정 10") == "자원 변경" &&
              effective(*d.project(), 2, "배정 10") == "자원 변경" && effective(*d.project(), 1, "배정 10") == "외부",
          "resource edit fans out");
    d.undo();
    const auto a0 = assignmentAttributeId(d.project()->assignments[0]),
               a1 = assignmentAttributeId(d.project()->assignments[1]);
    d.setAssignmentAttribute({a1}, "field:target0", "");
    d.setTaskAttribute({"T"}, "field:source0", "새 원본");
    check(effective(*d.project(), 1, "배정 0") == "", "manual empty wins");
    d.setAssignmentAttribute({a1}, "field:target0", {});
    check(effective(*d.project(), 1, "배정 0") == "새 원본", "clear resumes inheritance");
    d.undo();
    d.undo();
    d.undo();
    verify(*d.project());
    d.setAssignmentAttribute({a0}, "field:target2", {});
    check(effective(*d.project(), 0, "배정 2") == "2026-11-11T12:34:56.123456", "explicit missing reset");
    d.undo();
    verify(*d.project());
    auto before = *d.project();
    rejects([&] { d.setXmlRollDown(FieldScope::Task, "field:source0", XmlRollDown{"target1", true}); });
    check(*d.project() == before, "atomic bad binding");
    rejects([&] { d.removeCustomField("target10", FieldScope::Assignment); });
    d.setXmlRollDown(FieldScope::Task, "field:source0", XmlRollDown{"target0", false});
    check(!effective(*d.project(), 1, "배정 0") && effective(*d.project(), 0, "배정 0") == "직접",
          "disable keeps manual");
    d.undo();
    verify(*d.project());
    auto assignments = d.project()->assignments;
    d.setAssignments("T", {{"T", "R", 50}, {"T", "S", 50}});
    check(d.project()->assignments.back().taskId == "T" &&
              std::any_of(
                  d.project()->assignments.begin(), d.project()->assignments.end(),
                  [](const auto &a) { return a.resourceId == "R" && a.taskId == "T" && !a.rollDownMissing.empty(); }),
          "rate edit keeps explicit missing");
    d.undo();
    check(d.project()->assignments == assignments, "rate undo");
    d.setAssignments("B", {{"B", "R", 100}, {"B", "S", 50}});
    check(RollDownValues(*d.project()).value(d.project()->assignments.back(), "field:target10") == "외부" &&
              !attributeValue(d.project()->assignments.back().attributes, "field:target10"),
          "new assignment derives");
    d.undo();
    auto prepared = Document::prepareEdit(
        d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, {a0, a1}, "field:target0", "worker"}),
        std::make_shared<CalculationControl>());
    check(d.applyPreparedEdit(*prepared) == EditState::Applied, "worker overrides");
    d.undo();
    verify(*d.project());
    auto control = std::make_shared<CalculationControl>();
    prepared = Document::prepareEdit(
        d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, {a1}, "field:target0", "cancel"}), control);
    control->cancel();
    check(d.applyPreparedEdit(*prepared) == EditState::Cancelled, "worker cancel");
    prepared = Document::prepareEdit(
        d.makeEditRequest(ObjectAttributeEdit{FieldScope::Assignment, {a1}, "field:target0", "stale"}),
        std::make_shared<CalculationControl>());
    d.renameTask(0, "변경");
    check(d.applyPreparedEdit(*prepared) == EditState::Stale, "worker stale");
    d.undo();
    d.captureBaseline("roll-down 동결", "PM", "배정 근거", "2026-11-11T02:00:00Z");
    d.saveScenario("배정 사본", "task-order", 260, "2026-11-11T02:01:00Z", d.project()->documentId,
                   d.project()->revision);
    const auto frozen = d.project()->baselines[0].input;
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-11", "1/2 환산"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 2;
    fx.reporter = "PM";
    fx.reason = "한 번만 환산";
    fx.recordedAt = "2026-11-11T02:02:00Z";
    fx.confirmed = true;
    d.convertCurrency(fx);
    check(effective(*d.project(), 1, "배정 5") == "6.17" && effective(*d.project(), 0, "배정 5") == "0" &&
              !attributeValue(d.project()->assignments[1].attributes, "field:target5"),
          "FX inherited cost converts once");
    check(d.project()->baselines[0].input.customFields[0].xmlRollDown == frozen.customFields[0].xmlRollDown,
          "FX binding frozen");
    check(json::decodeProject(json::encodeProject(*d.project()), 42) == *d.project(), "snapshots schema42");
    auto archive = *d.project();
    for (auto scope : {FieldScope::Task, FieldScope::Resource})
        for (auto &f : customFields(archive, scope))
            f.xmlRollDown.reset();
    for (auto &a : archive.assignments)
        a.rollDownMissing.clear();
    check(nativeSchemaFor(archive) == 42, "archive-only rules");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-11T02:03:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "recovery42");
    Document readonly(store);
    readonly.open(path, true);
    rejects([&] { readonly.setXmlRollDown(FieldScope::Task, "field:source0", {}); });
    rejects([&] { readonly.setAssignmentAttribute({a1}, "field:target0", "no"); });
    auto created = p;
    created.customFields.push_back({"plain", "로컬 추가", CustomFieldType::Text, {}, {}});
    const auto other = temp.filePath("create.ppm").toStdString();
    store.save(other, created, {});
    Document create(store);
    create.open(other);
    auto id = create.createRollDownField(FieldScope::Task, "field:plain", "새 배정", true);
    create.setTaskAttribute({"T"}, "field:plain", "생성");
    check(RollDownValues(*create.project()).value(create.project()->assignments[0], "field:" + id) == "생성",
          "new receiver definition");
    create.undo();
    create.undo();
    check(create.project()->assignmentFields.size() == p.assignmentFields.size(), "create receiver undo");
    auto observed = p;
    observed.costEntries = {{"fact",
                             "T",
                             "2026-11-11 09:00",
                             "2026-11-11T02:00:00Z",
                             "PM",
                             "원본 ID 이관",
                             {},
                             p.documentId,
                             p.currency,
                             1,
                             100,
                             {}}};
    auto migrated = migrateLedgerIdentities(observed, {{"task", "T", "NEW"}});
    validate(migrated);
    check(effective(migrated, 1, "배정 0") == "표준" &&
              migrated.assignments[0].rollDownMissing == p.assignments[0].rollDownMissing,
          "ID migration preserves inheritance and missing override");
    const auto out = QDir::currentPath() + "/xml-roll-down-qa";
    QDir().mkpath(out);
    write(out + "/export.xml", xml.xml);
    write(out + "/reexport.xml", replay.xml);
    write(out + "/native.json", QJsonDocument(body).toJson());
    write(out + "/mapping.json", QJsonDocument(xml.report).toJson());
    write(out + "/converted.json", QJsonDocument(json::encodeProject(*d.project())).toJson());
    auto xer = exportPrimaveraXer(p);
    write(out + "/export.xer", xer.xer);
    auto xerRead = importPrimaveraXer(xer.xer, "static.xer").project;
    check(!xerRead.assignmentFields.empty(), "XER effective values exported");
    for (const auto *name : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(out + '/' + name);
    store.save((out + "/source.ppm").toStdString(), p, {});
    ReportOptions options;
    options.reportId = "xml-roll-down";
    options.generatedAt = "2026-11-11T02:04:00Z";
    auto report = buildReport(p, options);
    exportXlsx(report, out + "/fields.xlsx");
    check(exportPdf(report, out + "/fields.pdf") > 0, "PDF");
    std::cout
        << "PASS XML roll-down: task/resource to assignment, all scalar/lookup/multi types, direct/empty/zero/missing "
           "priority, live XML inheritance, worker/undo, FX/archives/recovery/schema42, XER/reports\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    auto p = xmlRollDownFixture();
    auto path = temp.filePath("ui.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    const auto out = QDir::currentPath() + "/xml-roll-down-qa";
    QDir().mkpath(out);
    auto run = [&](std::function<void(QDialog *)> f) {
        QTimer::singleShot(0, [&, f] {
            try {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                check(dialog, "modal");
                f(dialog);
            } catch (const std::exception &e) {
                std::cerr << e.what();
                std::exit(2);
            }
        });
    };
    run([&](QDialog *dialog) {
        dialog->findChild<QPushButton *>("previewRollDown")->click();
        check(dialog->findChild<QTableWidget *>("rollDownPreview")->rowCount() == 3, "preview rows");
        check(dialog->findChild<QLabel *>("rollDownSummary")->text().contains("직접 값 1"), "preview manual count");
        dialog->resize(800, 600);
        QApplication::processEvents();
        check(dialog->grab().save(out + "/settings-small.png"), "settings screenshot");
        dialog->findChild<QCheckBox *>("rollDownAutomatic")->setChecked(false);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    editXmlRollDown(d, FieldScope::Task, "field:source0", nullptr);
    check(!effective(*d.project(), 1, "배정 0"), "UI disables inheritance");
    d.undo();
    run([&](QDialog *dialog) {
        auto *key = dialog->findChild<QComboBox *>("objectFieldKey");
        key->setCurrentIndex(key->findData("field:target0"));
        auto *table = dialog->findChild<QTableView *>("objectFieldValues");
        table->selectRow(0);
        check(table->model()->data(table->model()->index(0, 2), Qt::ToolTipRole).toString().contains("직접 입력"),
              "manual origin");
        auto *clear = dialog->findChild<QPushButton *>("clearObjectField");
        check(clear && clear->text() == "원본 값 사용", "restore action");
        clear->click();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10000 && effective(*d.project(), 0, "배정 0") != "표준")
            QTest::qWait(10);
        check(effective(*d.project(), 0, "배정 0") == "표준", "UI restore worker");
        dialog->resize(800, 600);
        QApplication::processEvents();
        check(dialog->grab().save(out + "/assignment-small.png"), "assignment screenshot");
        dialog->reject();
    });
    editObjectAttributes(d, FieldScope::Assignment, nullptr);
    run([&](QDialog *dialog) {
        d.renameTask(0, "문서 변경");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        check(!dialog->findChild<QLabel *>("rollDownError")->text().isEmpty(), "stale blocked");
        dialog->reject();
    });
    editXmlRollDown(d, FieldScope::Task, "field:source0", nullptr);
    std::cout << "PASS roll-down UI: preview/direct origin, disable/undo, inheritance restore worker, stale, 800x600\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    check(font >= 0, "font");
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
