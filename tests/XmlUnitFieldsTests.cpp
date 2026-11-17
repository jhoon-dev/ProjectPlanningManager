#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/Classification.h"
#include "domain/XmlFormula.h"
#include "domain/XmlUnitFields.h"
#include "domain/XmlRollUp.h"
#include "domain/XmlRollDown.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "reporting/ReportExport.h"
#include "ui/XmlFormulaDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>
namespace {
using namespace jobs;
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f, const char *why) {
    bool caught = false;
    try {
        f();
    } catch (const AppError &) {
        caught = true;
    }
    check(caught, why);
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size(), "save QA file");
}
void add(Project &p, FieldScope scope, const std::string &id, const std::string &expression,
         CustomFieldType type = CustomFieldType::Number) {
    auto &fields = customFields(p, scope);
    fields.push_back({id, id, type, "", {}});
    fields.back().xmlFormula = bindXmlFormula(p, scope, expression);
    fields.back().xmlFormula->enabled = true;
}
XmlFormulaResult get(const Project &p, FieldScope scope, const std::string &name, std::size_t i = 0) {
    const auto &fields = customFields(p, scope);
    auto f = std::find_if(fields.begin(), fields.end(), [&](const auto &f) { return f.name == name; });
    check(f != fields.end(), "named formula exists");
    return xmlFormulaValue(p, scope, "field:" + f->id, scope == FieldScope::Task ? &p.tasks.at(i) : nullptr,
                           scope == FieldScope::Resource ? &p.resources.at(i) : nullptr,
                           scope == FieldScope::Assignment ? &p.assignments.at(i) : nullptr);
}
Project fixture() {
    Project p;
    p.documentId = "xml-unit-fields";
    p.name = "배정률과 가용률";
    p.startDate = "2026-11-16";
    p.currency = "USD";
    p.calculationProfile = effortProfile;
    p.wbsNodes = {{"G", "요약", ""}};
    p.tasks = {{"A", "A", 480, {}}, {"B", "B", 240, {}}};
    for (auto &t : p.tasks)
        t.wbsId = "G";
    Resource r;
    r.id = "R";
    r.name = "R";
    r.maxUnits = 75;
    r.exceptions = {{"2026-11-17", 0}, {"2026-11-18", {}}, {"2026-11-19", 125}};
    p.resources.push_back(r);
    r.id = "S";
    r.name = "S";
    r.type = "equipment";
    r.maxUnits = 250;
    r.exceptions.clear();
    p.resources.push_back(r);
    r.id = "Z";
    r.name = "Z";
    r.type = "labor";
    r.maxUnits.reset();
    r.exceptions = {{"2026-11-18", 50}};
    p.resources.push_back(r);
    r.id = "ZERO";
    r.name = "ZERO";
    r.maxUnits = 0;
    r.exceptions.clear();
    p.resources.push_back(r);
    p.assignments = {{"A", "R", 50, {{0, 240, 25}}}, {"B", "S", 150}, {"A", "Z", 100}, {"B", "ZERO", 25}};
    p.resourceFields.push_back({"when", "when", CustomFieldType::Date, "", {}});
    for (std::size_t i = 0; i < p.resources.size(); ++i)
        p.resources[i].attributes = {{"field:when", i == 2 ? "2026-11-18T23:59:00" : "2026-11-17T12:00:00"}};
    add(p, FieldScope::Assignment, "U", "[Units]");
    add(p, FieldScope::Assignment, "UA", "[Assignment Units]+[Assignment.Units]");
    add(p, FieldScope::Assignment, "M", "[Resource.Max Units]");
    add(p, FieldScope::Assignment, "MI", "[MaxUnits]");
    add(p, FieldScope::Assignment, "D", "PPMUnitAvailability(#2026-11-17T18:00:00#)");
    add(p, FieldScope::Assignment, "ratio", "[Units]/[Resource.Max Units]");
    add(p, FieldScope::Assignment, "percent", "FormatPercent([Units],2)", CustomFieldType::Text);
    add(p, FieldScope::Resource, "M", "[Max Units]");
    add(p, FieldScope::Resource, "D", "PPMUnitAvailability([field:when])");
    add(p, FieldScope::Resource, "U", "[Units]");
    add(p, FieldScope::Resource, "nullD", "IsNull(PPMUnitAvailability(#2026-11-18#))", CustomFieldType::Flag);
    p.resourceFields.push_back({"native", "native", CustomFieldType::Number, "", {}, "[field:M]*100"});
    add(p, FieldScope::Resource, "chain", "[field:native]+1");
    add(p, FieldScope::Task, "U", "[Units]");
    add(p, FieldScope::Task, "nullU", "IsNull([Assignment Units])", CustomFieldType::Flag);
    add(p, FieldScope::Project, "U", "[Units]");
    validate(p);
    return p;
}
void verify(const Project &p) {
    const auto A = FieldScope::Assignment, R = FieldScope::Resource;
    check(get(p, A, "U").value == "0.5" && get(p, A, "UA").value == "1" && get(p, A, "U", 1).value == "1.5",
          "nominal assignment units use ratio, not contour or percentage points");
    check(get(p, A, "M").value == "0.75" && get(p, A, "MI").value == "0.75",
          "resource prefix and implicit Max Units refer to base capacity");
    check(get(p, A, "D").value == "0" && get(p, A, "D", 1).value == "2.5" && get(p, A, "D", 2).missing,
          "day override, base fallback and unknown remain distinct");
    check(get(p, R, "D").value == "0" && get(p, R, "D", 2).value == "0.5",
          "explicit date expression chooses the resource day");
    check(get(p, R, "M", 2).missing && get(p, R, "M", 3).value == "0", "unknown capacity differs from zero");
    check(get(p, A, "ratio").value == "0.666667" && !get(p, A, "ratio", 3).missing &&
              !get(p, A, "ratio", 3).issue.empty(),
          "ratio rounding and zero division error");
    check(get(p, R, "chain").value == "76", "mixed native/XML dependency uses fraction units");
    check(get(p, A, "percent").value == "50.00%", "explicit percent formatting");
    check(get(p, FieldScope::Task, "U").missing && get(p, FieldScope::Project, "U").missing && get(p, R, "U").missing,
          "non-assignment units do not invent aggregate values");
    check(get(p, FieldScope::Task, "nullU").value == "1" && get(p, R, "nullD").value == "1",
          "Null checks on absent units/capacity");
}
void contract() {
    auto p = fixture();
    verify(p);
    check(nativeSchemaFor(p) == 78, "new bindings/functions require conditional schema78");
    auto body = json::encodeProject(p);
    check(json::decodeProject(body, 78) == p, "schema78 exact bindings and capacity data");
    rejects([&] { json::decodeProject(body, 77); }, "schema77 rejects new semantics");
    auto legacy = p;
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, s)) {
            f.xmlFormula.reset();
            f.expression.clear();
        }
    check(nativeSchemaFor(legacy) < 78, "no unconditional writer upgrade");
    for (int kind = 0; kind < 5; ++kind) {
        auto frozen = legacy;
        if (kind == 0) {
            frozen.baselines.push_back({});
            frozen.baselines.back().input = p;
        } else if (kind == 1) {
            frozen.scenarios.push_back({});
            frozen.scenarios.back().input = p;
        } else if (kind == 2) {
            frozen.xerScheduleAdoptions.push_back({});
            frozen.xerScheduleAdoptions.back().source.input = p;
        } else if (kind == 3) {
            frozen.currencyConversions.push_back({});
            frozen.currencyConversions.back().before = std::make_shared<Project>(p);
        } else {
            frozen.ledgerIdMigrations.push_back({});
            frozen.ledgerIdMigrations.back().after = std::make_shared<Project>(p);
        }
        check(nativeSchemaFor(frozen) == 78, "unit semantics in frozen/history copies require schema78");
    }
    auto inactive = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(inactive, scope))
            if (f.xmlFormula)
                f.xmlFormula->enabled = false;
    check(nativeSchemaFor(inactive) == 78, "inactive stable unit bindings retain schema78");
    add(legacy, FieldScope::Resource, "disabled", "PPMUnitAvailability(#2026-11-17#)");
    legacy.resourceFields.back().xmlFormula->enabled = false;
    check(nativeSchemaFor(legacy) == 44, "disabled function source stays at old raw-formula schema");
    verify(p);
    {
        EvaluationScope scope;
        check(get(p, FieldScope::Assignment, "U").value == "0.5", "before unit edit");
        p.assignments[0].units = 33;
        check(get(p, FieldScope::Assignment, "U").value == "0.33", "shared cache observes assignment unit edit");
        p.resources[0].maxUnits = 125;
        check(get(p, FieldScope::Assignment, "M").value == "1.25", "shared cache observes resource capacity edit");
        p.resources[0].exceptions[0].units = 50;
        check(get(p, FieldScope::Assignment, "D").value == "0.5", "shared cache observes capacity exception edit");
    }
    p = fixture();
    for (const auto &expression :
         {"PPMUnitAvailability()", "PPMUnitAvailability(#2026-11-17#,1)", "[Task.Max Units]", "[Resource.Max Units]"}) {
        auto invalid = p;
        add(invalid, FieldScope::Task, "bad", expression);
        rejects([&] { validate(invalid); }, "invalid arity/scope rejected");
    }
    for (const auto &expression : {"PPMUnitAvailability(100)", "PPMUnitAvailability(\"bad\")",
                                   "PPMUnitAvailability(\"2026-11-17T00:00:00+09:00\")"}) {
        auto invalid = p;
        add(invalid, FieldScope::Resource, "bad", expression);
        auto value = get(invalid, FieldScope::Resource, "bad");
        check(!value.value && !value.missing && !value.issue.empty(), "invalid query dates produce errors");
    }
    auto missing = p;
    add(missing, FieldScope::Resource, "empty", "PPMUnitAvailability([field:when])");
    missing.resources[0].attributes.clear();
    check(get(missing, FieldScope::Resource, "empty").missing, "missing query date propagates Null");
    XmlSummaryResolver summary = [](const auto &) { return XmlFormulaResult{{}, "summary", true}; };
    check(xmlFormulaValue(p, FieldScope::Resource, "field:M", nullptr, &p.resources[0], nullptr, summary).missing &&
              xmlFormulaValue(p, FieldScope::Resource, "field:D", nullptr, &p.resources[0], nullptr, summary).missing,
          "summary does not borrow a leaf resource");
    auto exported = exportProjectXml(p);
    auto imported = importProjectXml(exported.xml, "unit-fields.xml");
    verify(imported.project);
    check(imported.project.resources[0].exceptions == p.resources[0].exceptions,
          "XML availability periods preserve zero/unknown/day overrides");
    auto unsupported = exported.xml;
    unsupported.replace("T23:59:00</AvailableTo>", "T12:00:30</AvailableTo>");
    auto unsupportedInput = importProjectXml(unsupported, "partial-days.xml");
    const auto dateField =
        std::find_if(unsupportedInput.project.resourceFields.begin(), unsupportedInput.project.resourceFields.end(),
                     [](const auto &f) { return f.name == "D"; });
    check(dateField != unsupportedInput.project.resourceFields.end() && !dateField->xmlFormula->enabled,
          "unrepresentable availability prevents a false live recomputation");
    auto bounded = exported.xml;
    bounded.replace("<MaxUnits>0.75</MaxUnits>",
                    "<MaxUnits>0.75</MaxUnits><AvailableFrom>2026-11-17T00:00:00</AvailableFrom>");
    auto boundedInput = importProjectXml(bounded, "bounded.xml");
    check(hasXmlAvailabilityFunctions(boundedInput.project, true) &&
              boundedInput.project.resources[0].availableFrom == "2026-11-17 00:00",
          "resource lifetime bounds are retained with live date lookup");
    auto overlap = exported.xml;
    overlap.replace("2026-11-19T00:00:00</AvailableFrom>", "2026-11-17T00:00:00</AvailableFrom>");
    auto overlapInput = importProjectXml(overlap, "overlap.xml");
    check(overlapInput.project.resources[0].exceptions.empty() &&
              !hasXmlAvailabilityFunctions(overlapInput.project, true),
          "overlapping availability is not partially applied");
    auto range = exported.xml;
    range.replace("2026-11-19T23:59:00</AvailableTo>", "2026-11-21T23:59:00</AvailableTo>");
    auto rangeInput = importProjectXml(range, "range.xml");
    check(resourceUnitsOn(rangeInput.project.resources[0], "2026-11-21") == 125 &&
              resourceUnitsOn(rangeInput.project.resources[0], "2026-11-22") == 75,
          "inclusive multiday availability and base fallback");
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("units.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    doc.setXmlFormula(FieldScope::Assignment, "field:U", bindXmlFormula(p, FieldScope::Assignment, "[Units]*2"));
    // Explicitly enable the edited binding, as in the editor.
    auto edited = *customField(*doc.project(), "field:U", FieldScope::Assignment)->xmlFormula;
    edited.enabled = true;
    doc.setXmlFormula(FieldScope::Assignment, "field:U", edited);
    check(get(*doc.project(), FieldScope::Assignment, "U").value == "1", "document formula edit");
    doc.undo();
    doc.redo();
    check(get(*doc.project(), FieldScope::Assignment, "U").value == "1", "units binding undo/redo");
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-15T09:00:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "schema78 recovery");
    const auto dir = QDir::currentPath() + "/xml-unit-fields-qa";
    QDir().mkpath(dir);
    save(dir + "/project.xml", exported.xml);
    save(dir + "/mapping.json", QJsonDocument(exported.report).toJson());
    save(dir + "/native.json", QJsonDocument(body).toJson());
    auto xer = exportPrimaveraXer(p);
    save(dir + "/project.xer", xer.xer);
    save(dir + "/xer-mapping.json", QJsonDocument(xer.report).toJson());
    for (const auto *file : {"fields.xlsx", "fields.pdf"})
        QFile::remove(dir + "/" + file);
    ReportOptions options;
    options.reportId = "units";
    options.generatedAt = "2026-11-15T09:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, dir + "/fields.xlsx");
    check(exportPdf(report, dir + "/fields.pdf") > 0, "unit field reports");
    std::cout << "PASS units/max-units and explicit daily availability: ratio/null/scopes/cache/XML "
                 "periods/schema78/undo/recovery/reports\n";
}
void ui() {
    auto p = fixture();
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("ui.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    bool inspected = false;
    QTimer timer;
    timer.setInterval(30);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "xmlFormulaDialog" || inspected)
            return;
        inspected = true;
        auto *refs = dialog->findChild<QComboBox *>("xmlFormulaReferences");
        check(refs->findData("[Units]") >= 0 && refs->findData("[Resource.Max Units]") >= 0 &&
                  refs->findData("PPMUnitAvailability([Project Start])") >= 0,
              "unit and dated capacity insertions available");
        refs->setCurrentIndex(refs->findData("[Units]"));
        dialog->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("[Units]*2");
        dialog->findChild<QCheckBox *>("xmlFormulaEnabled")->setChecked(true);
        dialog->findChild<QPushButton *>("previewXmlFormula")->click();
        QTimer::singleShot(80, dialog, [dialog] {
            auto *table = dialog->findChild<QTableWidget *>("xmlFormulaPreview");
            check(table->rowCount() == 4, "assignment preview rows");
            check(table->item(0, 1)->text() == "1", "assignment unit preview value");
            const auto dir = QDir::currentPath() + "/xml-unit-fields-qa";
            QDir().mkpath(dir);
            check(dialog->grab().save(dir + "/review-references.png"), "unit reference insertion screenshot");
            if (auto *scroll = dialog->findChild<QScrollArea *>())
                scroll->ensureWidgetVisible(table);
            check(dialog->grab().save(dir + "/review.png"), "unit editor screenshot");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
    });
    timer.start();
    editXmlFormula(doc, FieldScope::Assignment, "field:U", nullptr);
    timer.stop();
    check(inspected && get(*doc.project(), FieldScope::Assignment, "U").value == "1", "unit formula saved from editor");
    std::cout << "PASS unit reference editor insertions/preview/save\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        if (app.arguments().contains("--ui"))
            ui();
        else
            contract();
    } catch (const std::exception &e) {
        std::cerr << e.what();
        if (const auto *error = dynamic_cast<const AppError *>(&e))
            for (const auto &a : error->diagnostic().arguments)
                std::cerr << ' ' << a;
        return 1;
    }
}
