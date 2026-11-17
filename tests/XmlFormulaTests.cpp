#include "XmlFormulaFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "calculation/ResourceViewReport.h"
#include "domain/FormulaFields.h"
#include "domain/XmlRollUp.h"
#include "domain/XmlRollDown.h"
#include "domain/ProjectCurrency.h"
#include "domain/LedgerIdentity.h"
#include "domain/ResourceView.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "reporting/ReportExport.h"
#include "ui/XmlFormulaDialog.h"
#include "ui/XmlRollDownDialog.h"
#include "ui/TaskTableModel.h"
#include "ui/TaskViewProjection.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
namespace {
using namespace jobs;
using namespace jobs::test;
void check(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("expected formula rejection");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "artifact write");
}
CustomField &field(Project &p, const std::string &id, FieldScope scope = FieldScope::Task) {
    auto &fields = customFields(p, scope);
    auto i = std::find_if(fields.begin(), fields.end(), [&](const auto &v) { return v.id == id; });
    if (i == fields.end())
        throw std::runtime_error("missing field");
    return *i;
}
const CustomField &named(const PlanInput &p, const std::string &name, FieldScope scope = FieldScope::Task) {
    for (const auto &f : customFields(p, scope))
        if (f.name == name)
            return f;
    throw std::runtime_error("missing named field");
}
void expression(Project &p, const std::string &id, const std::string &source, bool enabled = true) {
    auto &f = field(p, id);
    f.xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f.xmlFormula->enabled = enabled;
}
std::optional<std::string> value(const Project &p, const std::string &id, std::size_t row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
void verify(const Project &p) {
    EvaluationScope evaluation;
    check(value(p, "n") == "6" && value(p, "n", 1) == "10", "numeric references");
    check(value(p, "flag") == "0" && value(p, "flag", 1) == "1", "flags/comparisons/Not");
    check(value(p, "text") == "소형:설계" && value(p, "text", 1) == "대형:검토", "IIf and text");
    check(value(p, "cost") == "24.68" && value(p, "duration") == "180 m", "cost/duration conversion");
    check(value(p, "date") == "2026-11-13T09:00:00" && value(p, "start") == "2026-11-11T10:00:00" &&
              value(p, "finish") == "2026-11-15T17:30:06",
          "date functions and target roles");
    check(value(p, "nativeChain") == "12" && value(p, "mixed") == "13", "native/XML dependencies");
    check(!value(p, "error") && !value(p, "missing") && value(p, "nullFlag") == "1", "eager errors/null not snapshots");
    check(value(p, "unknown") == "777", "disabled preserved snapshot");
    const auto formulas = formulaValues(p, FieldScope::Task, &p.tasks[0]);
    check(!formulas->at("field:error").missing && formulas->at("field:missing").missing, "error/missing distinction");
    check(resolvedProjectAttributeValue(p, "field:projectResult") == "6" &&
              resolvedAttributeValue(p, p.resources[0], "field:r") == "6",
          "project/resource formula scopes");
    const RollDownValues assignments(p);
    check(assignments.value(p.assignments[0], "field:a") == "9" &&
              assignments.value(p.assignments[1], "field:a") == "14",
          "assignment formulas");
    check(assignments.value(p.assignments[0], "field:aText") == "인력 담당:설계" &&
              assignments.value(p.assignments[0], "field:aDuration") == "180 m",
          "resource/task references and formula roll-down");
    const RollUpValues summary(p);
    check(summary.cell("", "field:n").value == "16" && summary.cell("", "field:duration").value == "540 m",
          "formula leaf roll-up");
    check(summary.cell("", "field:summary").value == "요약" && summary.cell("", "field:summaryCost").value == "160",
          "calculation2 summary formula");
}
void contract() {
    auto p = xmlFormulaFixture();
    validate(p);
    verify(p);
    check(nativeSchemaFor(p) == 44, "schema44");
    const auto body = json::encodeProject(p);
    check(json::decodeProject(body, 44) == p, "native exact formula/snapshots");
    rejects([&] { json::decodeProject(body, 43); });
    auto old = p;
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(old, s))
            f.xmlFormula.reset();
    check(nativeSchemaFor(old) < 44, "conditional writer");
    for (const auto &source : {"Run(\"cmd\")", "Now()", "[unknown]+1", "1^^2", "1+", "a=1", "CStr(1,2)"}) {
        auto bad = p;
        expression(bad, "n", source);
        rejects([&] { validate(bad); });
    }
    auto bad = p;
    expression(bad, "n", "[混合]");
    rejects([&] { validate(bad); });
    bad = p;
    expression(bad, "n", "[混合]", false);
    validate(bad);
    bad = p;
    expression(bad, "n", "[혼합계산]");
    rejects([&] { validate(bad); });
    bad = p;
    field(bad, "nativeChain").expression = "[field:mixed]";
    rejects([&] { validate(bad); });
    bad = p;
    field(bad, "n").xmlFormula->references[0].scope = FieldScope::Assignment;
    rejects([&] { validate(bad); });
    bad = p;
    field(bad, "n").expression = "1";
    rejects([&] { validate(bad); });
    auto eval = [&](const std::string &source, CustomFieldType type = CustomFieldType::Number) {
        auto x = p;
        field(x, "nativeChain").expression.clear();
        auto &f = field(x, "n");
        f.xmlRollUp.reset();
        f.xmlRollDown.reset();
        f.type = type;
        expression(x, "n", source);
        validateXmlFormulas(x);
        return xmlFormulaValue(x, FieldScope::Task, "field:n", &x.tasks[0]);
    };
    check(eval("Round(2.5,0)").value == "2" && eval("Round(3.5,0)").value == "4" && eval("Round(-2.5,0)").value == "-2",
          "bankers Round");
    check(eval("CInt(2.5)").value == "2" && !eval("CInt(32768)").value, "CInt range");
    check(eval("1 And 2").value == "0" && eval("1 Or 2").value == "3" && eval("Not 0").value == "-1",
          "integer bitwise operators");
    check(eval("False And Null", CustomFieldType::Flag).value == "0" &&
              eval("True Or Null", CustomFieldType::Flag).value == "1",
          "three-valued logical operators");
    check(eval("Int(-1.2)").value == "-2" && eval("Fix(-1.2)").value == "-1" && eval("1/3*3").value == "0.999999",
          "fixed-six arithmetic");
    check(!eval("IIf(True,1,1/0)").value && !eval("Switch(True,1,False,1/0)").value,
          "VBA eager conditional evaluation");
    check(eval("Len(\"설계\")").value == "2" && eval("Mid(\"가나다\",2,1)", CustomFieldType::Text).value == "나",
          "BMP string functions");
    check(eval("DateDiff(\"d\",#2026-11-11T23:59:59#,#2026-11-12T00:00:00#)").value == "1",
          "calendar boundary datediff");
    check(eval("DateAdd(\"s\",1,#2026-11-11T23:59:59.123456#)", CustomFieldType::Finish).value ==
              "2026-11-12T00:00:00.123456",
          "fractional date carry");
    check(!eval("DateAdd(\"invalid\",1,#2026-11-11#)", CustomFieldType::Date).value,
          "unsupported interval explicit issue");
    check(eval("ProjDurValue(\"90 em?\")*2", CustomFieldType::Duration).value == "180 em?", "duration provenance");
    check(!eval("9000000000000+1").value, "range error");
    {
        EvaluationScope evaluation;
        auto x = p;
        check(xmlFormulaValue(x, FieldScope::Task, "field:mixed", &x.tasks[0]).value == "13",
              "direct mixed computation");
        check(formulaRawValue(formulaValues(x, FieldScope::Task, &x.tasks[0])->at("field:mixed")) == "13",
              "no cross-engine cache poison");
        x.tasks[0].attributes[0].value = "5";
        check(value(x, "n") == "12", "same-operation input cache invalidation");
        x.tasks[0].name = "변경";
        expression(x, "text", "[Name]");
        check(value(x, "text") == "변경", "definition/name cache invalidation");
    }
    check(eval("\"a\"\"b\"", CustomFieldType::Text).value == "a\"b", "quoted strings");
    check(eval("\"[unbound]\"", CustomFieldType::Text).value == "[unbound]", "literal brackets never bind");
    auto deep = p;
    deep.customFields = {p.customFields[0]};
    deep.assignmentFields.clear();
    deep.assignments.clear();
    deep.resourceFields.clear();
    deep.resources.clear();
    deep.projectFields.clear();
    deep.projectAttributes.clear();
    deep.taskSummaryAttributes.clear();
    for (auto &g : deep.wbsNodes)
        g.attributes.clear();
    for (auto &t : deep.tasks)
        t.attributes = {{"field:q", "2"}};
    for (int i = 0; i < 63; ++i) {
        CustomField f{"deep" + std::to_string(i), "계층" + std::to_string(i), CustomFieldType::Number, "", {}};
        auto source = std::string(20, '-') + "[field:" + (i ? "deep" + std::to_string(i - 1) : "q") + "]";
        f.xmlFormula = bindXmlFormula(deep, FieldScope::Task, source);
        f.xmlFormula->enabled = true;
        deep.customFields.push_back(f);
    }
    validateXmlFormulas(deep);
    auto bounded = xmlFormulaValue(deep, FieldScope::Task, "field:deep62", &deep.tasks[0]);
    check(!bounded.value && bounded.issue.find("128") != std::string::npos, "combined evaluation stack bounded");
    check(value(p, "n") == "6", "depth guard unwinds");
    auto builtin = p;
    field(builtin, "baseDate").name = "Start";
    expression(builtin, "n", "[Start]");
    validateXmlFormulas(builtin);
    check(field(builtin, "n").xmlFormula->references.front().key == "builtin:start",
          "Start reserved schedule reference");
    auto caseIds = deep;
    caseIds.customFields = {{"Q", "대문자", CustomFieldType::Number, "", {}},
                            {"q", "소문자", CustomFieldType::Number, "", {}},
                            {"result", "구별 결과", CustomFieldType::Number, "", {}}};
    for (auto &t : caseIds.tasks)
        t.attributes = {{"field:Q", "2"}, {"field:q", "3"}};
    expression(caseIds, "result", "[field:Q]*10+[field:q]");
    validate(caseIds);
    check(value(caseIds, "result") == "23" && field(caseIds, "result").xmlFormula->references.size() == 2,
          "stable IDs preserve case");
    auto caseRead = importProjectXml(exportProjectXml(caseIds).xml, "case.xml").project;
    check(value(caseRead, named(caseRead, "구별 결과").id) == "23", "case-sensitive IDs survive XML rewrite");
    auto xml = exportProjectXml(p);
    check(xml.xml.contains("<Formula>") && xml.xml.contains("[Number1]") && !xml.xml.contains("[field:"),
          "XML expressions rewritten to emitted slots");
    auto emptyXml = xml.xml;
    emptyXml.replace("<Formula>Now()</Formula>", "<Formula/>");
    auto emptyRead = importProjectXml(emptyXml, "empty.xml").project;
    check(!named(emptyRead, "미지원 함수").xmlFormula, "empty Formula means manual field");
    auto imported = importProjectXml(xml.xml, "formula.xml").project;
    validate(imported);
    check(value(imported, named(imported, "합산값").id) == "6", "XML live import");
    check(!named(imported, "미지원 함수").xmlFormula->enabled &&
              named(imported, "미지원 함수").xmlFormula->source == "Now()",
          "unsupported preserved/disabled");
    check(RollUpValues(imported).cell("", "field:" + named(imported, "요약비용").id).value == "160",
          "XML summary formula recomputed");
    auto replay = exportProjectXml(imported);
    auto again = importProjectXml(replay.xml, "replay.xml").project;
    check(value(again, named(again, "구분").id) == "소형:설계", "XML typed formula roundtrip");
    auto collision = p;
    field(collision, "label").xmlIdentity =
        XmlFieldIdentity{"188776448", "88888888-7777-4666-8555-444444444444", "Name"};
    auto collisionXml = exportProjectXml(collision);
    check(collisionXml.xml.contains("XML_Field_188776448"), "builtin name collision escaped");
    auto c = importProjectXml(collisionXml.xml, "collision.xml").project;
    check(value(c, named(c, "구분").id) == "소형:설계", "collision preserves reference meaning");
    auto canonical = p;
    field(canonical, "n").name = "Number1";
    auto canonicalRead = importProjectXml(exportProjectXml(canonical).xml, "canonical.xml").project;
    check(value(canonicalRead, named(canonicalRead, "Number1").id) == "6", "canonical names outrank colliding aliases");
    auto cycleXml = xml.xml;
    cycleXml.replace("([Number1]+1)*[Project.계수]", "[합산값]");
    auto cycle = importProjectXml(cycleXml, "cycle.xml").project;
    check(std::none_of(cycle.customFields.begin(), cycle.customFields.end(),
                       [](const auto &f) { return f.xmlFormula && f.xmlFormula->enabled; }),
          "cyclic import disables batch");
    QTemporaryDir temp;
    JsonProjectStore store;
    auto path = temp.filePath("formula.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    rejects([&] { d.setTaskAttribute({"A"}, "field:n", {}); });
    rejects([&] { d.setProjectAttribute("field:projectResult", "1"); });
    rejects([&] { d.setResourceAttribute({"R"}, "field:r", {}); });
    rejects([&] { d.setAssignmentAttribute({assignmentAttributeId(d.project()->assignments[0])}, "field:a", {}); });
    rejects([&] { d.removeCustomField("q"); });
    const auto receiver = d.createRollDownField(FieldScope::Task, "field:n", "새 계산 배정", true);
    const auto *created = customField(*d.project(), "field:" + receiver, FieldScope::Assignment);
    check(created && !created->xmlFormula && !created->xmlRollUp &&
              RollDownValues(*d.project()).value(d.project()->assignments[0], "field:" + receiver) == "6",
          "formula source creates manual roll-down receiver");
    d.undo();
    d.setTaskAttribute({"A"}, "field:q", "3");
    check(value(*d.project(), "n") == "8" &&
              RollDownValues(*d.project()).value(d.project()->assignments[0], "field:a") == "11",
          "live dependent updates");
    d.undo();
    verify(*d.project());
    auto renamed = *customField(*d.project(), "field:q");
    renamed.name = "이름 변경";
    d.putCustomField(renamed);
    check(value(*d.project(), "n") == "6", "stable bindings after rename");
    d.undo();
    auto disabled = field(p, "n").xmlFormula;
    disabled->enabled = false;
    d.setXmlFormula(FieldScope::Task, "field:n", disabled);
    check(value(*d.project(), "n") == "999", "disable reveals snapshot");
    d.undo();
    verify(*d.project());
    auto prepared =
        Document::prepareEdit(d.makeEditRequest(ObjectAttributeEdit{FieldScope::Resource, {"R"}, "field:skill", "4"}),
                              std::make_shared<CalculationControl>());
    check(d.applyPreparedEdit(*prepared) == EditState::Applied &&
              resolvedAttributeValue(*d.project(), d.project()->resources[0], "field:r") == "8",
          "worker formula propagation");
    d.undo();
    d.captureBaseline("계산식 동결", "PM", "XML 근거", "2026-11-11T04:00:00Z");
    d.saveScenario("계산식 사본", "task-order", 260, "2026-11-11T04:01:00Z", d.project()->documentId,
                   d.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-11", "1/2 환산"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 2;
    fx.reporter = "PM";
    fx.reason = "원본 환산";
    fx.recordedAt = "2026-11-11T04:02:00Z";
    fx.confirmed = true;
    d.convertCurrency(fx);
    check(value(*d.project(), "cost") == "12.34" && value(*d.project(), "n") == "6", "FX inputs converted once");
    check(json::decodeProject(json::encodeProject(*d.project()), 44) == *d.project(), "full FX archives schema44");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-11T04:03:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "recovery44");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:n", {}); });
    TaskTableModel model(d);
    model.synchronize({0, 1});
    auto column = model.columnForKey("field:text");
    check(column >= 14 && model.data(model.index(0, column)).toString() == "소형:설계" &&
              !(model.flags(model.index(0, column)) & Qt::ItemIsEditable),
          "typed read-only task model");
    TaskViewSpec spec;
    spec.classification.groupBy = {"field:text"};
    check(projectTaskView(*d.project(), nullptr, spec).groups.size() == 2, "typed task groups");
    ResourceViewSettings settings;
    settings.filters = {{"field:r", "ge", "6"}};
    check(projectResources(*d.project(), settings).size() == 1, "typed resource filters");
    auto observed = p;
    observed.costEntries = {{"fact",
                             "A",
                             "2026-11-11 09:00",
                             "2026-11-11T04:03:00Z",
                             "PM",
                             "ID 이관 근거",
                             {},
                             p.documentId,
                             p.currency,
                             1,
                             100,
                             {}}};
    auto migrated = migrateLedgerIdentities(observed, {{"task", "A", "NEW"}});
    validate(migrated);
    verify(migrated);
    auto cancelled = std::make_shared<CalculationControl>();
    cancelled->cancel();
    bool escaped = false;
    try {
        EvaluationScope evaluation(cancelled);
        (void)xmlFormulaValue(p, FieldScope::Task, "field:n", &p.tasks[0]);
    } catch (const CalculationCancelled &) {
        escaped = true;
    }
    check(escaped, "cancellation preserved");
    verify(p);
    const auto out = QDir::currentPath() + "/xml-formula-qa";
    QDir().mkpath(out);
    write(out + "/export.xml", xml.xml);
    write(out + "/reexport.xml", replay.xml);
    write(out + "/mapping.json", QJsonDocument(xml.report).toJson());
    write(out + "/native.json", QJsonDocument(body).toJson());
    write(out + "/converted.json", QJsonDocument(json::encodeProject(*d.project())).toJson());
    auto xer = exportPrimaveraXer(p);
    write(out + "/export.xer", xer.xer);
    for (const auto *name : {"source.ppm", "fields.xlsx", "fields.pdf", "resources.xlsx"})
        QFile::remove(out + '/' + name);
    store.save((out + "/source.ppm").toStdString(), p, {});
    ReportOptions options;
    options.reportId = "xml-formula";
    options.generatedAt = "2026-11-11T04:04:00Z";
    auto report = buildReport(p, options);
    exportXlsx(report, out + "/fields.xlsx");
    check(exportPdf(report, out + "/fields.pdf") > 0, "PDF");
    exportXlsx(resourceViewReport(p, {}, options), out + "/resources.xlsx");
    QByteArray vectors = "input\tfactor\toffset\tactual\n";
    auto random = p;
    for (int i = 0; i < 400; ++i) {
        const auto input = formatFieldNumber((i % 39 - 19) * 123457LL), factor = std::to_string(i % 11 - 5),
                   offset = formatFieldNumber((i % 17 - 8) * 234567LL);
        random.tasks[0].attributes[0].value = input;
        expression(random, "n", "[수량]*" + factor + "+" + offset + "/7");
        auto result = value(random, "n");
        check(result.has_value(), "random arithmetic");
        vectors += QByteArray::fromStdString(input + '\t' + factor + '\t' + offset + '\t' + *result + '\n');
    }
    write(out + "/arithmetic.tsv", vectors);
    std::cout << "PASS XML formulas: 4 scopes, typed bounded interpreter, stable refs, native/roll-up/roll-down "
                 "dependencies, errors/missing/eager IIf, schema44/FX/archives/recovery/edit/models/reports, 400 "
                 "arithmetic vectors\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    auto file = temp.filePath("ui.ppm").toStdString();
    store.save(file, xmlFormulaFixture(), {});
    Document d(store);
    d.open(file);
    const auto out = QDir::currentPath() + "/xml-formula-qa";
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
        auto *table = dialog->findChild<QTableWidget *>("xmlFormulaPreview");
        check(table->rowCount() == 2 && table->item(0, 1)->text() == "6", "typed preview");
        dialog->resize(800, 650);
        QApplication::processEvents();
        check(dialog->grab().save(out + "/settings-small.png"), "screenshot");
        dialog->findChild<QCheckBox *>("xmlFormulaEnabled")->setChecked(false);
        check(table->item(0, 1)->text() == "999", "snapshot preview");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    editXmlFormula(d, FieldScope::Task, "field:n", nullptr);
    check(value(*d.project(), "n") == "999", "UI disable");
    d.undo();
    run([&](QDialog *dialog) {
        dialog->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("Run(\"cmd\")");
        dialog->findChild<QPushButton *>("previewXmlFormula")->click();
        check(!dialog->findChild<QLabel *>("xmlFormulaError")->text().isEmpty(), "unsupported blocked");
        dialog->reject();
    });
    editXmlFormula(d, FieldScope::Task, "field:n", nullptr);
    run([&](QDialog *dialog) {
        d.renameTask(0, "변경");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        check(!dialog->findChild<QLabel *>("xmlFormulaError")->text().isEmpty(), "stale blocked");
        dialog->reject();
    });
    editXmlFormula(d, FieldScope::Task, "field:n", nullptr);
    run([&](QDialog *dialog) {
        auto *table = dialog->findChild<QTableWidget *>("xmlFormulaPreview");
        check(table->rowCount() == 2 && table->item(0, 1)->text() == "9", "assignment preview");
        dialog->resize(800, 650);
        QApplication::processEvents();
        check(dialog->grab().save(out + "/assignment-small.png"), "assignment screenshot");
        dialog->reject();
    });
    editXmlFormula(d, FieldScope::Assignment, "field:a", nullptr);
    run([&](QDialog *dialog) {
        auto *target = dialog->findChild<QComboBox *>("rollDownTarget");
        target->setCurrentIndex(target->findData("@new"));
        dialog->findChild<QPushButton *>("previewRollDown")->click();
        check(dialog->findChild<QLabel *>("rollDownError")->text().isEmpty() &&
                  dialog->findChild<QTableWidget *>("rollDownPreview")->rowCount() == 2,
              "formula source roll-down preview");
        dialog->reject();
    });
    editXmlRollDown(d, FieldScope::Task, "field:n", nullptr);
    std::cout << "PASS XML formula UI: preview/disable/snapshot/undo, unsupported/stale, assignment refs, Windows "
                 "small layout\n";
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
        for (const auto &s : e.diagnostic().arguments)
            std::cerr << ' ' << s;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
