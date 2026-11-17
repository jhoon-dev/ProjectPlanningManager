#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlMath.h"
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
    return QDir::currentPath() + "/xml-math-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-math";
    p.name = "XML 수학 함수";
    p.customFields.push_back({"angle", "각도입력", CustomFieldType::Number, "", {}});
    for (int i = 0; i < 2; ++i)
        p.tasks[i].attributes.push_back({"field:angle", i ? "-1" : "0.5"});
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "atn", "역탄젠트", CustomFieldType::Number, "Atn([각도입력])");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedMath", true};
    add(FieldScope::Task, "cos", "코사인", CustomFieldType::Number, "Cos([각도입력])");
    add(FieldScope::Task, "exp", "지수", CustomFieldType::Number, "Exp([각도입력])");
    add(FieldScope::Task, "log", "자연로그", CustomFieldType::Number, "Log([입력수])");
    add(FieldScope::Task, "sin", "사인", CustomFieldType::Number, "Sin([각도입력])");
    add(FieldScope::Task, "tan", "탄젠트", CustomFieldType::Number, "Tan([각도입력])");
    add(FieldScope::Task, "mathCost", "금액로그", CustomFieldType::Number, "Log([입력금액])");
    add(FieldScope::Task, "mathError", "수학오류", CustomFieldType::Text, "Log(0)");
    add(FieldScope::Task, "mathNull", "수학미설정", CustomFieldType::Text, "Sin([미설정])");
    add(FieldScope::Task, "mathSummary", "수학요약", CustomFieldType::Number, "Exp(0)");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"mathNative", "수학혼합식", CustomFieldType::Number, "", {}});
    p.customFields.back().expression = "[field:sin]*2";
    add(FieldScope::Project, "mathProject", "프로젝트로그", CustomFieldType::Number, "Log([프로젝트수])");
    add(FieldScope::Resource, "mathResource", "자원코사인", CustomFieldType::Number, "Cos(0)");
    p.assignmentFields.push_back({"receivedMath", "수학전파", CustomFieldType::Number, "", {}});
    add(FieldScope::Assignment, "mathAssignment", "배정역탄젠트", CustomFieldType::Number, "Atn([Task.각도입력])");
    p.tasks[0].attributes.push_back({"field:atn", "99"});
    p.tasks[0].attributes.push_back({"field:mathError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:atn", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:atn", &p.tasks[0]);
}
void verify(const Project &p) {
    const std::vector<std::pair<std::string, std::pair<std::string, std::string>>> expected = {
        {"atn", {"0.463648", "-0.785398"}}, {"cos", {"0.877583", "0.540302"}},  {"exp", {"1.648721", "0.367879"}},
        {"log", {"0.916291", "1.252763"}},  {"sin", {"0.479426", "-0.841471"}}, {"tan", {"0.546302", "-1.557408"}}};
    for (const auto &[id, values] : expected)
        check(value(p, id) == values.first && value(p, id, 1) == values.second, "six math functions");
    check(value(p, "mathCost") == (p.currency == "EUR" ? "1.819699" : "2.512846") &&
              value(p, "mathNative") == "0.958852",
          "FX recompute and native chain");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:mathError", &p.tasks[0]),
               n = xmlFormulaValue(p, FieldScope::Task, "field:mathNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error and Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:mathProject") == "2.772589" &&
              resolvedAttributeValue(p, p.resources[0], "field:mathResource") == "1",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:mathAssignment") == "0.463648" &&
              roll.value(p.assignments[1], "field:mathAssignment") == "-0.785398" &&
              roll.value(p.assignments[1], "field:receivedMath") == "-0.785398",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:mathSummary").value == "1", "summary");
}
void vectors() {
    QJsonArray rows;
    std::uint64_t state = 941387;
    const auto appendVector = [&](const std::string &fn, std::int64_t n) {
        rows.append(QJsonObject{{"function", QString::fromStdString(fn)},
                                {"input", QString::number(n)},
                                {"output", QString::number(xmlMathNumber(fn, n))}});
    };
    for (int i = 0; i < 1024; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto angle = std::int64_t(state % 20000000001ULL) - 10000000000LL;
        for (const auto *fn : {"atn", "sin", "cos", "tan"})
            appendVector(fn, angle);
        appendVector("log", std::int64_t(state % 9000000000000000000ULL) + 1);
        appendVector("exp", std::int64_t(state % 45828246ULL) - 16000000);
    }
    for (const auto n : {0LL, 1LL, -1LL, 1570796LL, 1570797LL, -1570796LL, 9000000000000000000LL,
                         -9000000000000000000LL, 8999999999999999999LL})
        for (const auto *fn : {"atn", "sin", "cos", "tan"})
            appendVector(fn, n);
    for (const auto n : {1LL, 1000000LL, 9000000000000000000LL})
        appendVector("log", n);
    for (const auto n : {-9000000000000000000LL, -16000000LL, -14508658LL, -14508657LL, 0LL, 29828245LL})
        appendVector("exp", n);
    save(output() + "/vectors.json",
         QJsonDocument(QJsonObject{{"long_double_digits", std::numeric_limits<long double>::digits}, {"rows", rows}})
             .toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlMathNumber("sin", 0);
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {{"Atn(0)", "0"},
                                                                    {"Atn(1)", "0.785398"},
                                                                    {"Atn(-1)", "-0.785398"},
                                                                    {"Atn(9000000000000)", "1.570796"},
                                                                    {"Cos(0)", "1"},
                                                                    {"Cos(1)", "0.540302"},
                                                                    {"Sin(0)", "0"},
                                                                    {"Sin(1)", "0.841471"},
                                                                    {"Sin(-1)", "-0.841471"},
                                                                    {"Tan(0)", "0"},
                                                                    {"Tan(1)", "1.557408"},
                                                                    {"Tan(-1)", "-1.557408"},
                                                                    {"Exp(0)", "1"},
                                                                    {"Exp(1)", "2.718282"},
                                                                    {"Exp(-1)", "0.367879"},
                                                                    {"Exp(-9000000000000)", "0"},
                                                                    {"Exp(-14.508658)", "0"},
                                                                    {"Exp(-14.508657)", "0.000001"},
                                                                    {"Log(1)", "0"},
                                                                    {"Log(2)", "0.693147"},
                                                                    {"Log(0.000001)", "-13.815511"},
                                                                    {"Log(9000000000000)", "29.828246"},
                                                                    {"Sin(True)", "-0.841471"},
                                                                    {"Cos(False)", "1"},
                                                                    {"Atn(\" +1 \")", "0.785398"},
                                                                    {"lOg(\"2\")", "0.693147"},
                                                                    {"CStr(Exp(1))", "2.718282"},
                                                                    {"Round(4*Atn(1),3)", "3.142"},
                                                                    {"IIf(Sin(1)>0,Log(1),Exp(0))", "0"},
                                                                    {"IsNull(Tan(Null))", "True"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto r = eval(probe, source);
        if (r.value != expected)
            std::cerr << source << " expected [" << expected << "] got [" << r.value.value_or(r.issue) << "]\n";
        check(r.value == expected, "math scalar case");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*r.value)}});
    }
    for (const auto *source : {"Atn(Null)", "Sin(Null)", "Cos(Null)", "Tan(Null)", "Log(Null)", "Exp(Null)"}) {
        const auto r = eval(probe, source);
        check(!r.value && r.missing, "native Null");
    }
    for (const auto *source :
         {"Log(0)", "Log(-1)", "Log(True)", "Exp(30)", "Exp(29.828246)", "Exp(9000000000000)", "Sin(#2024-01-01#)",
          "Log(\"1,5\")", "Cos(\"abc\")", "Tan(\"\")", "IIf(True,1,Log(0))"}) {
        const auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), "domain/type/range/eager error");
    }
    for (const auto *source : {"Atn()", "Sin(1,2)", "Exp()", "Log(1,10)", "Cos()", "Tan(0,1)"}) {
        const auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), "invalid arity");
    }
    rejects([&] { xmlMathNumber("unknown", 0); });
    rejects([&] { xmlMathNumber("sin", 9000000000000000001LL); });
    check(!xmlMathFormula({"CStr(\"Sin(1)\")", {}, true}) && !xmlMathFormula({"[Log(1)]", {}, true}),
          "scanner ignores strings/refs");
    check(nativeSchemaFor(p) == 58 && json::decodeProject(json::encodeProject(p), 58) == p, "schema58");
    rejects([&] { json::decodeProject(json::encodeProject(p), 57); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlMathFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "Atn(1)");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:atn", changed);
    check(value(*doc.project(), "atn") == "0.785398", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "atn") == "0.785398", "redo");
    doc.undo();
    auto renamed = *customField(*doc.project(), "field:angle");
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
    check(json::decodeProject(json::encodeProject(*doc.project()), 58) == *doc.project(), "FX archives58");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 58 && json::decodeProject(json::encodeProject(frozen), 58) == frozen,
          "frozen-only58");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 57); });
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
    check(json::decodeProject(json::encodeProject(migrated), 58) == migrated, "ID archives58");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-13T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery58");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:atn", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "numbers.xml");
    check(hasXmlMathFunctions(imported.project, true), "XML active");
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
    options.reportId = "xml-math";
    options.generatedAt = "2026-11-13T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS Atn/Cos/Exp/Log/Sin/Tan: domains/finite/range/fixed6/Null, scopes/summary/roll-down, "
                 "schema58/archives/recovery/XML/XER/reports; 6189 vectors\n";
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
    check(model.data(model.index(0, model.columnForKey("field:atn"))).toString() == "0.463648", "math table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlMathFunction");
            check(fn && fn->count() == 6, "math examples");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlMathFormula")->click();
            check(source->toPlainText() == "Atn(1)", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "0.785398")
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
    editXmlFormula(doc, FieldScope::Task, "field:atn", nullptr);
    check(done && value(*doc.project(), "atn") == "0.785398", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("Log(0)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:atn", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS math UI: examples/async async preview/save/undo/cancel and small layout\n";
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
