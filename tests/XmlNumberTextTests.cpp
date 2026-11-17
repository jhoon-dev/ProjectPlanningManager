#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlNumberText.h"
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
namespace {
using namespace jobs;
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
    return QDir::currentPath() + "/xml-number-text-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-number-text";
    p.name = "XML 숫자 문자열 변환";
    p.customFields.push_back({"inputText", "숫자문자", CustomFieldType::Text, "", {}});
    p.customFields.push_back({"radixInput", "진수입력", CustomFieldType::Number, "", {}});
    for (int i = 0; i < 2; ++i) {
        p.tasks[i].attributes.push_back({"field:inputText", i ? "&HFFFF&" : " 12.3456785 hours"});
        p.tasks[i].attributes.push_back({"field:radixInput", i ? "-1.5" : "255.5"});
    }
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "strAmount", "금액문자열", CustomFieldType::Text, "Str([입력금액])");
    add(FieldScope::Task, "parsed", "접두숫자", CustomFieldType::Number, "Val([숫자문자])");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedNumber", true};
    add(FieldScope::Task, "hex", "진수16", CustomFieldType::Text, "Hex([진수입력])");
    add(FieldScope::Task, "oct", "진수8", CustomFieldType::Text, "Oct([진수입력])");
    add(FieldScope::Task, "decoded", "진수왕복", CustomFieldType::Text, "CStr(Val(\"&H\"&[진수16]&\"&\"))");
    add(FieldScope::Task, "small", "작은숫자문자", CustomFieldType::Text, "Str(0.000001)");
    add(FieldScope::Task, "numberError", "숫자변환오류", CustomFieldType::Number, "Val(\"50.5%\")");
    add(FieldScope::Task, "numberNull", "숫자미설정", CustomFieldType::Number, "Val(Null)");
    add(FieldScope::Task, "numberSummary", "숫자요약", CustomFieldType::Number, "Val(\"12 units\")");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"numberNative", "접두혼합식", CustomFieldType::Number, "", {}});
    p.customFields.back().expression = "[field:parsed]*2";
    add(FieldScope::Project, "numberProject", "프로젝트변환", CustomFieldType::Text, "CStr(Val(\"&HFFFF\"))");
    add(FieldScope::Resource, "numberResource", "자원변환", CustomFieldType::Number, "Val(\"&O177777\")");
    p.assignmentFields.push_back({"receivedNumber", "숫자전파", CustomFieldType::Number, "", {}});
    add(FieldScope::Assignment, "numberAssignment", "배정변환", CustomFieldType::Number, "Val([Task.숫자문자])");
    p.tasks[0].attributes.push_back({"field:strAmount", "stale"});
    p.tasks[0].attributes.push_back({"field:numberError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:strAmount", FieldScope::Task));
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:strAmount", &p.tasks[0]);
}
void verify(const Project &p) {
    check(value(p, "strAmount") == (p.currency == "EUR" ? " 6.17" : " 12.34") && value(p, "parsed") == "12.345679" &&
              value(p, "parsed", 1) == "65535",
          "Str leading space and fixed-six Val");
    check(value(p, "hex") == "100" && value(p, "hex", 1) == "FFFFFFFE" && value(p, "oct") == "400" &&
              value(p, "oct", 1) == "37777777776",
          "radix rounding and signed32");
    check(value(p, "decoded") == "256" && value(p, "decoded", 1) == "-2" && value(p, "small") == " 1E-06" &&
              value(p, "numberNative") == "24.691358",
          "roundtrip, exponent and native chain");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:numberError", &p.tasks[0]),
               n = xmlFormulaValue(p, FieldScope::Task, "field:numberNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error and Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:numberProject") == "-1" &&
              resolvedAttributeValue(p, p.resources[0], "field:numberResource") == "-1",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:numberAssignment") == "12.345679" &&
              roll.value(p.assignments[1], "field:numberAssignment") == "65535" &&
              roll.value(p.assignments[1], "field:receivedNumber") == "65535",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:numberSummary").value == "12", "summary");
}
void vectors() {
    QJsonArray conversions, prefixes;
    std::uint64_t state = 941387;
    for (int i = 0; i < 1024; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto n = std::int64_t(state % 4294967296ULL) - 2147483648LL;
        const auto hex = xmlRadixNumber(n, 16);
        const auto decoded = xmlValNumber("&H" + hex + "&");
        check(decoded == n * 1000000, "signed32 radix roundtrip");
        auto fixed = std::int64_t(state % 9000000000000000001ULL);
        if (state & (1ULL << 63))
            fixed = -fixed;
        const auto fixedText = xmlStrNumber(fixed);
        const auto fixedBack = xmlValNumber(fixedText);
        check(fixedBack == fixed, "full native fixed-six Str/Val roundtrip");
        conversions.append(QJsonObject{{"integer", QString::number(n)},
                                       {"str", QString::fromStdString(xmlStrNumber(n * 1000000))},
                                       {"hex", QString::fromStdString(hex)},
                                       {"oct", QString::fromStdString(xmlRadixNumber(n, 8))},
                                       {"roundtrip", QString::number(decoded / 1000000)},
                                       {"fixedScaled", QString::number(fixed)},
                                       {"fixedStr", QString::fromStdString(fixedText)},
                                       {"fixedBack", QString::number(fixedBack)}});
        const auto whole = std::to_string(state % 10000);
        auto fraction = std::to_string(state / 10000 % 1000000000 + 1000000000).substr(1);
        const auto sign = (state & 1) ? "-" : "";
        const auto exponent = int(state / 10000000000 % 14) - 8;
        const auto input = std::string(" \t") + sign + whole + " . " + fraction + (i % 2 ? "D" : "e") +
                           std::to_string(exponent) + "kg";
        prefixes.append(
            QJsonObject{{"input", QString::fromStdString(input)}, {"scaled", QString::number(xmlValNumber(input))}});
    }
    save(output() + "/conversions.json", QJsonDocument(conversions).toJson());
    save(output() + "/prefixes.json", QJsonDocument(prefixes).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlValNumber("12h");
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {{"Str(0)", " 0"},
                                                                    {"Str(459)", " 459"},
                                                                    {"Str(-459.65)", "-459.65"},
                                                                    {"Str(0.5)", " .5"},
                                                                    {"Str(-0.5)", "-.5"},
                                                                    {"Str(0.000001)", " 1E-06"},
                                                                    {"Str(0.000099)", " 9.9E-05"},
                                                                    {"Str(0.0001)", " .0001"},
                                                                    {"Str(9000000000000)", " 9000000000000"},
                                                                    {"Str(\" +2.5 \")", " 2.5"},
                                                                    {"Hex(255)", "FF"},
                                                                    {"Hex(2.5)", "2"},
                                                                    {"Hex(3.5)", "4"},
                                                                    {"Hex(-1.5)", "FFFFFFFE"},
                                                                    {"Hex(-2147483648)", "80000000"},
                                                                    {"Hex(2147483647)", "7FFFFFFF"},
                                                                    {"Hex(\"255.5\")", "100"},
                                                                    {"Oct(459)", "713"},
                                                                    {"Oct(-1)", "37777777777"},
                                                                    {"Oct(0)", "0"},
                                                                    {"Hex(True)", "FFFFFFFF"},
                                                                    {"Val(\"12kg\")", "12"},
                                                                    {"Val(\"$12\")", "0"},
                                                                    {"Val(\"abc\")", "0"},
                                                                    {"Val(\"\")", "0"},
                                                                    {"Val(\" 1 2.3h\")", "12.3"},
                                                                    {"Val(\".5\")", "0.5"},
                                                                    {"Val(\"1.\")", "1"},
                                                                    {"Val(\"1e\")", "1"},
                                                                    {"Val(\"1e+\")", "1"},
                                                                    {"Val(\"1E-2\")", "0.01"},
                                                                    {"Val(\"1D3x\")", "1000"},
                                                                    {"Val(\"1.2345675\")", "1.234568"},
                                                                    {"Val(\"-0.0000005\")", "-0.000001"},
                                                                    {"Val(\"0.00000049\")", "0"},
                                                                    {"Val(\"&HFFFF\")", "-1"},
                                                                    {"Val(\"&HFFFF&\")", "65535"},
                                                                    {"Val(\"&H8000&\")", "32768"},
                                                                    {"Val(\"&H80000000\")", "-2147483648"},
                                                                    {"Val(\"&HFFFFFFFF%\")", "-1"},
                                                                    {"Val(\"&H0000FFFF#\")", "-1"},
                                                                    {"Val(\"&O177777\")", "-1"},
                                                                    {"Val(\"&O37777777777\")", "-1"},
                                                                    {"Val(\"&HFFG\")", "255"},
                                                                    {"Val(\"&O178\")", "15"},
                                                                    {"Val(\"-&HFF\")", "0"},
                                                                    {"Val(\"&H\")", "0"},
                                                                    {"Val(\"50%\")", "50"},
                                                                    {"Val(\"-32768%\")", "-32768"},
                                                                    {"Val(\"2147483647&\")", "2147483647"},
                                                                    {"Val(\"-2147483648&\")", "-2147483648"},
                                                                    {"Val(\"1.23456789#\")", "1.234568"},
                                                                    {"Val(\"0e10000\")", "0"},
                                                                    {"Val(\"1e-10000\")", "0"},
                                                                    {"Val(\"9000000000000.000000\")", "9000000000000"},
                                                                    {"Val(\"1,234\")", "1"},
                                                                    {"Val(\"12가\")", "12"},
                                                                    {"Val(Str(0.5))", "0.5"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto r = eval(probe, source);
        if (r.value != expected)
            std::cerr << source << " expected [" << expected << "] got [" << r.value.value_or(r.issue) << "]\n";
        check(r.value == expected, "numeric text case");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*r.value)}});
    }
    for (const auto *source : {"Str(Null)", "Val(Null)", "Hex(Null)", "Oct(Null)"}) {
        const auto r = eval(probe, source);
        check(!r.value && r.missing, "native Null");
    }
    for (const auto *source :
         {"Val(12)", "Str(#2024-01-01#)", "Hex(2147483647.5)", "Oct(-2147483648.500001)", "Val(\"50.5%\")",
          "Val(\"1.0&\")", "Val(\"1e2%\")", "Val(\"32768%\")", "Val(\"2147483648&\")", "Val(\"&H100000000\")",
          "Val(\"&H10000%\")", "Val(\"1!\")", "Val(\"1@\")", "Val(\"1$\")", "Val(\"1^\")", "Val(\"1e10001\")",
          "Val(\"9.0000000000000000001e12\")", "Val(\"9000000000000.0000001\")", "IIf(True,1,Val(\"50.5%\"))"}) {
        const auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), "invalid conversion explicit");
    }
    rejects([&] { xmlValNumber(std::string(4097, '1')); });
    rejects([&] { xmlRadixNumber(2147483648LL, 16); });
    check(!xmlNumberTextFormula({"CStr(\"Val(1)\")", {}, true}) && !xmlNumberTextFormula({"[Hex(1)]", {}, true}),
          "scanner ignores strings/refs");
    check(nativeSchemaFor(p) == 57 && json::decodeProject(json::encodeProject(p), 57) == p, "schema57");
    rejects([&] { json::decodeProject(json::encodeProject(p), 56); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlNumberTextFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "Str(0.5)");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:strAmount", changed);
    check(value(*doc.project(), "strAmount") == " .5", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "strAmount") == " .5", "redo");
    doc.undo();
    auto renamed = *customField(*doc.project(), "field:inputText");
    renamed.name = "명시 숫자 입력";
    doc.putCustomField(renamed);
    verify(*doc.project());
    doc.undo();
    doc.captureBaseline("변환 동결", "PM", "원본 보존", "2026-11-13T03:00:00Z");
    doc.saveScenario("변환 사본", "task-order", 260, "2026-11-13T03:01:00Z", doc.project()->documentId,
                     doc.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-13", "원본 보존"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 2;
    fx.reporter = "PM";
    fx.reason = "변환 원본";
    fx.recordedAt = "2026-11-13T03:02:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    verify(*doc.project());
    check(json::decodeProject(json::encodeProject(*doc.project()), 57) == *doc.project(), "FX archives57");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 57 && json::decodeProject(json::encodeProject(frozen), 57) == frozen,
          "frozen-only57");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 56); });
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
    check(json::decodeProject(json::encodeProject(migrated), 57) == migrated, "ID archives57");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-13T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery57");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:strAmount", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "numbers.xml");
    check(hasXmlNumberTextFunctions(imported.project, true), "XML active");
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
    save(output() + "/numbers.xml", xml.xml);
    save(output() + "/numbers.xer", exportPrimaveraXer(p).xer);
    save(output() + "/mapping.json", QJsonDocument(xml.report).toJson());
    save(output() + "/cases.json", QJsonDocument(records).toJson());
    vectors();
    for (const auto *n : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(output() + '/' + n);
    store.save((output() + "/source.ppm").toStdString(), p, {});
    ReportOptions options;
    options.reportId = "xml-number-text";
    options.generatedAt = "2026-11-13T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS Str/Val/Hex/Oct: prefix/exponents/typed suffixes/fixed6/signed32, scopes/summary/roll-down, "
                 "schema57/archives/recovery/XML/XER/reports; 1024 conversions and 1024 prefixes\n";
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
    check(model.data(model.index(0, model.columnForKey("field:strAmount"))).toString() == " 12.34", "table sign space");
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
            auto *fn = d->findChild<QComboBox *>("xmlNumberTextFunction");
            check(fn && fn->count() == 5, "conversion examples");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlNumberTextFormula")->click();
            check(source->toPlainText() == "Str(0.5)", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == " .5")
                        found = true;
            check(found, "leading-space preview");
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
    editXmlFormula(doc, FieldScope::Task, "field:strAmount", nullptr);
    check(done && value(*doc.project(), "strAmount") == " .5", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("Val(\"50.5%\")");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:strAmount", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS conversion UI: examples/leading-space async preview/save/undo/cancel and small layout\n";
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
