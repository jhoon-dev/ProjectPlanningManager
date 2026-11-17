#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlLogical.h"
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
    return QDir::currentPath() + "/xml-logical-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-logical";
    p.name = "XML 논리 연산";
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "eqv", "논리동치", CustomFieldType::Number, "CLng([입력수]) Eqv 3");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedLogical", true};
    add(FieldScope::Task, "imp", "논리함의", CustomFieldType::Number, "CLng([입력수]) Imp 1");
    add(FieldScope::Task, "costLogic", "금액비트", CustomFieldType::Number, "CLng([입력금액]) Eqv 3");
    add(FieldScope::Task, "flagLogic", "조건함의", CustomFieldType::Flag, "([입력수]>3) Imp False");
    add(FieldScope::Task, "partialLogic", "부분미설정", CustomFieldType::Number, "Null Imp 2");
    add(FieldScope::Task, "logicError", "논리오류", CustomFieldType::Text, "1.5 Eqv 2");
    add(FieldScope::Task, "logicNull", "논리미설정", CustomFieldType::Text, "Null Eqv True");
    add(FieldScope::Task, "logicSummary", "논리요약", CustomFieldType::Flag, "(True Or True Xor True) Eqv False");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"logicNative", "논리혼합식", CustomFieldType::Number, "", {}, "[field:eqv]*2"});
    add(FieldScope::Project, "logicProject", "프로젝트함의", CustomFieldType::Number, "[프로젝트수] Imp 3");
    add(FieldScope::Resource, "logicResource", "자원동치", CustomFieldType::Flag, "True Eqv False");
    p.assignmentFields.push_back({"receivedLogical", "논리전파", CustomFieldType::Number, "", {}});
    add(FieldScope::Assignment, "logicAssignment", "배정동치", CustomFieldType::Number, "CLng([Task.입력수]) Eqv 3");
    p.tasks[0].attributes.push_back({"field:eqv", "99"});
    p.tasks[0].attributes.push_back({"field:logicError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:eqv", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:eqv", &p.tasks[0]);
}
void verify(const Project &p) {
    check(value(p, "eqv") == "-2" && value(p, "eqv", 1) == "-8", "numeric equivalence");
    check(value(p, "imp") == "-3" && value(p, "imp", 1) == "-5", "numeric implication");
    check(value(p, "flagLogic") == "1" && value(p, "flagLogic", 1) == "0" && value(p, "partialLogic") == "2",
          "Boolean and numeric Null");
    check(value(p, "costLogic") == (p.currency == "EUR" ? "-6" : "-16") && value(p, "logicNative") == "-4",
          "cost/native chain");
    auto e = xmlFormulaValue(p, FieldScope::Task, "field:logicError", &p.tasks[0]),
         n = xmlFormulaValue(p, FieldScope::Task, "field:logicNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error/Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:logicProject") == "-17" &&
              resolvedAttributeValue(p, p.resources[0], "field:logicResource") == "0",
          "project/resource");
    RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:logicAssignment") == "-2" &&
              roll.value(p.assignments[1], "field:logicAssignment") == "-8" &&
              roll.value(p.assignments[1], "field:receivedLogical") == "-8",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:logicSummary").value == "1", "summary");
}
void vectors() {
    QJsonArray rows;
    std::uint64_t state = 941387;
    const auto next = [&] {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state;
    };
    for (int i = 0; i < 2048; ++i) {
        const auto a = std::int64_t(next() % 4294967296ULL) - 2147483648LL,
                   b = std::int64_t(next() % 4294967296ULL) - 2147483648LL;
        for (const auto *op : {"eqv", "imp"})
            rows.append(
                QJsonObject{{"op", op},
                            {"left", QString::number(a)},
                            {"right", QString::number(b)},
                            {"output", QString::number(xmlLogicalNumber(op, a * 1000000, b * 1000000) / 1000000)}});
    }
    save(output() + "/vectors.json", QJsonDocument(rows).toJson());
    QJsonArray truth;
    auto p = fixture();
    for (const auto *a : {"True", "False", "Null", "-2147483648", "-2", "-1", "0", "1", "2", "2147483647"})
        for (const auto *b : {"True", "False", "Null", "-2147483648", "-2", "-1", "0", "1", "2", "2147483647"})
            for (const auto *op : {"Eqv", "Imp"}) {
                const auto source = "(" + std::string(a) + ") " + op + " (" + b + ")";
                const auto r = eval(p, source);
                check(r.value || r.missing, "truth table no error");
                truth.append(QJsonObject{
                    {"left", a},
                    {"right", b},
                    {"op", op},
                    {"value", r.value ? QJsonValue(QString::fromStdString(*r.value)) : QJsonValue(QJsonValue::Null)}});
            }
    save(output() + "/truth.json", QJsonDocument(truth).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlLogicalNumber("eqv", 2000000, 3000000);
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"True Eqv True", "True"},
        {"True Eqv False", "False"},
        {"False Eqv False", "True"},
        {"True Imp False", "False"},
        {"False Imp False", "True"},
        {"False Imp True", "True"},
        {"False Imp Null", "True"},
        {"Null Imp True", "True"},
        {"0 Imp Null", "-1"},
        {"2 Imp Null", "-3"},
        {"Null Imp 2", "2"},
        {"Null Imp -2", "-2"},
        {"5 Eqv 3", "-7"},
        {"5 Imp 3", "-5"},
        {"3 Imp 5", "-3"},
        {"True Imp 2", "2"},
        {"False Eqv 0", "-1"},
        {"True Eqv -1", "-1"},
        {"2147483647 Eqv -2147483648", "0"},
        {"-2147483648 Eqv 0", "2147483647"},
        {"0 Imp -2147483648", "-1"},
        {"True Or True Xor True", "True"},
        {"(True Or True Xor True) Eqv False", "True"},
        {"False Imp False Imp False", "False"},
        {"Not True Eqv False", "True"},
        {"1+2 Eqv 3", "-1"},
        {"8 Mod 3*2 Eqv 2", "-1"},
        {"(-2^2) Eqv -4", "-1"},
        {"False And True Or True Xor True Eqv False Imp False", "False"},
        {"True eQv tRuE", "True"},
        {"ProjDurValue(\"2 em\") Eqv 3", "-2"},
        {"IsNull(Null Eqv True)", "True"},
        {"CLng(1.5) Imp 1", "-3"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        auto r = eval(probe, source);
        if (r.value != expected)
            std::cerr << source << " expected " << expected << " got " << r.value.value_or(r.issue) << '\n';
        check(r.value == expected, "logical scalar");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*r.value)}});
    }
    for (const auto *source : {"True Eqv Null", "Null Eqv False", "True Imp Null", "Null Imp False", "Null Imp 0",
                               "-1 Imp Null", "Null Imp Null"}) {
        auto r = eval(probe, source);
        check(!r.value && r.missing, "typed Null");
    }
    for (const auto *source : {"1.5 Eqv 2", "2147483648 Eqv 0", "0 Imp 2147483648", "#2024-01-01# Imp True",
                               "\"True\" Eqv True", "False Imp (1/0)", "Null Eqv (1/0)", "Eqv(True,False)",
                               "TrueEqvFalse", "True Imp Imp False", "1Eqv0", "0 Imp1"}) {
        auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), "type/range/eager/syntax error");
    }
    rejects([&] { xmlLogicalNumber("bad", 0, 0); });
    rejects([&] { xmlLogicalNumber("eqv", 1000001, 0); });
    rejects([&] { xmlLogicalNumber("imp", 2147483648000000LL, 0); });
    check(!xmlLogicalFormula({"CStr(\"Eqv Imp\")", {}, true}) && !xmlLogicalFormula({"[field:Eqv\\]Imp]", {}, true}) &&
              !xmlLogicalFormula({"Important(1)", {}, true}) && !xmlLogicalFormula({"#2024-01-01#", {}, true}),
          "scanner whole tokens/escapes");
    check(nativeSchemaFor(p) == 64 && json::decodeProject(json::encodeProject(p), 64) == p, "schema64");
    rejects([&] { json::decodeProject(json::encodeProject(p), 63); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlLogicalFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "5 Eqv 3");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:eqv", changed);
    check(value(*doc.project(), "eqv") == "-7", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "eqv") == "-7", "redo");
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
    check(json::decodeProject(json::encodeProject(*doc.project()), 64) == *doc.project(), "FX archives64");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 64 && json::decodeProject(json::encodeProject(frozen), 64) == frozen,
          "frozen-only64");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 63); });
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
    check(json::decodeProject(json::encodeProject(migrated), 64) == migrated, "ID archives64");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery64");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:eqv", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "operators.xml");
    check(hasXmlLogicalOperators(imported.project, true), "XML active");
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
    options.reportId = "xml-logical";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS Eqv/Imp: precedence/rounding/domain/range/Null, scopes/summary/roll-down, "
                 "schema64/archives/recovery/XML/XER/reports; 4096 vectors/200 typed truth cases\n";
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
    check(model.data(model.index(0, model.columnForKey("field:eqv"))).toString() == "-2", "logical table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlLogicalOperator");
            check(fn && fn->count() == 4, "math examples");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlLogicalFormula")->click();
            check(source->toPlainText() == "True Eqv False", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "0")
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
    editXmlFormula(doc, FieldScope::Task, "field:eqv", nullptr);
    check(done && value(*doc.project(), "eqv") == "0", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("1.5 Eqv 2");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:eqv", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS logical UI: examples/async preview/save/undo/cancel and small layout\n";
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
