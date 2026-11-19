#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlUnicode.h"
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
    return QDir::currentPath() + "/xml-unicode-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-unicode";
    p.name = "XML Unicode 문자 코드";
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "code", "Unicode문자코드", CustomFieldType::Number, "AscW(Trim([입력문자]))");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedUnicode", true};
    add(FieldScope::Task, "character", "Unicode코드문자", CustomFieldType::Text, "ChrW(44032)");
    add(FieldScope::Task, "signedCode", "부호코드", CustomFieldType::Number, "AscW(ChrW(-21504))");
    add(FieldScope::Task, "roundCharacter", "반올림문자", CustomFieldType::Text, "ChrW(44031.5)");
    add(FieldScope::Task, "costCode", "금액문자코드", CustomFieldType::Number, "AscW(CStr([입력금액]))");
    add(FieldScope::Task, "unicodeError", "문자코드오류", CustomFieldType::Text, "ChrW(55296)");
    add(FieldScope::Task, "unicodeNull", "문자코드미설정", CustomFieldType::Text,
        "IIf(IsNull([미설정]),Null,AscW(\"A\"))");
    add(FieldScope::Task, "unicodeSummary", "문자코드요약", CustomFieldType::Text, "ChrW(54620)&ChrW(44544)");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"unicodeNative", "문자코드혼합식", CustomFieldType::Number, "", {}, "[field:code]*2"});
    add(FieldScope::Project, "unicodeProject", "프로젝트문자코드", CustomFieldType::Number, "AscW(CStr([프로젝트수]))");
    add(FieldScope::Resource, "unicodeResource", "자원문자코드", CustomFieldType::Number, "AscW([Name])");
    p.assignmentFields.push_back({"receivedUnicode", "문자코드전파", CustomFieldType::Number, "", {}});
    add(FieldScope::Assignment, "unicodeAssignment", "배정문자코드", CustomFieldType::Number,
        "AscW(Trim([Task.입력문자]))");
    p.tasks[0].attributes.push_back({"field:code", "99"});
    p.tasks[0].attributes.push_back({"field:unicodeError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:code", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:code", &p.tasks[0]);
}
void verify(const Project &p) {
    for (const auto &[id, a, b] :
         std::vector<std::tuple<std::string, std::string, std::string>>{{"code", "65", "66"},
                                                                        {"character", "가", "가"},
                                                                        {"signedCode", "-21504", "-21504"},
                                                                        {"roundCharacter", "가", "가"},
                                                                        {"unicodeNative", "130", "132"}})
        check(value(p, id) == a && value(p, id, 1) == b, "Unicode values and native chain");
    check(value(p, "costCode") == (p.currency == "EUR" ? "54" : "49") &&
              value(p, "costCode", 1) == (p.currency == "EUR" ? "49" : "50"),
          "FX recompute");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:unicodeError", &p.tasks[0]);
    const auto n = xmlFormulaValue(p, FieldScope::Task, "field:unicodeNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error versus Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:unicodeProject") == "49" &&
              resolvedAttributeValue(p, p.resources[0], "field:unicodeResource") == "-14427",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:unicodeAssignment") == "65" &&
              roll.value(p.assignments[1], "field:unicodeAssignment") == "66" &&
              roll.value(p.assignments[1], "field:receivedUnicode") == "66",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:unicodeSummary").value == "한글", "summary");
}
void vectors() {
    QJsonArray rows;
    for (int code = 0; code <= 65535; ++code) {
        const bool allowed = !(code < 32 && code != 9 && code != 10 && code != 13) && code != 127 &&
                             !(code >= 0xd800 && code <= 0xdfff) && code < 0xfffe;
        if (!allowed) {
            rejects([&] { xmlChrWide(code); });
            continue;
        }
        const auto text = xmlChrWide(code);
        const auto signedCode = xmlAscWide(text);
        check(code < 32768 || xmlChrWide(code - 65536) == text, "signed alias");
        rows.append(QJsonObject{{"input", code},
                                {"utf8", QString::fromLatin1(QByteArray::fromStdString(text).toHex())},
                                {"asc", double(signedCode)}});
    }
    check(rows.size() == 63456, "all permitted BMP characters");
    save(output() + "/vectors.json", QJsonDocument(rows).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlChrWide(65);
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {{"Asc(\"A\")", "65"},
                                                                    {"Chr(65)", "A"},
                                                                    {"AscW(\"A\")", "65"},
                                                                    {"AscW(\"가나다\")", "-21504"},
                                                                    {"AscW(\"é\")", "233"},
                                                                    {"AscW(\"中\")", "20013"},
                                                                    {"AscW(ChrW(32767))", "32767"},
                                                                    {"AscW(ChrW(32768))", "-32768"},
                                                                    {"ChrW(44032)", "가"},
                                                                    {"ChrW(-21504)", "가"},
                                                                    {"ChrW(32.5)", " "},
                                                                    {"AscW(ChrW(33.5))", "34"},
                                                                    {"AscW(ChrW(44031.5))", "-21504"},
                                                                    {"AscW(ChrW(44032.5))", "-21504"},
                                                                    {"AscW(ChrW(-21503.5))", "-21504"},
                                                                    {"AscW(ChrW(65533))", "-3"},
                                                                    {"AscW(ChrW(-3))", "-3"},
                                                                    {"AscW(CStr(123))", "49"},
                                                                    {"ChrW(65)&ChrW(233)", "Aé"},
                                                                    {"AscW(ChrW(9))", "9"},
                                                                    {"AscW(ChrW(10))", "10"},
                                                                    {"AscW(ChrW(13))", "13"},
                                                                    {"AscW(ChrW(65)) + 1", "66"},
                                                                    {"IIf(AscW(\"A\")=65,ChrW(44032),ChrW(66))", "가"},
                                                                    {"ascw(\"B\")", "66"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto result = eval(probe, source);
        if (result.value != expected)
            std::cerr << source << " expected " << expected << " got " << result.value.value_or(result.issue) << '\n';
        check(result.value == expected, "Unicode scalar");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*result.value)}});
    }
    for (const auto *source :
         {"Asc(\"가\")",  "Chr(44032)",         "AscW(\"\")",         "AscW(Null)",    "ChrW(Null)", "AscW(12)",
          "AscW(True)",   "ChrW(\"65\")",       "ChrW(#2024-01-01#)", "ChrW(0)",       "ChrW(127)",  "ChrW(55296)",
          "ChrW(57343)",  "ChrW(-10240)",       "ChrW(65534)",        "ChrW(65535)",   "ChrW(True)", "ChrW(-32769)",
          "ChrW(65536)",  "ChrW(65535.5)",      "AscW(\"😀\")",       "AscW(\"A😀\")", "AscW()",     "ChrW(65,66)",
          "AscW1(\"A\")", "IIf(True,1,ChrW(0))"}) {
        const auto result = eval(probe, source);
        check(!result.value && !result.missing && !result.issue.empty(), "Unicode range/type/Null/eager error");
    }
    check(eval(probe, "IIf(True,Null,AscW(\"A\"))").missing, "explicit missing result");
    rejects([&] { xmlAscWide(std::string(4097, 'a')); });
    rejects([&] { xmlAscWide(std::string("\xc0\xaf", 2)); });
    rejects([&] { xmlAscWide(std::string("A\xed\xa0\x80", 4)); });
    check(xmlAscWide(std::string(4096, 'a')) == 97, "bounded input");
    check(!xmlUnicodeFormula({"\"AscW(1)\"", {}, true}) && !xmlUnicodeFormula({"[field:ChrW\\]X]", {}, true}) &&
              !xmlUnicodeFormula({"AscW1(1)", {}, true}) && xmlUnicodeFormula({"cHrW (65)", {}, true}),
          "scanner");
    check(nativeSchemaFor(p) == 66 && json::decodeProject(json::encodeProject(p), 66) == p, "schema66");
    rejects([&] { json::decodeProject(json::encodeProject(p), 65); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlUnicodeFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "AscW(\"Z\")");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:code", changed);
    check(value(*doc.project(), "code") == "90", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "code") == "90", "redo");
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
    check(json::decodeProject(json::encodeProject(*doc.project()), 66) == *doc.project(), "FX archives66");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 66 && json::decodeProject(json::encodeProject(frozen), 66) == frozen,
          "frozen-only66");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 65); });
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
    check(json::decodeProject(json::encodeProject(migrated), 66) == migrated, "ID archives66");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery66");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:code", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "operators.xml");
    check(hasXmlUnicodeFunctions(imported.project, true), "XML active");
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
    options.reportId = "xml-unicode";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS AscW/ChrW: precedence/rounding/domain/range/Null, scopes/summary/roll-down, "
                 "schema66/archives/recovery/XML/XER/reports; 63456 BMP vectors\n";
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
    check(model.data(model.index(0, model.columnForKey("field:code"))).toString() == "65", "Unicode table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlUnicodeFunction");
            check(fn && fn->count() == 4, "Unicode examples");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlUnicodeFormula")->click();
            check(source->toPlainText() == "AscW(\"가\")", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "-21504")
                        found = true;
            check(found, "Unicode preview");
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
    editXmlFormula(doc, FieldScope::Task, "field:code", nullptr);
    check(done && value(*doc.project(), "code") == "-21504", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("ChrW(0)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:code", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS AscW/ChrW UI: examples/async preview/save/undo/cancel and small layout\n";
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
