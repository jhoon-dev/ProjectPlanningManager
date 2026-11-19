#include "XmlAdditionalFunctionFixture.h"
#include "XmlFormulaFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/FormulaFields.h"
#include "domain/XmlRollDown.h"
#include "domain/XmlRollUp.h"
#include "domain/LedgerIdentity.h"
#include "domain/ProjectCurrency.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "reporting/ReportExport.h"
#include "ui/TaskTableModel.h"
#include "ui/XmlFormulaDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
namespace {
using namespace ppm;
using namespace ppm::test;
void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "expected rejection");
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "artifact write");
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source, CustomFieldType type = CustomFieldType::Number) {
    auto it = std::find_if(p.customFields.begin(), p.customFields.end(), [](const auto &f) { return f.id == "probe"; });
    if (it == p.customFields.end()) {
        p.customFields.push_back({"probe", "검사", type, "", {}});
        it = std::prev(p.customFields.end());
    }
    it->type = type;
    it->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    it->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:probe", &p.tasks[0]);
}
void verify(const Project &p) {
    EvaluationScope scope;
    check(value(p, "choose") == "two" && value(p, "choose", 1) == "four", "Choose half-even index");
    check(value(p, "find") == "8" && value(p, "find", 1) == "0" && value(p, "compare") == "0",
          "BMP index and ASCII compare");
    check(value(p, "lefttrim") == "Alpha가  " && value(p, "righttrim") == "  Alpha가", "one-sided spaces");
    check(value(p, "space") == "x  y" && value(p, "repeat") == "가가가" && value(p, "chr") == "A!" &&
              value(p, "asc") == "65",
          "text functions");
    check(value(p, "byte") == "2" && value(p, "byte", 1) == "4" && value(p, "native") == "6",
          "conversions and native chain");
    check(value(p, "currency") == "4.11" && value(p, "currency", 1) == "0.67", "CCur then Cost precision");
    check(value(p, "date") == "2026-11-13" && value(p, "sqrt") == "1.414214" && value(p, "duration") == "60 m",
          "date/root/duration outputs");
    check(value(p, "flag") == "1" && value(p, "flag", 1) == "0", "Flag output");
    const auto error = xmlFormulaValue(p, FieldScope::Task, "field:eager", &p.tasks[0]);
    const auto missing = xmlFormulaValue(p, FieldScope::Task, "field:null", &p.tasks[0]);
    check(!error.value && !error.missing && !error.issue.empty() && !missing.value && missing.missing,
          "eager errors and Null never snapshots");
    check(resolvedProjectAttributeValue(p, "field:project") == "4" &&
              resolvedAttributeValue(p, p.resources[0], "field:resource") == "0",
          "project/resource scopes");
    RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:assignment") == "4" &&
              roll.value(p.assignments[1], "field:assignment") == "6" &&
              roll.value(p.assignments[0], "field:received") == "2",
          "assignment references and roll-down");
    const RollUpValues summary(p);
    check(summary.cell("", "field:summary").value == "요약" && summary.cell("", "field:byte").value == "6",
          "summary formula and roll-up");
}
void contract() {
    const auto p = xmlAdditionalFunctionFixture();
    validate(p);
    verify(p);
    check(nativeSchemaFor(p) == 52 && json::decodeProject(json::encodeProject(p), 52) == p, "conditional schema52");
    rejects([&] { json::decodeProject(json::encodeProject(p), 51); });
    check(nativeSchemaFor(xmlFormulaFixture()) == 44, "old formulas retain writer44");
    auto preserved = p;
    for (const auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(preserved, scope))
            if (f.xmlFormula)
                f.xmlFormula->enabled = false;
    check(nativeSchemaFor(preserved) == 44 && json::decodeProject(json::encodeProject(preserved), 44) == preserved,
          "legacy disabled additional formulas remain readable without activation");
    for (const auto &source :
         {"\"Choose(1,2)\"", "[field:InStr(]", "\"x\"\"Sqr(2)\"", "NotChoose(1,2)", "#2026-11-13#"}) {
        XmlFormula f;
        f.source = source;
        check(!xmlAdditionalFormula(f), "strings/references/longer names are not calls");
    }
    auto probe = p;
    const std::vector<std::pair<std::string, std::string>> good = {{"Choose(2.5,10,20,30)", "20"},
                                                                   {"Choose(3.5,1,2,3,4)", "4"},
                                                                   {"Choose(1,8,Null)", "8"},
                                                                   {"InStr(\"가나다가\",\"가\")", "1"},
                                                                   {"InStr(2,\"가나다가\",\"가\")", "4"},
                                                                   {"InStr(1,\"ABa\",\"a\",1)", "1"},
                                                                   {"InStr(\"ABa\",\"a\")", "3"},
                                                                   {"InStr(2,\"abc\",\"\")", "2"},
                                                                   {"InStr(2147483647,\"abc\",\"a\")", "0"},
                                                                   {"InStr(\"\",\"\")", "0"},
                                                                   {"StrComp(\"가\",\"나\")", "-1"},
                                                                   {"StrComp(\"b\",\"a\")", "1"},
                                                                   {"StrComp(\"A\",\"a\")", "-1"},
                                                                   {"CByte(255.4)", "255"},
                                                                   {"CByte(-0.5)", "0"},
                                                                   {"CCur(1.23445)", "1.2344"},
                                                                   {"CCur(-1.23455)", "-1.2346"},
                                                                   {"CByte(\"2.5\")", "2"},
                                                                   {"Asc(Chr(126))", "126"},
                                                                   {"Sqr(9000000000000)", "3000000"},
                                                                   {"Sqr(0.000001)", "0.001"},
                                                                   {"Sqr(0)", "0"}};
    QJsonArray scalarCases;
    for (const auto &[source, expected] : good) {
        const auto r = eval(probe, source);
        check(r.value == expected, source.c_str());
        scalarCases.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(expected)}});
    }
    for (const auto &source : {"Choose(0,1)", "Choose(9,1,2)", "Choose(Null,1,2)", "Choose(1,Null,2)", "CByte(Null)",
                               "String(Null,\"a\")", "InStr(Null,\"a\")", "StrComp(\"a\",Null)", "Sqr([미설정])"}) {
        const auto r = eval(probe, source);
        check(!r.value && r.missing, "Null propagation/choice bounds");
    }
    for (const auto &source : {"Choose(1,1,Sqr(-1))",
                               "Choose(0,1,1/0)",
                               "InStr(Null,Null,\"a\")",
                               "InStr(0,Null,\"a\")",
                               "InStr(1,\"가\",\"가\",1)",
                               "InStr(1.5,\"ab\",\"b\")",
                               "InStr(1,\"a\",\"a\",2)",
                               "StrComp(Null,Null,Null)",
                               "CByte(255.5)",
                               "CByte(-1)",
                               "Sqr(-1)",
                               "Space(4097)",
                               "String(1366,\"가\")",
                               "String(1,\"\")",
                               "Asc(\"가\")",
                               "Chr(128)",
                               "Chr(10)",
                               "DateValue(\"11/13/2026\")",
                               "DateValue(\"2026-02-30\")",
                               "DateValue(123)",
                               "String(3,321)"}) {
        const auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), source);
    }
    check(eval(probe, "String(3,65)", CustomFieldType::Text).value == "AAA" &&
              eval(probe, "String(2,\"가나\")", CustomFieldType::Text).value == "가가",
          "first character and code repeat");
    check(eval(probe, "Len(Space(4096))").value == "4096" && eval(probe, "Len(String(1365,\"가\"))").value == "1365",
          "bounded strings");
    check(eval(probe, "LTrim(\"  x  \")", CustomFieldType::Text).value == "x  " &&
              eval(probe, "RTrim(\"  x  \")", CustomFieldType::Text).value == "  x",
          "trim preserves opposite side");
    check(eval(probe, "DateValue(\"2024-02-29T23:59:59.123456\")", CustomFieldType::Start).value ==
              "2024-02-29T00:00:00",
          "leap day and midnight date role");
    for (const auto &source :
         {"Choose(1)", "InStr(1)", "StrComp(\"a\")", "Sqr(1,2)", "Space(1,2)", "Now()", "Rnd()", "Format()"}) {
        auto bad = p;
        bad.customFields[5].xmlFormula = bindXmlFormula(bad, FieldScope::Task, source);
        bad.customFields[5].xmlFormula->enabled = true;
        rejects([&] { validate(bad); });
    }
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope scope(control);
        eval(probe, "Sqr(2)");
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cooperative cancellation");
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("functions.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    auto disabled = customField(*doc.project(), "field:byte")->xmlFormula;
    disabled->enabled = false;
    doc.setXmlFormula(FieldScope::Task, "field:byte", disabled);
    check(value(*doc.project(), "byte") == "99" && nativeSchemaFor(*doc.project()) == 52,
          "disabled source retains52 and snapshot");
    doc.undo();
    verify(*doc.project());
    auto renamed = *customField(*doc.project(), "field:q");
    renamed.name = "바뀐 입력수";
    doc.putCustomField(renamed);
    verify(*doc.project());
    doc.undo();
    doc.captureBaseline("함수 동결", "PM", "지원 근거", "2026-11-13T03:00:00Z");
    doc.saveScenario("함수 사본", "task-order", 260, "2026-11-13T03:01:00Z", doc.project()->documentId,
                     doc.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-13", "검토"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 2;
    fx.reporter = "PM";
    fx.reason = "원본 보존";
    fx.recordedAt = "2026-11-13T03:02:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    check(value(*doc.project(), "currency") == "2.06" &&
              json::decodeProject(json::encodeProject(*doc.project()), 52) == *doc.project(),
          "FX recomputes and reads schema52 archives");
    auto observed = *doc.project();
    clearTransitionHistories(observed);
    observed.statusAt = "2026-11-13 18:00";
    observed.costEntries = {{"AC",
                             "A",
                             *observed.statusAt,
                             "2026-11-13T03:03:00Z",
                             "PM",
                             "ID 이관",
                             {},
                             observed.documentId,
                             observed.currency,
                             observed.revision,
                             100,
                             {}}};
    const auto migrated = migrateLedgerIdentities(observed, {{"task", "A", "NEW-A"}});
    check(json::decodeProject(json::encodeProject(migrated), 52) == migrated && value(migrated, "byte") == "2",
          "ID archive52");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (const auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 52 && json::decodeProject(json::encodeProject(frozen), 52) == frozen,
          "archived-only functions retain52");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 51); });
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-13T03:03:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "schema52 recovery");
    doc.save();
    check(store.load(path).project == *doc.project(), "schema52 file roundtrip");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:byte", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "functions.xml");
    check(hasXmlAdditionalFunctions(imported.project, true), "XML enables new supported formulas");
    for (const auto &f : p.customFields) {
        if (!f.xmlFormula)
            continue;
        const auto it = std::find_if(imported.project.customFields.begin(), imported.project.customFields.end(),
                                     [&](const auto &v) { return v.name == f.name; });
        check(it != imported.project.customFields.end() && it->xmlFormula && it->xmlFormula->enabled,
              "XML formula name mapping");
        for (int row = 0; row < 2; ++row)
            check(resolvedAttributeValue(imported.project, imported.project.tasks[row], "field:" + it->id) ==
                      value(p, f.id, row),
                  "XML formula recalculation");
    }
    auto unknownXml = xml.xml;
    unknownXml.replace("Sqr(2)", "Unimplemented(2)");
    const auto unsupported = importProjectXml(unknownXml, "unsupported.xml");
    check(std::any_of(unsupported.project.customFields.begin(), unsupported.project.customFields.end(),
                      [](const auto &f) {
                          return f.xmlFormula && !f.xmlFormula->enabled &&
                                 f.xmlFormula->source.find("Unimplemented") != std::string::npos;
                      }),
          "unknown XML functions stay preserved/disabled");
    const auto out = QDir::currentPath() + "/xml-additional-functions-qa";
    QDir().mkpath(out);
    save(out + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(out + "/archived.json", QJsonDocument(json::encodeProject(*doc.project())).toJson());
    save(out + "/functions.xml", xml.xml);
    save(out + "/functions.xer", exportPrimaveraXer(p).xer);
    save(out + "/mapping.json", QJsonDocument(xml.report).toJson());
    save(out + "/cases.json", QJsonDocument(scalarCases).toJson());
    QJsonArray roots;
    std::uint64_t state = 527391;
    for (int i = 0; i < 512; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto n = std::int64_t(state % 9000000000000000001ULL);
        const auto input = std::to_string(n / 1000000) + "." + std::to_string(n % 1000000 + 1000000).substr(1);
        const auto r = eval(probe, "Sqr(" + input + ")");
        check(r.value.has_value(), "large fixed-point roots");
        roots.append(
            QJsonObject{{"input", QString::fromStdString(input)}, {"value", QString::fromStdString(*r.value)}});
    }
    save(out + "/roots.json", QJsonDocument(roots).toJson());
    for (const auto *name : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(out + '/' + name);
    store.save((out + "/source.ppm").toStdString(), p, {});
    ReportOptions options;
    options.reportId = "xml-additional-functions";
    options.generatedAt = "2026-11-13T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, out + "/fields.xlsx");
    check(exportPdf(report, out + "/fields.pdf") > 0, "typed reports");
    std::cout
        << "PASS 13 additional functions: 4 scopes, BMP/ASCII, eager/null/errors, exact roots/half-even, schema52/old "
           "guards, XML/XER/reports, roll-up/down, FX/ID/frozen/recovery/read-only; 512 root vectors\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("functions.ppm").toStdString();
    store.save(path, xmlAdditionalFunctionFixture(), {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    model.synchronize({0, 1});
    check(model.data(model.index(0, model.columnForKey("field:byte"))).toString() == "2",
          "read-only computed table value");
    int phase = 0;
    bool finished = false;
    QTimer timer;
    timer.setInterval(25);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d || d->objectName() != "xmlFormulaDialog" || d->property("xmlFormulaBusy").toBool())
            return;
        try {
            auto *table = d->findChild<QTableWidget *>("xmlFormulaPreview");
            auto *source = d->findChild<QPlainTextEdit *>("xmlFormulaSource");
            if (phase == 0) {
                check(table->rowCount() == 2 && table->item(0, 1)->text() == "2" && table->item(1, 1)->text() == "4",
                      "typed initial preview");
                check(d->findChild<QLabel *>("xmlFormulaHelp")->text().contains("ASCII32~126"),
                      "function contract visible");
                auto *functions = d->findChild<QComboBox *>("xmlAdditionalFunction");
                check(functions && functions->count() == 13, "all additional function examples");
                source->clear();
                functions->setCurrentIndex(functions->findText("Sqr"));
                d->findChild<QPushButton *>("insertXmlAdditionalFunction")->click();
                check(source->toPlainText() == "Sqr(2)", "function insertion");
                ++phase;
                d->findChild<QPushButton *>("previewXmlFormula")->click();
            } else {
                check(table->rowCount() == 2 && table->item(0, 1)->text() == "1.414214",
                      "async added function preview");
                const auto out = QDir::currentPath() + "/xml-additional-functions-qa";
                QDir().mkpath(out);
                d->resize(800, 700);
                QApplication::processEvents();
                check(d->grab().save(out + "/functions-small.png"), "small UI evidence");
                timer.stop();
                finished = true;
                d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            }
        } catch (const std::exception &e) {
            std::cerr << e.what();
            std::exit(2);
        }
    });
    timer.start();
    editXmlFormula(doc, FieldScope::Task, "field:byte", nullptr);
    check(finished && value(*doc.project(), "byte") == "1.414214", "UI saved schema52 function");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("Unknown(1)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        check(!d->findChild<QLabel *>("xmlFormulaError")->text().isEmpty(), "unsupported functions rejected in UI");
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:byte", nullptr);
    check(*doc.project() == unchanged, "cancelled invalid source unchanged");
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        doc.renameTask(0, "미리보기 후 변경");
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        check(!d->findChild<QLabel *>("xmlFormulaError")->text().isEmpty(), "stale preview blocked");
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:byte", nullptr);
    check(value(*doc.project(), "byte") == "2" && doc.project()->tasks[0].name == "미리보기 후 변경",
          "stale edit kept");
    std::cout << "PASS additional function UI: examples, typed asynchronous preview/save/undo, errors/cancel/stale and "
                 "small layout\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    check(font >= 0, "Korean font");
    QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        if (app.arguments().contains("--ui"))
            ui();
        else
            contract();
    } catch (const std::exception &e) {
        std::cerr << e.what();
        if (const auto *a = dynamic_cast<const AppError *>(&e))
            for (const auto &s : a->diagnostic().arguments)
                std::cerr << ' ' << s;
        return 1;
    }
}
