#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlColors.h"
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
    return QDir::currentPath() + "/xml-colors-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-colors";
    p.name = "XML 색상 코드";
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "color", "계산색상", CustomFieldType::Number, "RGB([입력수],1,2)");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedColor", true};
    add(FieldScope::Task, "clampedColor", "성분상한색상", CustomFieldType::Number, "RGB(256,1,2)");
    add(FieldScope::Task, "paletteColor", "팔레트색상", CustomFieldType::Number, "QBColor(14)");
    add(FieldScope::Task, "colorHex", "색상진수문자", CustomFieldType::Text, "Hex(RGB(1,2,3))");
    add(FieldScope::Task, "colorCost", "금액성분색상", CustomFieldType::Number, "RGB([입력금액],0,0)");
    add(FieldScope::Task, "colorError", "색상오류", CustomFieldType::Text, "RGB(-1,0,0)");
    add(FieldScope::Task, "colorNull", "색상미설정", CustomFieldType::Text, "IIf(IsNull([미설정]),Null,QBColor(0))");
    add(FieldScope::Task, "colorSummary", "색상요약", CustomFieldType::Number, "QBColor(15)");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"colorNative", "색상혼합식", CustomFieldType::Number, "", {}, "[field:color]*2"});
    add(FieldScope::Project, "colorProject", "프로젝트색상", CustomFieldType::Number, "RGB([프로젝트수],0,0)");
    add(FieldScope::Resource, "colorResource", "자원색상", CustomFieldType::Number, "RGB(Len([Name]),0,0)");
    p.assignmentFields.push_back({"receivedColor", "색상전파", CustomFieldType::Number, "", {}});
    add(FieldScope::Assignment, "colorAssignment", "배정색상", CustomFieldType::Number, "RGB([Task.입력수],0,0)");
    p.tasks[0].attributes.push_back({"field:color", "99"});
    p.tasks[0].attributes.push_back({"field:colorError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:color", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:color", &p.tasks[0]);
}
void verify(const Project &p) {
    for (const auto &[id, a, b] :
         std::vector<std::tuple<std::string, std::string, std::string>>{{"color", "131330", "131332"},
                                                                        {"clampedColor", "131583", "131583"},
                                                                        {"paletteColor", "65535", "65535"},
                                                                        {"colorHex", "30201", "30201"},
                                                                        {"colorNative", "262660", "262664"}})
        check(value(p, id) == a && value(p, id, 1) == b, "color values and native chain");
    check(value(p, "colorCost") == (p.currency == "EUR" ? "6" : "12") &&
              value(p, "colorCost", 1) == (p.currency == "EUR" ? "1" : "2"),
          "FX recompute");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:colorError", &p.tasks[0]);
    const auto n = xmlFormulaValue(p, FieldScope::Task, "field:colorNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error versus Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:colorProject") == "16" &&
              resolvedAttributeValue(p, p.resources[0], "field:colorResource") == "2",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:colorAssignment") == "2" &&
              roll.value(p.assignments[1], "field:colorAssignment") == "4" &&
              roll.value(p.assignments[1], "field:receivedColor") == "131332",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:colorSummary").value == "16777215", "summary");
}
void vectors() {
    QJsonArray rows;
    const auto add = [&](int r, int g, int b) {
        rows.append(QJsonObject{
            {"function", "rgb"}, {"red", r}, {"green", g}, {"blue", b}, {"value", double(xmlRgbColor(r, g, b))}});
    };
    for (int r : {0, 1, 2, 127, 128, 254, 255, 256, 32767})
        for (int g : {0, 1, 2, 127, 128, 254, 255, 256, 32767})
            for (int b : {0, 1, 2, 127, 128, 254, 255, 256, 32767})
                add(r, g, b);
    for (int c = 0; c <= 255; ++c) {
        add(c, 0, 0);
        add(0, c, 0);
        add(0, 0, c);
    }
    std::uint64_t state = 344197;
    for (int i = 0; i < 4096; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        add(int(state % 512), int((state >> 10) % 512), int((state >> 20) % 32768));
    }
    for (int c = 0; c <= 15; ++c)
        rows.append(QJsonObject{{"function", "qbcolor"}, {"index", c}, {"value", double(xmlQbColor(c))}});
    check(rows.size() == 5609, "RGB boundaries, single channels, mixed colors and 16-color palette");
    save(output() + "/vectors.json", QJsonDocument(rows).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlRgbColor(1, 2, 3);
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {{"RGB(0,0,0)", "0"},
                                                                    {"RGB(255,0,0)", "255"},
                                                                    {"RGB(0,255,0)", "65280"},
                                                                    {"RGB(0,0,255)", "16711680"},
                                                                    {"RGB(255,255,255)", "16777215"},
                                                                    {"RGB(1,2,3)", "197121"},
                                                                    {"RGB(256,1,2)", "131583"},
                                                                    {"RGB(32767,1,2)", "131583"},
                                                                    {"RGB(255.5,0,0)", "255"},
                                                                    {"RGB(-0.5,0,0)", "0"},
                                                                    {"RGB(1.5,2.5,3.5)", "262658"},
                                                                    {"RGB(32767.499999,0,0)", "255"},
                                                                    {"RGB(False,0,0)", "0"},
                                                                    {"RGB(CInt(2.5),0,0)", "2"},
                                                                    {"RGB(CDbl(\"255\"),0,0)", "255"},
                                                                    {"QBColor(0)", "0"},
                                                                    {"QBColor(1)", "8388608"},
                                                                    {"QBColor(2)", "32768"},
                                                                    {"QBColor(7)", "12632256"},
                                                                    {"QBColor(8)", "8421504"},
                                                                    {"QBColor(15)", "16777215"},
                                                                    {"QBColor(2.5)", "32768"},
                                                                    {"QBColor(14.5)", "65535"},
                                                                    {"QBColor(15.499999)", "16777215"},
                                                                    {"QBColor(-0.5)", "0"},
                                                                    {"QBColor(False)", "0"},
                                                                    {"Hex(RGB(1,2,3))", "30201"},
                                                                    {"CStr(QBColor(15))", "16777215"},
                                                                    {"rGb(1;2;3)", "197121"},
                                                                    {"IIf(True,RGB(255,0,0),0)", "255"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto result = eval(probe, source);
        if (result.value != expected)
            std::cerr << source << " expected " << expected << " got " << result.value.value_or(result.issue) << '\n';
        check(result.value == expected, "color scalar");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*result.value)}});
    }
    for (const auto *source : {"RGB(-1,0,0)",      "RGB(0,-1,0)",
                               "RGB(0,0,-1)",      "RGB(32768,0,0)",
                               "RGB(32767.5,0,0)", "RGB(-0.500001,0,0)",
                               "RGB(Null,0,0)",    "RGB(0,Null,0)",
                               "RGB(0,0,Null)",    "RGB(\"1\",0,0)",
                               "RGB(True,0,0)",    "RGB(#2024-01-01#,0,0)",
                               "QBColor(-1)",      "QBColor(16)",
                               "QBColor(15.5)",    "QBColor(Null)",
                               "QBColor(\"1\")",   "QBColor(True)",
                               "RGB(1,2)",         "QBColor(1,2)",
                               "RGB1(1,2,3)",      "IIf(True,1,QBColor(Null))"}) {
        const auto result = eval(probe, source);
        check(!result.value && !result.missing && !result.issue.empty(), "color type/range/Null/eager error");
    }
    check(eval(probe, "IIf(True,Null,RGB(0,0,0))").missing, "explicit missing result");
    rejects([&] { xmlRgbColor(-1, 0, 0); });
    rejects([&] { xmlRgbColor(0, 32768, 0); });
    rejects([&] { xmlQbColor(-1); });
    rejects([&] { xmlQbColor(16); });
    check(!xmlColorFormula({"\"RGB(1,2,3)\"", {}, true}) && !xmlColorFormula({"[field:QBColor\\]X]", {}, true}) &&
              !xmlColorFormula({"RGB1(1,2,3)", {}, true}) && xmlColorFormula({"qBcOlOr (1)", {}, true}),
          "scanner boundaries");
    check(nativeSchemaFor(p) == 70 && json::decodeProject(json::encodeProject(p), 70) == p, "schema70");
    rejects([&] { json::decodeProject(json::encodeProject(p), 69); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlColorFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "RGB(9,0,0)");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:color", changed);
    check(value(*doc.project(), "color") == "9", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "color") == "9", "redo");
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
    check(json::decodeProject(json::encodeProject(*doc.project()), 70) == *doc.project(), "FX archives70");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 70 && json::decodeProject(json::encodeProject(frozen), 70) == frozen,
          "frozen-only70");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 69); });
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
    check(json::decodeProject(json::encodeProject(migrated), 70) == migrated, "ID archives70");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery70");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:color", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "operators.xml");
    check(hasXmlColorFunctions(imported.project, true), "XML active");
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
    options.reportId = "xml-colors";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS RGB/QBColor: precedence/rounding/domain/range/Null, scopes/summary/roll-down, "
                 "schema70/archives/recovery/XML/XER/reports; 5609 color vectors\n";
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
    check(model.data(model.index(0, model.columnForKey("field:color"))).toString() == "131330", "colors table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlColorFunction");
            check(fn && fn->count() == 4, "colors examples");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlColorFormula")->click();
            check(source->toPlainText() == "RGB(255,0,0)", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "255")
                        found = true;
            check(found, "colors preview");
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
    editXmlFormula(doc, FieldScope::Task, "field:color", nullptr);
    check(done && value(*doc.project(), "color") == "255", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("RGB(Null,0,0)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:color", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS RGB/QBColor UI: examples/async preview/save/undo/cancel and small layout\n";
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
