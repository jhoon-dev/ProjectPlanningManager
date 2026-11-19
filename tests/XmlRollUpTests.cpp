#include "XmlRollUpFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "calculation/ResourceViewReport.h"
#include "domain/CalculationControl.h"
#include "domain/FormulaFields.h"
#include "domain/LedgerIdentity.h"
#include "domain/ProjectCurrency.h"
#include "domain/ResourceView.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include "ui/TaskViewProjection.h"
#include "ui/XmlRollUpDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
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
    throw std::runtime_error("expected roll-up rejection");
}
void write(const QString &path, const QByteArray &v) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(v) == v.size(), "artifact write");
}
std::optional<std::string> value(const PlanInput &p, const std::string &id, const std::string &field) {
    return RollUpValues(p).cell(id, "field:" + field).value;
}
void set(Project &p, std::size_t i, const std::string &key, const std::optional<std::string> &v) {
    std::erase_if(p.tasks[i].attributes, [&](const auto &a) { return a.key == "field:" + key; });
    if (v)
        p.tasks[i].attributes.push_back({"field:" + key, *v});
}
const CustomField &named(const PlanInput &p, const std::string &name) {
    for (const auto &f : p.customFields)
        if (f.name == name)
            return f;
    throw std::runtime_error("missing field");
}
void verify(const PlanInput &p) {
    const std::vector<std::string> root{"16", "2", "6", "30", "7.5", "10", "2", "4"},
        group{"8", "2", "4", "14", "4.666667", "4", "2", "3"};
    for (int m = 0; m < 8; ++m) {
        check(value(p, "", "n" + std::to_string(m)) == root[std::size_t(m)], "all eight root methods");
        check(value(p, "G", "n" + std::to_string(m)) == group[std::size_t(m)], "nested methods/weights");
    }
    check(value(p, "", "cost") == "4.04" && value(p, "", "duration") == "6 em?", "cost/duration precision");
    check(value(p, "", "date") == "2026-11-14" && value(p, "", "start") == "2026-11-11T12:34:56" &&
              value(p, "", "finish") == "2026-11-14T12:34:56",
          "date extrema");
    check(value(p, "", "or") == "1" && value(p, "", "and") == "0", "OR/AND");
    check(value(p, "", "snapshot") == "루트 메모", "explicit snapshot");
    check(RollUpValues(p).describe("").find("루트 메모") != std::string::npos, "WBS tooltip includes explicit summary");
}
void contract() {
    auto p = xmlRollUpFixture();
    validate(p);
    verify(p);
    check(nativeSchemaFor(p) == 43, "schema43");
    const auto body = json::encodeProject(p);
    check(json::decodeProject(body, 43) == p, "native exact snapshots");
    rejects([&] { json::decodeProject(body, 42); });
    auto old = p;
    old.taskSummaryAttributes.clear();
    for (auto &g : old.wbsNodes)
        g.attributes.clear();
    for (auto scope : {FieldScope::Task, FieldScope::Resource})
        for (auto &f : customFields(old, scope))
            f.xmlRollUp.reset();
    check(nativeSchemaFor(old) < 43, "conditional schema");
    for (int method : {-1, 8}) {
        auto bad = p;
        bad.customFields[0].xmlRollUp->rollupType = method;
        rejects([&] { validate(bad); });
    }
    auto bad = p;
    bad.customFields[0].xmlRollUp->calculationType = 3;
    rejects([&] { validate(bad); });
    bad = p;
    bad.customFields.back().xmlRollUp = XmlRollUp{1, 3};
    rejects([&] { validate(bad); });
    bad = p;
    bad.taskSummaryAttributes.push_back(bad.taskSummaryAttributes[0]);
    rejects([&] { validate(bad); });
    bad = p;
    bad.wbsNodes[0].attributes.push_back({"field:unknown", "1"});
    rejects([&] { validate(bad); });
    auto missing = p;
    set(missing, 1, "n3", {});
    check(!value(missing, "", "n3") && !RollUpValues(missing).cell("", "field:n3").issue.empty(), "missing never zero");
    set(missing, 1, "n7", {});
    check(value(missing, "", "n7") == "4", "count includes missing");
    set(missing, 1, "or", {});
    set(missing, 1, "and", {});
    check(value(missing, "", "or") == "1" && value(missing, "", "and") == "0", "three value dominating flags");
    set(missing, 0, "or", "0");
    set(missing, 2, "or", "0");
    set(missing, 3, "or", "0");
    check(!value(missing, "", "or"), "OR unknown");
    auto empty = p;
    empty.wbsNodes.push_back({"E", "빈 그룹", "G"});
    check(value(empty, "G", "n2") == "5" && value(empty, "G", "n6") == "3" && value(empty, "G", "n5") == "4",
          "empty group count and mean");
    auto mixed = p;
    set(mixed, 1, "duration", "1.5 m");
    check(!value(mixed, "", "duration") && !RollUpValues(mixed).cell("", "field:duration").issue.empty(),
          "duration mixed mode error");
    auto precise = p;
    for (auto &f : precise.customFields)
        if (f.id == "cost")
            f.xmlRollUp->rollupType = 4;
    set(precise, 0, "cost", "0.01");
    set(precise, 1, "cost", "0");
    set(precise, 2, "cost", "0");
    set(precise, 3, "cost", "0.01");
    check(value(precise, "", "cost") == "0.01", "half away cost");
    for (std::size_t i : {0, 3})
        set(precise, i, "cost", "-0.01");
    check(value(precise, "", "cost") == "-0.01", "negative half away cost");
    auto wide = p;
    for (std::size_t i = 0; i < 4; ++i)
        set(wide, i, "n3", i < 2 ? "999999999" : "-999999999");
    validate(wide);
    check(value(wide, "", "n3") == "0", "128 bit cancellation");
    for (std::size_t i = 0; i < 4; ++i)
        set(wide, i, "n3", "999999999");
    check(!value(wide, "", "n3") && !RollUpValues(wide).cell("", "field:n3").issue.empty(), "sum overflow is issue");
    auto clock = p;
    set(clock, 3, "finish", "2026-11-14T12:34:56.123456");
    check(value(clock, "", "finish") == "2026-11-14T12:34:56.123456", "fractional date maximum");
    auto large = p;
    large.customFields = {p.customFields[3]};
    large.taskSummaryAttributes.clear();
    for (auto &g : large.wbsNodes)
        g.attributes.clear();
    large.tasks.clear();
    for (int i = 0; i < 20000; ++i) {
        Task t{"L" + std::to_string(i), "합계", 0, {}};
        t.attributes = {{"field:n3", i < 10000 ? "999999999" : "-999999999"}};
        large.tasks.push_back(t);
    }
    validate(large);
    check(value(large, "", "n3") == "0", "sum beyond signed64 intermediate cancels exactly");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope evaluation(control);
        (void)RollUpValues(p);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cooperative cancellation");
    auto calc = p;
    calc.customFields[3].xmlRollUp->calculationType = 2;
    check(value(calc, "", "n3") == "999", "calculation2 snapshot only");
    calc.customFields[3].xmlRollUp->calculationType = 0;
    check(value(calc, "", "n3") == "999", "disabled snapshot");
    auto formula = p;
    CustomField ff{"formula", "말단 계산식", CustomFieldType::Number, "점", {}};
    ff.expression = "[field:n3] * 2";
    ff.xmlRollUp = XmlRollUp{1, 3};
    formula.customFields.push_back(ff);
    validate(formula);
    check(value(formula, "", "formula") == "60", "native formula leaf values");
    auto xml = exportProjectXml(p);
    check(xml.xml.contains("<CalculationType>1</CalculationType>") && xml.xml.contains("<RollupType>7</RollupType>"),
          "XML metadata");
    auto imported = importProjectXml(xml.xml, "roll-up.xml").project;
    const auto sum = named(imported, "수치 3");
    check(value(imported, "", sum.id) == "30", "XML recompute");
    check(attributeValue(imported.taskSummaryAttributes, "field:" + sum.id) == "30", "XML summary snapshot");
    check(imported.projectAttributes.size() == 1 && imported.taskSummaryAttributes.size() > 1, "UID0 scope separation");
    check(!imported.wbsNodes[0].attributes.empty(), "WBS explicit snapshot");
    auto replay = exportProjectXml(imported);
    auto again = importProjectXml(replay.xml, "replay.xml").project;
    check(value(again, "", named(again, "수치 3").id) == "30", "XML replay");
    auto badXml = xml.xml;
    badXml.replace("<RollupType>7</RollupType>", "<RollupType>8</RollupType>");
    rejects([&] { importProjectXml(badXml, "bad.xml"); });
    badXml = xml.xml;
    badXml.replace("<CalculationType>1</CalculationType>", "<CalculationType>3</CalculationType>");
    rejects([&] { importProjectXml(badXml, "bad.xml"); });
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto file = temp.filePath("roll-up.ppm").toStdString();
    store.save(file, p, {});
    Document d(store);
    d.open(file);
    d.setTaskAttribute({"A"}, "field:n3", "10");
    check(value(*d.project(), "", "n3") == "38", "live edit");
    d.undo();
    verify(*d.project());
    d.redo();
    check(value(*d.project(), "", "n3") == "38", "redo");
    d.undo();
    d.setXmlRollUp(FieldScope::Task, "field:n3", XmlRollUp{0, 3});
    check(value(*d.project(), "", "n3") == "999", "disable reveals original");
    d.setSummaryAttribute("", "field:n3", "100");
    check(value(*d.project(), "", "n3") == "100", "explicit editing");
    d.undo();
    d.undo();
    verify(*d.project());
    rejects([&] { d.setSummaryAttribute("", "field:n3", "100"); });
    rejects([&] { d.removeCustomField("snapshot"); });
    d.setSummaryAttribute("", "field:snapshot", "1");
    d.setSummaryAttribute("G", "field:snapshot", "1");
    auto typed = *customField(*d.project(), "field:snapshot");
    typed.type = CustomFieldType::Number;
    rejects([&] { d.putCustomField(typed); });
    d.undo();
    d.undo();
    verify(*d.project());
    d.renameWbs("G", "이름 변경");
    check(value(*d.project(), "G", "snapshot") == "상위 메모", "rename preserves snapshot");
    d.undo();
    d.moveWbs("H", "");
    check(value(*d.project(), "G", "n3") == "2", "hierarchy changes");
    d.undo();
    auto prepared = Document::prepareEdit(
        d.makeEditRequest(ObjectAttributeEdit{FieldScope::Resource, {"R"}, "field:resource", "10"}),
        std::make_shared<CalculationControl>());
    check(d.applyPreparedEdit(*prepared) == EditState::Applied &&
              RollUpValues(*d.project(), FieldScope::Resource).cell("", "field:resource").value == "16",
          "worker edit");
    d.undo();
    d.captureBaseline("요약 동결", "PM", "근거", "2026-11-11T03:00:00Z");
    d.saveScenario("요약 사본", "task-order", 260, "2026-11-11T03:01:00Z", d.project()->documentId,
                   d.project()->revision);
    check(json::decodeProject(json::encodeProject(*d.project()), 43) == *d.project(), "archives roundtrip");
    auto converted =
        convertProjectCurrency(*d.project(), {"USD", "EUR", "2026-11-11", "1/2 환산", "half-away-from-zero-v1", 1, 2});
    check(value(converted, "", "cost") == "2.04" &&
              attributeValue(converted.taskSummaryAttributes, "field:cost") == "10.01",
          "FX stored once derived recomputed");
    validate(converted);
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto recoverySession = recovery.newSession();
    recoverySession->checkpoint(d.recoverySnapshot("2026-11-11T03:03:00Z"));
    recoverySession.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "schema43 recovery");
    Document readOnly(store);
    readOnly.open(file, true);
    rejects([&] { readOnly.setXmlRollUp(FieldScope::Task, "field:n3", XmlRollUp{1, 0}); });
    auto archives = *d.project();
    archives.customFields = old.customFields;
    archives.resourceFields = old.resourceFields;
    archives.taskSummaryAttributes.clear();
    for (auto &g : archives.wbsNodes)
        g.attributes.clear();
    check(nativeSchemaFor(archives) == 43, "archive-only schema43");
    auto observed = p;
    observed.costEntries = {{"fact",
                             "A",
                             "2026-11-11 09:00",
                             "2026-11-11T03:03:00Z",
                             "PM",
                             "이관 근거",
                             {},
                             p.documentId,
                             p.currency,
                             1,
                             100,
                             {}}};
    auto migrated = migrateLedgerIdentities(observed, {{"task", "A", "NEW"}});
    validate(migrated);
    verify(migrated);
    ResourceViewSettings settings;
    settings.groupBy = {"type"};
    auto rows = projectResources(p, settings);
    auto groups = resourceRollUpGroups(rows);
    check(RollUpValues(p, FieldScope::Resource, groups).cell("", "field:resource").value == "8", "resource groups");
    settings.query = "자원 1";
    rows = projectResources(p, settings);
    check(RollUpValues(p, FieldScope::Resource, resourceRollUpGroups(rows)).cell("", "field:resource").value == "2",
          "filtered resources");
    auto taskSpec = TaskViewSpec{};
    taskSpec.classification.groupBy = {"wbs"};
    auto projection = projectTaskView(p, nullptr, taskSpec);
    check(!projection.groups.empty() && !projection.groups[0].rollUpText.empty(), "task group projection");
    rejects([&] { RollUpValues(p, FieldScope::Task, {{"", {}, {0, 0}}}); });
    rejects([&] { RollUpValues(p, FieldScope::Task, {{"", {}, {}}, {"X", "Y", {}}, {"Y", "X", {}}}); });
    const auto out = QDir::currentPath() + "/xml-roll-up-qa";
    QDir().mkpath(out);
    write(out + "/export.xml", xml.xml);
    write(out + "/reexport.xml", replay.xml);
    write(out + "/native.json", QJsonDocument(body).toJson());
    write(out + "/mapping.json", QJsonDocument(xml.report).toJson());
    write(out + "/converted.json", QJsonDocument(json::encodeProject(converted)).toJson());
    auto xer = exportPrimaveraXer(p);
    write(out + "/export.xer", xer.xer);
    for (const auto *name : {"source.ppm", "fields.xlsx", "fields.pdf", "resources.xlsx"})
        QFile::remove(out + '/' + name);
    store.save((out + "/source.ppm").toStdString(), p, {});
    ReportOptions options;
    options.reportId = "xml-roll-up";
    options.generatedAt = "2026-11-11T03:04:00Z";
    auto report = buildReport(p, options);
    exportXlsx(report, out + "/fields.xlsx");
    check(exportPdf(report, out + "/fields.pdf") > 0, "PDF");
    exportXlsx(resourceViewReport(p, settings, options), out + "/resources.xlsx");
    std::cout << "PASS XML summary roll-up: 8 methods, nested weights, typed precision, missing/flags/overflow, "
                 "metadata/snapshots/schema43, XML, edit/undo/worker/FX/archives/groups/reports\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto file = temp.filePath("ui.ppm").toStdString();
    store.save(file, xmlRollUpFixture(), {});
    Document d(store);
    d.open(file);
    const auto out = QDir::currentPath() + "/xml-roll-up-qa";
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
        auto *table = dialog->findChild<QTableWidget *>("rollUpPreview");
        check(table && table->rowCount() == 3 && table->item(0, 1)->text() == "30", "preview computed");
        dialog->resize(800, 600);
        QApplication::processEvents();
        check(dialog->grab().save(out + "/settings-small.png"), "screenshot");
        auto *calculation = dialog->findChild<QComboBox *>("rollUpCalculation");
        calculation->setCurrentIndex(0);
        check(table->item(0, 1)->text() == "999", "retained snapshot preview");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    editXmlRollUp(d, FieldScope::Task, "field:n3", nullptr);
    check(value(*d.project(), "", "n3") == "999", "UI save");
    d.undo();
    verify(*d.project());
    run([&](QDialog *dialog) {
        auto *method = dialog->findChild<QComboBox *>("rollUpMethod");
        method->setCurrentIndex(method->findData(5));
        check(dialog->findChild<QTableWidget *>("rollUpPreview")->item(0, 1)->text() == "10",
              "direct child mean preview");
        dialog->reject();
    });
    editXmlRollUp(d, FieldScope::Task, "field:n3", nullptr);
    run([&](QDialog *dialog) {
        d.renameTask(0, "문서 변경");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        check(!dialog->findChild<QLabel *>("rollUpError")->text().isEmpty(), "stale blocked");
        dialog->reject();
    });
    editXmlRollUp(d, FieldScope::Task, "field:n3", nullptr);
    run([&](QDialog *dialog) {
        auto *table = dialog->findChild<QTableWidget *>("rollUpPreview");
        check(table->rowCount() == 1 && table->item(0, 1)->text() == "8", "resource preview");
        dialog->resize(800, 600);
        QApplication::processEvents();
        check(dialog->grab().save(out + "/resource-small.png"), "resource screenshot");
        dialog->reject();
    });
    editXmlRollUp(d, FieldScope::Resource, "field:resource", nullptr);
    std::cout
        << "PASS roll-up UI: WBS/resource preview, retained snapshots, weighted means, save/undo/stale, 800x600\n";
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
