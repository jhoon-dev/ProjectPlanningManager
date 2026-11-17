#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlDepreciation.h"
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
    return QDir::currentPath() + "/xml-depreciation-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-depreciation";
    p.name = "XML 감가상각";
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "depreciation", "정액상각", CustomFieldType::Number, "SLN([입력금액],0,[입력수])");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedDepreciation", true};
    add(FieldScope::Task, "yearDepreciation", "연수상각", CustomFieldType::Number, "SYD(100,10,5,1)");
    add(FieldScope::Task, "lastDepreciation", "마지막상각", CustomFieldType::Number, "SYD(100,10,5,5)");
    add(FieldScope::Task, "negativeDepreciation", "음수상각", CustomFieldType::Number, "SLN(10,100,5)");
    add(FieldScope::Task, "fractionDepreciation", "소수상각", CustomFieldType::Number, "SLN(1,0,3)");
    add(FieldScope::Task, "depreciationError", "상각오류", CustomFieldType::Text, "SLN(1,0,0)");
    add(FieldScope::Task, "depreciationNull", "상각미설정", CustomFieldType::Text,
        "IIf(IsNull([미설정]),Null,SLN(1,0,1))");
    add(FieldScope::Task, "depreciationSummary", "상각요약", CustomFieldType::Number, "SLN(100,0,5)");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back(
        {"depreciationNative", "상각혼합식", CustomFieldType::Number, "", {}, "[field:depreciation]*2"});
    add(FieldScope::Project, "depreciationProject", "프로젝트상각", CustomFieldType::Number, "SLN([프로젝트수],0,2)");
    add(FieldScope::Resource, "depreciationResource", "자원상각", CustomFieldType::Number, "SLN(Len([Name]),0,2)");
    p.assignmentFields.push_back({"receivedDepreciation", "상각전파", CustomFieldType::Number, "", {}});
    add(FieldScope::Assignment, "depreciationAssignment", "배정상각", CustomFieldType::Number,
        "SLN([Task.입력수],0,2)");
    p.tasks[0].attributes.push_back({"field:depreciation", "99"});
    p.tasks[0].attributes.push_back({"field:depreciationError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:depreciation", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:depreciation", &p.tasks[0]);
}
void verify(const Project &p) {
    for (const auto &[id, a, b] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"yearDepreciation", "30", "30"},
             {"lastDepreciation", "6", "6"},
             {"negativeDepreciation", "-18", "-18"},
             {"fractionDepreciation", "0.333333", "0.333333"}})
        check(value(p, id) == a && value(p, id, 1) == b, "depreciation values");
    check(value(p, "depreciation") == (p.currency == "EUR" ? "2.468" : "4.936") &&
              value(p, "depreciation", 1) == (p.currency == "EUR" ? "0.285714" : "0.571429"),
          "FX recompute");
    check(value(p, "depreciationNative") == (p.currency == "EUR" ? "4.936" : "9.872") &&
              value(p, "depreciationNative", 1) == (p.currency == "EUR" ? "0.571428" : "1.142858"),
          "native numeric chain");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:depreciationError", &p.tasks[0]);
    const auto n = xmlFormulaValue(p, FieldScope::Task, "field:depreciationNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error versus Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:depreciationProject") == "8" &&
              resolvedAttributeValue(p, p.resources[0], "field:depreciationResource") == "1",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:depreciationAssignment") == "1.25" &&
              roll.value(p.assignments[1], "field:depreciationAssignment") == "1.75" &&
              roll.value(p.assignments[1], "field:receivedDepreciation") ==
                  (p.currency == "EUR" ? "0.285714" : "0.571429"),
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:depreciationSummary").value == "20", "summary");
}
void vectors() {
    QJsonArray rows;
    const auto add = [&](const char *fn, std::int64_t cost, std::int64_t salvage, std::int64_t life,
                         std::int64_t period) {
        QJsonObject row{{"function", fn},
                        {"cost", QString::number(cost)},
                        {"salvage", QString::number(salvage)},
                        {"life", QString::number(life)},
                        {"period", QString::number(period)}};
        try {
            row["value"] = QString::number(xmlDepreciation(fn, cost, salvage, life, period));
        } catch (const AppError &) {
            row["error"] = true;
        }
        rows.append(row);
    };
    for (std::int64_t cost : {0LL, 1LL, 500000LL, 1000000LL, 12340000LL, 100000000LL, 9000000000000000000LL})
        for (std::int64_t salvage : {0LL, 1LL, 500000LL, 20000000LL, 9000000000000000000LL})
            for (std::int64_t life : {1LL, 500000LL, 1000000LL, 2500000LL, 5000000LL, 9000000000000000000LL}) {
                add("sln", cost, salvage, life, 1000000);
                for (auto period : {std::int64_t(1), std::max(std::int64_t(1), life / 2), life})
                    add("syd", cost, salvage, life, period);
            }
    std::uint64_t state = 325091;
    for (int i = 0; i < 4096; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto cost = std::int64_t(state % 1000000000), salvage = std::int64_t((state >> 12) % 1000000000);
        const auto life = 1000000 + std::int64_t((state >> 22) % 999000000),
                   period = 1 + std::int64_t((state >> 5) % std::uint64_t(life));
        add("sln", cost, salvage, life, 1000000);
        add("syd", cost, salvage, life, period);
    }
    check(rows.size() == 9032, "extreme, fractional and mixed depreciation vectors");
    save(output() + "/vectors.json", QJsonDocument(rows).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlDepreciation("sln", 1000000, 0, 1000000);
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"SLN(100,10,5)", "18"},
        {"SYD(100,10,5,1)", "30"},
        {"SYD(100,10,5,5)", "6"},
        {"SLN(0,0,5)", "0"},
        {"SLN(10,100,5)", "-18"},
        {"SYD(10,100,5,1)", "-30"},
        {"SLN(1,0,3)", "0.333333"},
        {"SLN(2,0,3)", "0.666667"},
        {"SLN(0.000001,0,2)", "0.000001"},
        {"SLN(0,0.000001,2)", "-0.000001"},
        {"SLN(12.34,0,2.5)", "4.936"},
        {"SYD(12,0,2.5,0.5)", "8.228571"},
        {"SYD(12,0,2.5,2.5)", "2.742857"},
        {"SLN(1,0,0.5)", "2"},
        {"SLN(9000000000000,9000000000000,0.000001)", "0"},
        {"SLN(9000000000000,0,1)", "9000000000000"},
        {"SLN(9000000000000,8999999999999.999999,1)", "0.000001"},
        {"SYD(9000000000000,8999999999999.999999,1,1)", "0.000001"},
        {"SYD(1,0,9000000000000,9000000000000)", "0"},
        {"sLn(100;10;5)", "18"},
        {"SLN(CDbl(\"100\"),0,5)", "20"},
        {"SLN(False,0,1)", "0"},
        {"Round(SLN(1,0,3),2)", "0.33"},
        {"IIf(True,SLN(100,10,5),0)", "18"},
        {"CStr(SLN(100,10,5))", "18"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto result = eval(probe, source);
        if (result.value != expected)
            std::cerr << source << " expected " << expected << " got " << result.value.value_or(result.issue) << '\n';
        check(result.value == expected, "depreciation scalar");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*result.value)}});
    }
    for (const auto *source : {"SLN(-1,0,1)",
                               "SLN(1,-1,1)",
                               "SLN(1,0,-1)",
                               "SLN(1,0,0)",
                               "SYD(1,0,1,0)",
                               "SYD(1,0,1,-1)",
                               "SYD(1,0,1,2)",
                               "SYD(1,-1,1,1)",
                               "SLN(Null,0,1)",
                               "SLN(1,Null,1)",
                               "SLN(1,0,Null)",
                               "SYD(1,0,1,Null)",
                               "SLN(\"1\",0,1)",
                               "SLN(#2024-01-01#,0,1)",
                               "SLN(True,0,1)",
                               "SLN(9000000000000,0,0.000001)",
                               "SYD(9000000000000,0,0.5,0.5)",
                               "SLN(1,0)",
                               "SYD(1,0,1)",
                               "SLN(1,0,1,1)",
                               "SLN1(1,0,1)",
                               "IIf(True,1,SLN(1,0,Null))"}) {
        const auto result = eval(probe, source);
        check(!result.value && !result.missing && !result.issue.empty(),
              "depreciation domain/type/Null/overflow/eager error");
    }
    check(eval(probe, "IIf(True,Null,SLN(1,0,1))").missing, "explicit missing result");
    rejects([&] { xmlDepreciation("sln", -1, 0, 1000000); });
    rejects([&] { xmlDepreciation("syd", 1000000, 0, 1000000, 2000000); });
    rejects([&] { xmlDepreciation("other", 1000000, 0, 1000000); });
    check(!xmlDepreciationFormula({"\"SLN(1,0,1)\"", {}, true}) &&
              !xmlDepreciationFormula({"[field:SYD\\]X]", {}, true}) &&
              !xmlDepreciationFormula({"SLN1(1,0,1)", {}, true}) && xmlDepreciationFormula({"sYd (1,0,1,1)", {}, true}),
          "scanner boundaries");
    check(nativeSchemaFor(p) == 72 && json::decodeProject(json::encodeProject(p), 72) == p, "schema72");
    rejects([&] { json::decodeProject(json::encodeProject(p), 71); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlDepreciationFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "SLN(9,0,3)");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:depreciation", changed);
    check(value(*doc.project(), "depreciation") == "3", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "depreciation") == "3", "redo");
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
    check(json::decodeProject(json::encodeProject(*doc.project()), 72) == *doc.project(), "FX archives72");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 72 && json::decodeProject(json::encodeProject(frozen), 72) == frozen,
          "frozen-only72");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 71); });
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
    check(json::decodeProject(json::encodeProject(migrated), 72) == migrated, "ID archives72");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery72");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:depreciation", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "operators.xml");
    check(hasXmlDepreciationFunctions(imported.project, true), "XML active");
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
    options.reportId = "xml-depreciation";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS SLN/SYD: precedence/rounding/domain/range/Null, scopes/summary/roll-down, "
                 "schema72/archives/recovery/XML/XER/reports; 9032 depreciation vectors\n";
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
    check(model.data(model.index(0, model.columnForKey("field:depreciation"))).toString() == "4.936",
          "depreciation table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlDepreciationFunction");
            check(fn && fn->count() == 4, "depreciation examples");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlDepreciationFormula")->click();
            check(source->toPlainText() == "SLN(100,10,5)", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "18")
                        found = true;
            check(found, "depreciation preview");
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
    editXmlFormula(doc, FieldScope::Task, "field:depreciation", nullptr);
    check(done && value(*doc.project(), "depreciation") == "18", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("SLN(Null,0,1)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:depreciation", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS SLN/SYD UI: examples/async preview/save/undo/cancel and small layout\n";
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
