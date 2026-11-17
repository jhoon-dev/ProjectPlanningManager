#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlArithmetic.h"
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
    return QDir::currentPath() + "/xml-arithmetic-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-arithmetic";
    p.name = "XML 산술 연산";
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "power", "거듭제곱", CustomFieldType::Number, "[입력수]^3");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedArithmetic", true};
    add(FieldScope::Task, "division", "정수나눗셈", CustomFieldType::Number, "[입력수]\\2");
    add(FieldScope::Task, "costPower", "금액제곱", CustomFieldType::Number, "[입력금액]^2");
    add(FieldScope::Task, "negative", "부호우선순위", CustomFieldType::Number, "-2^2");
    add(FieldScope::Task, "reciprocal", "역수", CustomFieldType::Number, "2^-3");
    add(FieldScope::Task, "arithError", "산술오류", CustomFieldType::Text, "0^-1");
    add(FieldScope::Task, "arithNull", "산술미설정", CustomFieldType::Text, "[미설정]^2");
    add(FieldScope::Task, "arithSummary", "산술요약", CustomFieldType::Number, "2^3^2");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"arithNative", "산술혼합식", CustomFieldType::Number, "", {}, "[field:power]*2"});
    add(FieldScope::Project, "arithProject", "프로젝트제곱근", CustomFieldType::Number, "[프로젝트수]^0.5");
    add(FieldScope::Resource, "arithResource", "자원정수몫", CustomFieldType::Number, "7.5\\2.5");
    p.assignmentFields.push_back({"receivedArithmetic", "산술전파", CustomFieldType::Number, "", {}});
    add(FieldScope::Assignment, "arithAssignment", "배정제곱", CustomFieldType::Number, "[Task.입력수]^2");
    p.tasks[0].attributes.push_back({"field:power", "99"});
    p.tasks[0].attributes.push_back({"field:arithError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:power", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:power", &p.tasks[0]);
}
void verify(const Project &p) {
    check(value(p, "power") == "15.625" && value(p, "power", 1) == "42.875", "power");
    check(value(p, "division") == "1" && value(p, "division", 1) == "2", "operand half-even");
    check(value(p, "negative") == "-4" && value(p, "reciprocal") == "0.125", "precedence/negative exponent");
    check(value(p, "costPower") == (p.currency == "EUR" ? "38.0689" : "152.2756") && value(p, "arithNative") == "31.25",
          "cost major units/native chain");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:arithError", &p.tasks[0]),
               n = xmlFormulaValue(p, FieldScope::Task, "field:arithNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error/Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:arithProject") == "4" &&
              resolvedAttributeValue(p, p.resources[0], "field:arithResource") == "4",
          "project/resource");
    RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:arithAssignment") == "6.25" &&
              roll.value(p.assignments[1], "field:arithAssignment") == "12.25" &&
              roll.value(p.assignments[1], "field:receivedArithmetic") == "42.875",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:arithSummary").value == "64", "summary");
}
void vectors() {
    QJsonArray rows;
    std::uint64_t state = 941387;
    const auto next = [&] {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state;
    };
    for (int i = 0; i < 2048; ++i) {
        const std::int64_t left = 250000 + next() % 3750001, right = std::int64_t(next() % 8000001) - 4000000;
        rows.append(QJsonObject{{"op", "^"},
                                {"left", QString::number(left)},
                                {"right", QString::number(right)},
                                {"output", QString::number(xmlArithmeticNumber("^", left, right))}});
        const std::int64_t a = std::int64_t(next() % 2000000000001ULL) - 1000000000000LL,
                           b = (i % 2 ? -1 : 1) * std::int64_t(500001 + next() % 100000000);
        rows.append(QJsonObject{{"op", "\\"},
                                {"left", QString::number(a)},
                                {"right", QString::number(b)},
                                {"output", QString::number(xmlArithmeticNumber("\\", a, b))}});
    }
    save(output() + "/vectors.json", QJsonDocument(rows).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlArithmeticNumber("^", 2000000, 3000000);
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {{"2^3", "8"},
                                                                    {"2^3^2", "64"},
                                                                    {"2^(3^2)", "512"},
                                                                    {"-2^2", "-4"},
                                                                    {"(-2)^2", "4"},
                                                                    {"2^-3", "0.125"},
                                                                    {"2^-2^2", "0.0625"},
                                                                    {"0^0", "1"},
                                                                    {"0^2", "0"},
                                                                    {"(-2)^3", "-8"},
                                                                    {"(-2)^-3", "-0.125"},
                                                                    {"9^0.5", "3"},
                                                                    {"2^0.5", "1.414214"},
                                                                    {"True^2", "1"},
                                                                    {"(-1)^8999999999999", "-1"},
                                                                    {"9000000000000^1", "9000000000000"},
                                                                    {"2^-9000000000000", "0"},
                                                                    {"11\\4", "2"},
                                                                    {"7.5\\2.5", "4"},
                                                                    {"-7.5\\2.5", "-4"},
                                                                    {"7\\-2", "-3"},
                                                                    {"-7\\-2", "3"},
                                                                    {"True\\2", "0"},
                                                                    {"2147483647.499999\\1", "2147483647"},
                                                                    {"(-2147483648.5)\\1", "-2147483648"},
                                                                    {"8\\3*2", "1"},
                                                                    {"8*3\\2", "12"},
                                                                    {"8\\3 Mod 2", "0"},
                                                                    {"8 Mod 3*2+0^1", "2"},
                                                                    {"8 Mod 3*2", "4"},
                                                                    {"2^3*4+1", "33"},
                                                                    {"(2+3)^2", "25"},
                                                                    {"Not 2^2", "-5"},
                                                                    {"ProjDurValue(\"2 em\")^2", "4"},
                                                                    {"ProjDurValue(\"7 m\")\\2", "3"},
                                                                    {"CDbl(\"2\")^3", "8"},
                                                                    {"IsNull(Null^2)", "True"},
                                                                    {"IsNull(2\\Null)", "True"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        auto r = eval(probe, source);
        if (r.value != expected)
            std::cerr << source << " expected " << expected << " actual " << r.value.value_or(r.issue) << '\n';
        check(r.value == expected, "scalar arithmetic");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*r.value)}});
    }
    for (const auto *source : {"Null^2", "2^Null", "Null\\0", "1\\Null"}) {
        auto r = eval(probe, source);
        check(!r.value && r.missing, "Null propagation");
    }
    for (const auto *source :
         {"0^-1", "(-2)^0.5", "9000000000000^2", "2^100", "1\\0", "1\\0.5", "2147483647.5\\2", "(-2147483648)\\(-1)",
          "1\\2147483648", "\"2\"^3", "#2024-01-01#^2", "IIf(True,1,0^-1)", "2^^3", "2\\\\3", "^2", "2^"}) {
        auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), "domain/type/range/syntax/eager error");
    }
    rejects([&] { xmlArithmeticNumber("bad", 0, 0); });
    rejects([&] { xmlArithmeticNumber("^", 9000000000000000001LL, 0); });
    check(!xmlArithmeticFormula({"CStr(\"^\\\")", {}, true}) && !xmlArithmeticFormula({"[field:a\\]b^]", {}, true}) &&
              !xmlArithmeticFormula({"#2024-01-01#", {}, true}),
          "scanner literal/reference escapes");
    check(nativeSchemaFor(p) == 63 && json::decodeProject(json::encodeProject(p), 63) == p, "schema63");
    rejects([&] { json::decodeProject(json::encodeProject(p), 62); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlArithmeticFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "2^4");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:power", changed);
    check(value(*doc.project(), "power") == "16", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "power") == "16", "redo");
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
    check(json::decodeProject(json::encodeProject(*doc.project()), 63) == *doc.project(), "FX archives63");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 63 && json::decodeProject(json::encodeProject(frozen), 63) == frozen,
          "frozen-only63");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 62); });
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
    check(json::decodeProject(json::encodeProject(migrated), 63) == migrated, "ID archives63");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery63");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:power", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "operators.xml");
    check(hasXmlArithmeticOperators(imported.project, true), "XML active");
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
    options.reportId = "xml-arithmetic";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS power/integer division: precedence/rounding/domain/range/Null, scopes/summary/roll-down, "
                 "schema63/archives/recovery/XML/XER/reports; 4096 vectors\n";
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
    check(model.data(model.index(0, model.columnForKey("field:power"))).toString() == "15.625", "math table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlArithmeticOperator");
            check(fn && fn->count() == 4, "math examples");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlArithmeticFormula")->click();
            check(source->toPlainText() == "2 ^ 3", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "8")
                        found = true;
            check(found, "math preview");
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
    editXmlFormula(doc, FieldScope::Task, "field:power", nullptr);
    check(done && value(*doc.project(), "power") == "8", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("0^-1");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:power", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS arithmetic UI: examples/async preview/save/undo/cancel and small layout\n";
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
