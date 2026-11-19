#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlNumericDisplay.h"
#include "domain/FormulaFields.h"
#include "domain/XmlRollUp.h"
#include "domain/XmlRollDown.h"
#include "domain/ProjectCurrency.h"
#include "domain/LedgerIdentity.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include "ui/XmlFormulaDialog.h"
#include "ui/TaskTableModel.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <limits>
#include <tuple>
#include <chrono>
#include "domain/XmlDateFunctions.h"
namespace {
using namespace ppm;
void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    bool bad = false;
    try {
        f();
    } catch (const std::exception &) {
        bad = true;
    }
    check(bad, "expected rejection");
}
QString output() {
    return QDir::currentPath() + "/xml-numeric-display-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-numeric-display";
    p.name = "XML 숫자·백분율·통화 표시";
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "display", "숫자문자표시", CustomFieldType::Text, "FormatNumber([입력수])");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedNumericDisplay", true};
    add(FieldScope::Task, "percentText", "백분율문자표시", CustomFieldType::Text, "FormatPercent([입력수]/100,1)");
    add(FieldScope::Task, "moneyText", "통화문자표시", CustomFieldType::Text, "FormatCurrency([입력금액])");
    add(FieldScope::Task, "numericLength", "숫자표시길이", CustomFieldType::Number, "Len(FormatNumber([입력수]))");
    add(FieldScope::Task, "noZeroText", "선행영생략", CustomFieldType::Text, "FormatNumber(-0.5,2,0,-1,0)");
    add(FieldScope::Task, "numericError", "숫자표시오류", CustomFieldType::Text, "FormatPercent([입력수],7)");
    add(FieldScope::Task, "numericNull", "숫자표시미설정", CustomFieldType::Text,
        "IIf(IsNull([미설정]),Null,FormatNumber(1))");
    add(FieldScope::Task, "numericSummary", "숫자표시요약", CustomFieldType::Text, "FormatCurrency(-1234.5)");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back(
        {"numericNative", "숫자표시혼합식", CustomFieldType::Number, "", {}, "[field:numericLength]*2"});
    add(FieldScope::Project, "numericProject", "프로젝트숫자표시", CustomFieldType::Text, "FormatNumber([프로젝트수])");
    add(FieldScope::Resource, "numericResource", "자원백분율표시", CustomFieldType::Text,
        "FormatPercent(Len([Name])/10,0)");
    p.assignmentFields.push_back({"receivedNumericDisplay", "숫자표시전파", CustomFieldType::Text, "", {}});
    add(FieldScope::Assignment, "numericAssignment", "배정숫자표시", CustomFieldType::Text,
        "FormatNumber([Task.입력수],0)");
    p.tasks[0].attributes.push_back({"field:display", "saved"});
    p.tasks[0].attributes.push_back({"field:numericError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:display", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:display", &p.tasks[0]);
}
void verify(const Project &p) {
    for (const auto &[id, a, b] :
         std::vector<std::tuple<std::string, std::string, std::string>>{{"display", "2.50", "3.50"},
                                                                        {"percentText", "2.5%", "3.5%"},
                                                                        {"numericLength", "4", "4"},
                                                                        {"noZeroText", "(.50)", "(.50)"},
                                                                        {"numericNative", "8", "8"}})
        check(value(p, id) == a && value(p, id, 1) == b, "numeric display and native chain");
    check(value(p, "moneyText") == (p.currency == "EUR" ? "$6.17" : "$12.34") &&
              value(p, "moneyText", 1) == (p.currency == "EUR" ? "$1.00" : "$2.00"),
          "FX recompute with fixed dollar display");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:numericError", &p.tasks[0]);
    const auto n = xmlFormulaValue(p, FieldScope::Task, "field:numericNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error versus Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:numericProject") == "16.00" &&
              resolvedAttributeValue(p, p.resources[0], "field:numericResource") == "20%",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:numericAssignment") == "3" &&
              roll.value(p.assignments[1], "field:numericAssignment") == "4" &&
              roll.value(p.assignments[1], "field:receivedNumericDisplay") == "3.50",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:numericSummary").value == "($1,234.50)", "summary");
}
void vectors() {
    QJsonArray rows;
    std::vector<std::int64_t> inputs = {0};
    for (auto n : {1LL, 4999LL, 5000LL, 500000LL, 1005000LL, 1125000LL, 1250000LL, 2500000LL, 999995000LL, 1234567890LL,
                   999999999999LL, 9000000000000000000LL}) {
        inputs.push_back(n);
        inputs.push_back(-n);
    }
    for (const auto &[kind, name] :
         std::vector<std::pair<XmlNumericDisplayKind, const char *>>{{XmlNumericDisplayKind::Number, "number"},
                                                                     {XmlNumericDisplayKind::Percent, "percent"},
                                                                     {XmlNumericDisplayKind::Currency, "currency"}})
        for (int digits = -1; digits <= 6; ++digits)
            for (int leading = -2; leading <= 0; ++leading)
                for (int parens = -2; parens <= 0; ++parens)
                    for (int groups = -2; groups <= 0; ++groups)
                        for (auto input : inputs) {
                            const auto n = input < 0 ? -input : input;
                            const bool comparable = n == 0 || n == 500000 || n == 1125000 || n == 1250000 ||
                                                    n == 2500000 || n == 9000000000000000000LL;
                            rows.append(QJsonObject{{"kind", name},
                                                    {"input", QString::number(input)},
                                                    {"digits", digits},
                                                    {"leading", leading},
                                                    {"parentheses", parens},
                                                    {"grouping", groups},
                                                    {"windowsComparable", comparable},
                                                    {"value", QString::fromStdString(xmlNumericDisplay(
                                                                  kind, input, digits, leading, parens, groups))}});
                        }
    check(rows.size() == 16200, "three displays, precision and all tristate combinations");
    save(output() + "/vectors.json", QJsonDocument(rows).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlNumericDisplay(XmlNumericDisplayKind::Number, 1000000);
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"FormatNumber(1234.5)", "1,234.50"},
        {"FormatPercent(0.125,1)", "12.5%"},
        {"FormatCurrency(-1234.5)", "($1,234.50)"},
        {"FormatNumber(1.25,1)", "1.3"},
        {"FormatNumber(1.125,2)", "1.13"},
        {"FormatNumber(1.005,2)", "1.01"},
        {"FormatNumber(2.5,0)", "3"},
        {"FormatNumber(-2.5,0)", "-3"},
        {"FormatNumber(0,2,0)", ".00"},
        {"FormatNumber(0,0,0)", ""},
        {"FormatNumber(-0.001,2)", "-0.00"},
        {"FormatCurrency(-0.001,2)", "($0.00)"},
        {"FormatPercent(-0.00001,2)", "-0.00%"},
        {"FormatCurrency(0.5,2,0,-1,-1)", "$.50"},
        {"FormatPercent(0.005,2,0,-1,-1)", ".50%"},
        {"FormatNumber(-1234.5,2,-1,-1,0)", "(1234.50)"},
        {"FormatCurrency(-1234.5,2,-1,0,0)", "-$1234.50"},
        {"FormatNumber(1.5,1.5)", "1.50"},
        {"FormatNumber(1.5,2.5)", "1.50"},
        {"FormatNumber(1.5,-0.5)", "2"},
        {"FormatNumber(0.5,2,-0.5)", ".50"},
        {"FormatNumber(0.5,2,-1.5)", "0.50"},
        {"FormatNumber(True)", "-1.00"},
        {"FormatNumber(CDbl(\"1.25\"),1)", "1.3"},
        {"FormatPercent(9000000000000,0)", "900,000,000,000,000%"},
        {"FormatPercent(0.000001,6)", "0.000100%"},
        {"formatNUMBER(12;2;-1;0;0)", "12.00"},
        {"Len(FormatNumber(1234.5))", "8"},
        {"IIf(True,FormatNumber(1),\"x\")", "1.00"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto result = eval(probe, source);
        if (result.value != expected)
            std::cerr << source << " expected [" << expected << "] got [" << result.value.value_or(result.issue)
                      << "]\n";
        check(result.value == expected, "numeric-display scalar");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*result.value)}});
    }
    for (const auto *source : {"FormatNumber(Null)",
                               "FormatPercent(Null)",
                               "FormatCurrency(Null)",
                               "FormatNumber(1,Null)",
                               "FormatNumber(1,2,Null)",
                               "FormatNumber(1,2,-1,Null)",
                               "FormatNumber(1,2,-1,0,Null)",
                               "FormatNumber(\"1\")",
                               "FormatNumber(#2024-01-01#)",
                               "FormatNumber(1,7)",
                               "FormatNumber(1,-2)",
                               "FormatNumber(1,6.500001)",
                               "FormatNumber(1,2,1)",
                               "FormatNumber(1,2,-3)",
                               "FormatNumber(1,2,-1,1)",
                               "FormatNumber(1,2,-1,0,1)",
                               "FormatNumber(1,2,\"-1\")",
                               "FormatNumber()",
                               "FormatPercent(1,2,-1,0,-1,0)",
                               "FormatNumber1(1)",
                               "IIf(True,1,FormatCurrency(Null))"}) {
        const auto result = eval(probe, source);
        check(!result.value && !result.missing && !result.issue.empty(), "numeric-display type/range/Null/eager error");
    }
    check(eval(probe, "IIf(True,Null,FormatNumber(1))").missing, "explicit missing result");
    rejects([&] { xmlNumericDisplay(XmlNumericDisplayKind::Number, 0, 7); });
    rejects([&] { xmlNumericDisplay(XmlNumericDisplayKind::Number, 0, 2, 1); });
    rejects([&] { xmlNumericDisplay(XmlNumericDisplayKind::Number, std::numeric_limits<std::int64_t>::min()); });
    rejects([&] { xmlNumericDisplay(static_cast<XmlNumericDisplayKind>(99), 0); });
    check(!xmlNumericDisplayFormula({"\"FormatNumber(1)\"", {}, true}) &&
              !xmlNumericDisplayFormula({"[field:FormatCurrency\\]X]", {}, true}) &&
              !xmlNumericDisplayFormula({"FormatNumber1(1)", {}, true}) &&
              xmlNumericDisplayFormula({"fOrMaTpErCeNt (1)", {}, true}),
          "scanner boundaries");
    check(nativeSchemaFor(p) == 69 && json::decodeProject(json::encodeProject(p), 69) == p, "schema69");
    rejects([&] { json::decodeProject(json::encodeProject(p), 68); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlNumericDisplayFormula(*f.xmlFormula))
                    f.xmlFormula->enabled = false;
                else
                    f.xmlFormula.reset();
            }
    check(nativeSchemaFor(legacy) == 44 && json::decodeProject(json::encodeProject(legacy), 44) == legacy,
          "disabled legacy44");
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("numbers.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "FormatNumber(9)");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:display", changed);
    check(value(*doc.project(), "display") == "9.00", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "display") == "9.00", "redo");
    doc.undo();
    auto renamed = *customField(*doc.project(), "field:q");
    renamed.name = "명시 숫자 입력";
    doc.putCustomField(renamed);
    verify(*doc.project());
    doc.undo();
    doc.captureBaseline("변환 동결", "PM", "원본 보존", "2026-11-14T03:00:00Z");
    doc.saveScenario("변환 사본", "task-order", 260, "2026-11-14T03:01:00Z", doc.project()->documentId,
                     doc.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-14", "원본 보존"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 2;
    fx.reporter = "PM";
    fx.reason = "변환 원본";
    fx.recordedAt = "2026-11-14T03:02:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    verify(*doc.project());
    check(json::decodeProject(json::encodeProject(*doc.project()), 69) == *doc.project(), "FX archives69");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 69 && json::decodeProject(json::encodeProject(frozen), 69) == frozen,
          "frozen-only69");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 68); });
    auto observed = *doc.project();
    clearTransitionHistories(observed);
    observed.statusAt = "2026-11-14 18:00";
    observed.costEntries = {{"AC",
                             "A",
                             *observed.statusAt,
                             "2026-11-14T03:03:00Z",
                             "PM",
                             "ID 이관",
                             {},
                             observed.documentId,
                             observed.currency,
                             observed.revision,
                             100,
                             {}}};
    const auto migrated = migrateLedgerIdentities(observed, {{"task", "A", "NEW-A"}});
    check(json::decodeProject(json::encodeProject(migrated), 69) == migrated, "ID archives69");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery69");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:display", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "operators.xml");
    check(hasXmlNumericDisplayFunctions(imported.project, true), "XML active");
    for (const auto &f : p.customFields) {
        const auto it = std::find_if(imported.project.customFields.begin(), imported.project.customFields.end(),
                                     [&](const auto &v) { return v.name == f.name; });
        check(it != imported.project.customFields.end(), "XML field");
        for (int i = 0; i < 2; ++i)
            check(value(p, f.id, i) == value(imported.project, it->id, i), "XML recalc");
    }
    QDir().mkpath(output());
    save(output() + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(output() + "/archived.json", QJsonDocument(json::encodeProject(*doc.project())).toJson());
    save(output() + "/operators.xml", xml.xml);
    save(output() + "/operators.xer", exportPrimaveraXer(p).xer);
    save(output() + "/mapping.json", QJsonDocument(xml.report).toJson());
    save(output() + "/cases.json", QJsonDocument(records).toJson());
    vectors();
    for (const auto *n : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(output() + '/' + n);
    store.save((output() + "/source.ppm").toStdString(), p, {});
    ReportOptions options;
    options.reportId = "xml-numeric-display";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS FormatNumber/Percent/Currency: precedence/rounding/domain/range/Null, scopes/summary/roll-down, "
                 "schema69/archives/recovery/XML/XER/reports; 16200 numeric-display vectors\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("numbers.ppm").toStdString();
    store.save(path, fixture(), {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    model.synchronize({0, 1});
    check(model.data(model.index(0, model.columnForKey("field:display"))).toString() == "2.50",
          "numeric-display table value");
    bool done = false;
    int phase = 0;
    QTimer timer;
    timer.setInterval(50);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d)
            return;
        auto *source = d->findChild<QPlainTextEdit *>("xmlFormulaSource");
        if (!phase) {
            auto *fn = d->findChild<QComboBox *>("xmlNumericDisplayFunction");
            check(fn && fn->count() == 4, "numeric-display examples");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlNumericDisplayFormula")->click();
            check(source->toPlainText() == "FormatNumber(1234.5)", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "1,234.50")
                        found = true;
            check(found, "numeric-display preview");
            QDir().mkpath(output());
            check(d->grab().save(output() + "/editor.png"), "editor capture");
            d->resize(800, 650);
            if (auto *scroll = d->findChild<QScrollArea *>())
                scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            check(d->grab().save(output() + "/editor-small.png"), "small capture");
            timer.stop();
            done = true;
            d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        }
    });
    timer.start();
    editXmlFormula(doc, FieldScope::Task, "field:display", nullptr);
    check(done && value(*doc.project(), "display") == "1,234.50", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("FormatNumber(Null)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:display", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS FormatNumber/Percent/Currency UI: examples/async preview/save/undo/cancel and small layout\n";
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
