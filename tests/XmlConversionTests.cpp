#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlSingle.h"
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
    return QDir::currentPath() + "/xml-conversion-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-conversion";
    p.name = "XML 형 변환";
    p.customFields.push_back({"decimalInput", "십진입력", CustomFieldType::Text, "", {}});
    p.customFields.push_back({"singleInput", "단정밀입력", CustomFieldType::Text, "", {}});
    for (int i = 0; i < 2; ++i) {
        p.tasks[i].attributes.push_back({"field:decimalInput", i ? "-2.5" : "1.234567"});
        p.tasks[i].attributes.push_back({"field:singleInput", i ? "16777219" : "16777217"});
    }
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "decimal", "십진변환", CustomFieldType::Number, "CDec([십진입력])");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedConversion", true};
    add(FieldScope::Task, "single", "단정밀변환", CustomFieldType::Number, "CSng([단정밀입력])");
    add(FieldScope::Task, "variant", "형식보존", CustomFieldType::Text, "CVar([십진입력])");
    add(FieldScope::Task, "variantDate", "날짜보존", CustomFieldType::Date, "CVar([입력일시])");
    add(FieldScope::Task, "variantDuration", "경과추정보존", CustomFieldType::Duration,
        "CVar(ProjDurValue(\"1440 em?\"))");
    add(FieldScope::Task, "conversionCost", "금액변환", CustomFieldType::Number, "CDec([입력금액])");
    add(FieldScope::Task, "conversionError", "변환오류", CustomFieldType::Text, "CSng(9000000000000)");
    add(FieldScope::Task, "conversionNull", "변환미설정", CustomFieldType::Text, "CDec(Null)");
    add(FieldScope::Task, "conversionSummary", "변환요약", CustomFieldType::Text, "CVar(\"요약\")");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"conversionNative", "변환혼합식", CustomFieldType::Number, "", {}});
    p.customFields.back().expression = "[field:decimal]*2";
    add(FieldScope::Project, "conversionProject", "프로젝트십진", CustomFieldType::Number, "CDec([프로젝트수])");
    add(FieldScope::Resource, "conversionResource", "자원단정밀", CustomFieldType::Number, "CSng(True)");
    p.assignmentFields.push_back({"receivedConversion", "변환전파", CustomFieldType::Number, "", {}});
    add(FieldScope::Assignment, "conversionAssignment", "배정단정밀", CustomFieldType::Number,
        "CSng([Task.단정밀입력])");
    p.tasks[0].attributes.push_back({"field:decimal", "99"});
    p.tasks[0].attributes.push_back({"field:conversionError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:decimal", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:decimal", &p.tasks[0]);
}
void verify(const Project &p) {
    const std::vector<std::pair<std::string, std::pair<std::string, std::string>>> expected = {
        {"decimal", {"1.234567", "-2.5"}},
        {"single", {"16777216", "16777220"}},
        {"variant", {"1.234567", "-2.5"}},
        {"variantDate", {"2026-11-13T09:30:12.123456", "2026-11-13T09:30:12.123456"}},
        {"variantDuration", {"1440 em?", "1440 em?"}},
        {"conversionNative", {"2.469134", "-5"}}};
    for (const auto &[id, vs] : expected)
        check(value(p, id) == vs.first && value(p, id, 1) == vs.second, "conversion values and native chain");
    check(value(p, "conversionCost") == (p.currency == "EUR" ? "6.17" : "12.34"), "FX recompute");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:conversionError", &p.tasks[0]),
               n = xmlFormulaValue(p, FieldScope::Task, "field:conversionNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error versus Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:conversionProject") == "16" &&
              resolvedAttributeValue(p, p.resources[0], "field:conversionResource") == "-1",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:conversionAssignment") == "16777216" &&
              roll.value(p.assignments[1], "field:conversionAssignment") == "16777220" &&
              roll.value(p.assignments[1], "field:receivedConversion") == "-2.5",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:conversionSummary").value == "요약", "summary");
}
void vectors() {
    QJsonArray rows;
    const auto append = [&](std::int64_t n) {
        rows.append(QJsonObject{{"input", QString::number(n)}, {"output", QString::number(xmlSingleNumber(n))}});
    };
    std::uint64_t state = 941387;
    for (int i = 0; i < 2048; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        auto n = std::int64_t(state % 8999999000000000001ULL);
        if (state & (1ULL << 63))
            n = -n;
        if (i % 3 == 0)
            n = std::int64_t(state % 2000000001ULL) - 1000000000;
        append(n);
    }
    for (auto n : {0LL, 1LL, -1LL, 1000000LL, 16777217000000LL, 16777219000000LL, -16777217000000LL, -16777219000000LL,
                   1234567500000LL, 8999998000000000000LL})
        append(n);
    save(output() + "/vectors.json", QJsonDocument(rows).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlSingleNumber(1234567);
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"CDec(\"1.234567\")", "1.234567"},
        {"CDec(True)", "-1"},
        {"CDec(False)", "0"},
        {"CDec(9000000000000)", "9000000000000"},
        {"CDec(\"-2.5\")", "-2.5"},
        {"CSng(16777217)", "16777216"},
        {"CSng(16777219)", "16777220"},
        {"CSng(-16777217)", "-16777216"},
        {"CSng(-16777219)", "-16777220"},
        {"CSng(1.234567)", "1.234567"},
        {"CSng(0.000001)", "0.000001"},
        {"CSng(-0.000001)", "-0.000001"},
        {"CSng(0)", "0"},
        {"CSng(True)", "-1"},
        {"CSng(\"16777217\")", "16777216"},
        {"CVar(\"001.20\")", "001.20"},
        {"CVar(12.5)", "12.5"},
        {"CVar(True)", "True"},
        {"CVar(#2024-02-29T09:30:12.123456#)", "2024-02-29T09:30:12.123456"},
        {"Year(CVar(#2024-02-29#))", "2024"},
        {"IsNumeric(CVar(\"abc\"))", "False"},
        {"InStr(CVar(\"123\"),\"2\")", "2"},
        {"CVar(CDec(2.5))+1", "3.5"},
        {"ProjDateAdd(#2026-11-13T16:00:00#,CVar(ProjDurValue(\"1440 em?\")))", "2026-11-14T16:00:00"},
        {"CStr(CSng(16777217))", "16777216"},
        {"cdec(\" +2.5 \")", "2.5"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        auto r = eval(probe, source);
        if (r.value != expected)
            std::cerr << source << " expected " << expected << " got " << r.value.value_or(r.issue) << "\n";
        check(r.value == expected, "scalar conversion");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*r.value)}});
    }
    for (const auto *source : {"CDec(Null)", "CSng(Null)", "CVar(Null)"}) {
        auto r = eval(probe, source);
        check(!r.value && r.missing, "Null propagation");
    }
    for (const auto *source : {"CSng(9000000000000)", "CSng(-9000000000000)", "CDec(\"1.2345678\")", "CSng(\"1e20\")",
                               "CDec(\"1,5\")", "CDec(#2024-01-01#)", "CSng(#2024-01-01#)", "CVar(\"12\")+1",
                               "InStr(CVar(12),\"2\")", "IIf(True,1,CSng(9000000000000))"}) {
        auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), "type/range/eager error");
    }
    for (const auto *source : {"CDec()", "CSng(1,2)", "CVar(1,2)"}) {
        auto r = eval(probe, source);
        check(!r.value && !r.issue.empty(), "arity");
    }
    rejects([&] { xmlSingleNumber(9000000000000000001LL); });
    check(!xmlConversionFormula({"CStr(\"CSng(1)\")", {}, true}) && !xmlConversionFormula({"[CVar(1)]", {}, true}),
          "scanner ignores literal/ref");
    check(nativeSchemaFor(p) == 60 && json::decodeProject(json::encodeProject(p), 60) == p, "schema60");
    rejects([&] { json::decodeProject(json::encodeProject(p), 59); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlConversionFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "CSng(16777217)");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:decimal", changed);
    check(value(*doc.project(), "decimal") == "16777216", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "decimal") == "16777216", "redo");
    doc.undo();
    auto renamed = *customField(*doc.project(), "field:decimalInput");
    renamed.name = "명시 문자열 입력";
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
    check(json::decodeProject(json::encodeProject(*doc.project()), 60) == *doc.project(), "FX archives60");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 60 && json::decodeProject(json::encodeProject(frozen), 60) == frozen,
          "frozen-only60");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 59); });
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
    check(json::decodeProject(json::encodeProject(migrated), 60) == migrated, "ID archives60");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery60");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:decimal", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "numbers.xml");
    check(hasXmlConversionFunctions(imported.project, true), "XML active");
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
    options.reportId = "xml-conversion";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS CDec/CSng/CVar: exact binary32/fixed6/type preservation/Null, "
                 "scopes/archives/60/XML/XER/reports; 2058 vectors\n";
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
    check(model.data(model.index(0, model.columnForKey("field:decimal"))).toString() == "1.234567",
          "conversion table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlConversionFunction");
            check(fn && fn->count() == 4, "conversion examples");
            source->clear();
            fn->setCurrentIndex(1);
            d->findChild<QPushButton *>("insertXmlConversionFormula")->click();
            check(source->toPlainText() == "CSng(16777217)", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "16777216")
                        found = true;
            check(found, "conversion preview");
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
    editXmlFormula(doc, FieldScope::Task, "field:decimal", nullptr);
    check(done && value(*doc.project(), "decimal") == "16777216", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("CSng(9000000000000)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:decimal", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS conversion UI: examples/async preview/save/undo/cancel and small layout\n";
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
