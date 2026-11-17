#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlDateDisplay.h"
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
    return QDir::currentPath() + "/xml-date-display-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-date-display";
    p.name = "XML 날짜·시각 표시";
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "display", "날짜시각표시", CustomFieldType::Text, "FormatDateTime([입력일시])");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedDateDisplay", true};
    add(FieldScope::Task, "longDate", "긴날짜표시", CustomFieldType::Text, "FormatDateTime([입력일시],1)");
    add(FieldScope::Task, "shortTime", "짧은시각표시", CustomFieldType::Text, "FormatDateTime([입력일시],4)");
    add(FieldScope::Task, "displayLength", "시각표시길이", CustomFieldType::Number,
        "Len(FormatDateTime([입력일시],4))");
    add(FieldScope::Task, "costDisplay", "금액날짜표시", CustomFieldType::Text,
        "FormatDateTime(DateAdd(\"d\",Int([입력금액]),#2024-01-01#),2)");
    add(FieldScope::Task, "displayError", "표시오류", CustomFieldType::Text, "FormatDateTime([입력일시],5)");
    add(FieldScope::Task, "displayNull", "표시미설정", CustomFieldType::Text,
        "IIf(IsNull([미설정]),Null,FormatDateTime(#2024-01-01#))");
    add(FieldScope::Task, "displaySummary", "표시요약", CustomFieldType::Text, "FormatDateTime(#2024-02-29#,2)");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back(
        {"displayNative", "표시혼합식", CustomFieldType::Number, "", {}, "[field:displayLength]*2"});
    add(FieldScope::Project, "displayProject", "프로젝트날짜표시", CustomFieldType::Text,
        "FormatDateTime(DateAdd(\"d\",[프로젝트수],#2024-01-01#),2)");
    add(FieldScope::Resource, "displayResource", "자원날짜표시", CustomFieldType::Text,
        "FormatDateTime(DateAdd(\"d\",Len([Name]),#2024-01-01#),2)");
    p.assignmentFields.push_back({"receivedDateDisplay", "날짜표시전파", CustomFieldType::Text, "", {}});
    add(FieldScope::Assignment, "displayAssignment", "배정날짜표시", CustomFieldType::Text,
        "FormatDateTime([Task.입력일시],2)");
    p.tasks[0].attributes.push_back({"field:display", "saved"});
    p.tasks[0].attributes.push_back({"field:displayError", "777"});
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
    for (const auto &[id, a, b] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"display", "11/13/2026 9:30:12 AM", "11/13/2026 9:30:12 AM"},
             {"longDate", "Friday, November 13, 2026", "Friday, November 13, 2026"},
             {"shortTime", "09:30", "09:30"},
             {"displayLength", "5", "5"},
             {"displayNative", "10", "10"}})
        check(value(p, id) == a && value(p, id, 1) == b, "date display and native chain");
    check(value(p, "costDisplay") == (p.currency == "EUR" ? "1/7/2024" : "1/13/2024") &&
              value(p, "costDisplay", 1) == (p.currency == "EUR" ? "1/2/2024" : "1/3/2024"),
          "FX recompute");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:displayError", &p.tasks[0]);
    const auto n = xmlFormulaValue(p, FieldScope::Task, "field:displayNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error versus Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:displayProject") == "1/17/2024" &&
              resolvedAttributeValue(p, p.resources[0], "field:displayResource") == "1/3/2024",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:displayAssignment") == "11/13/2026" &&
              roll.value(p.assignments[1], "field:displayAssignment") == "11/13/2026" &&
              roll.value(p.assignments[1], "field:receivedDateDisplay") == "11/13/2026 9:30:12 AM",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:displaySummary").value == "2/29/2024", "summary");
}
void vectors() {
    QJsonArray rows;
    const auto add = [&](const std::string &date) {
        for (int format = 0; format <= 4; ++format)
            rows.append(QJsonObject{{"date", QString::fromStdString(date)},
                                    {"format", format},
                                    {"value", QString::fromStdString(xmlFormatDateTime(date, format))}});
    };
    using namespace std::chrono;
    const auto two = [](unsigned n) { return std::string(n < 10 ? "0" : "") + std::to_string(n); };
    for (auto d = sys_days(year(2024) / 1 / 1); d < sys_days(year(2025) / 1 / 1); d += days(1)) {
        const year_month_day ymd(d);
        const auto date =
            std::to_string(int(ymd.year())) + '-' + two(unsigned(ymd.month())) + '-' + two(unsigned(ymd.day()));
        for (const auto *clock : {"T00:00:00", "T09:30:12", "T13:04:05", "T23:59:59"})
            add(date + clock);
    }
    for (const auto *date : {"1899-12-29T00:00:00", "1899-12-29T13:04:05", "1899-12-30T00:00:00", "1899-12-30T13:04:05",
                             "1899-12-31T00:00:00", "1899-12-31T13:04:05", "1601-01-01T00:00:00", "9999-12-31T23:59:59",
                             "1900-03-01T12:00:00", "2000-02-29T12:00:00"})
        add(date);
    check(rows.size() == 7370, "leap year, four clocks, five formats and date boundaries");
    save(output() + "/vectors.json", QJsonDocument(rows).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlFormatDateTime("2024-01-01");
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"FormatDateTime(#2024-02-29#)", "2/29/2024"},
        {"FormatDateTime(#2024-02-29T13:04:05#)", "2/29/2024 1:04:05 PM"},
        {"FormatDateTime(#2024-02-29#,1)", "Thursday, February 29, 2024"},
        {"FormatDateTime(#2024-02-29#,2)", "2/29/2024"},
        {"FormatDateTime(#2024-02-29T13:04:05#,3)", "1:04:05 PM"},
        {"FormatDateTime(#2024-02-29T13:04:05#,4)", "13:04"},
        {"FormatDateTime(#1899-12-30#)", "12:00:00 AM"},
        {"FormatDateTime(TimeSerial(13,4,5))", "1:04:05 PM"},
        {"FormatDateTime(TimeValue(\"13:04:05\"),4)", "13:04"},
        {"FormatDateTime(#1899-12-29T13:04:05#)", "12/29/1899 1:04:05 PM"},
        {"FormatDateTime(#2024-02-29#,1.5)", "2/29/2024"},
        {"FormatDateTime(#2024-02-29#,2.5)", "2/29/2024"},
        {"FormatDateTime(#2024-02-29T13:04:05#,3.5)", "13:04"},
        {"FormatDateTime(#2024-02-29T13:04:05#,4.5)", "13:04"},
        {"FormatDateTime(#2024-02-29#,-0.5)", "2/29/2024"},
        {"FormatDateTime(\"2024-02-29\",1)", "Thursday, February 29, 2024"},
        {"FormatDateTime(CDate(\"2024-02-29T13:04:05\"),3)", "1:04:05 PM"},
        {"formatDATETIME(#2024-02-29#;2)", "2/29/2024"},
        {"Len(FormatDateTime(#2024-02-29T13:04:05#,4))", "5"},
        {"IIf(True,FormatDateTime(#2024-02-29#,2),\"x\")", "2/29/2024"},
        {"FormatDateTime(#2024-02-29T00:00:00.6#)", "2/29/2024"},
        {"FormatDateTime(#2024-02-29T23:59:59.999999#,4)", "23:59"},
        {"FormatDateTime(#0001-01-01#,2)", "1/1/0001"},
        {"FormatDateTime(#1600-01-01#,2)", "1/1/1600"},
        {"FormatDateTime(#9999-12-31T23:59:59.999999#,3)", "11:59:59 PM"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto result = eval(probe, source);
        if (result.value != expected)
            std::cerr << source << " expected " << expected << " got " << result.value.value_or(result.issue) << '\n';
        check(result.value == expected, "date-display scalar");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*result.value)}});
    }
    for (const auto *source :
         {"FormatDateTime(Null)", "FormatDateTime(#2024-01-01#,Null)", "FormatDateTime(1)", "FormatDateTime(True)",
          "FormatDateTime(\"13:04:05\")", "FormatDateTime(\"02/29/2024\")", "FormatDateTime(\"2023-02-29\")",
          "FormatDateTime(\"2024-01-01T00:00:00Z\")", "FormatDateTime(#2024-01-01#,-1)",
          "FormatDateTime(#2024-01-01#,5)", "FormatDateTime(#2024-01-01#,4.500001)",
          "FormatDateTime(#2024-01-01#,-0.500001)", "FormatDateTime(#2024-01-01#,\"2\")", "FormatDateTime()",
          "FormatDateTime(#2024-01-01#,1,1)", "FormatDateTime1(#2024-01-01#)", "IIf(True,1,FormatDateTime(Null))"}) {
        const auto result = eval(probe, source);
        check(!result.value && !result.missing && !result.issue.empty(), "date-display type/range/Null/eager error");
    }
    check(eval(probe, "IIf(True,Null,FormatDateTime(#2024-01-01#))").missing, "explicit missing result");
    rejects([&] { xmlFormatDateTime("2024-01-01", -1); });
    rejects([&] { xmlFormatDateTime("2024-01-01", 5); });
    rejects([&] { xmlFormatDateTime("2023-02-29"); });
    rejects([&] { xmlFormatDateTime(std::string(4096, 'a')); });
    check(!xmlDateDisplayFormula({"\"FormatDateTime(1)\"", {}, true}) &&
              !xmlDateDisplayFormula({"[field:FormatDateTime\\]X]", {}, true}) &&
              !xmlDateDisplayFormula({"FormatDateTime1(1)", {}, true}) &&
              xmlDateDisplayFormula({"fOrMaTdAtEtImE (1)", {}, true}),
          "scanner boundaries");
    check(nativeSchemaFor(p) == 68 && json::decodeProject(json::encodeProject(p), 68) == p, "schema68");
    rejects([&] { json::decodeProject(json::encodeProject(p), 67); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlDateDisplayFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "FormatDateTime(#2024-01-01#,2)");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:display", changed);
    check(value(*doc.project(), "display") == "1/1/2024", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "display") == "1/1/2024", "redo");
    doc.undo();
    auto renamed = *customField(*doc.project(), "field:when");
    renamed.name = "명시 일시 입력";
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
    check(json::decodeProject(json::encodeProject(*doc.project()), 68) == *doc.project(), "FX archives68");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 68 && json::decodeProject(json::encodeProject(frozen), 68) == frozen,
          "frozen-only68");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 67); });
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
    check(json::decodeProject(json::encodeProject(migrated), 68) == migrated, "ID archives68");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery68");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:display", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "operators.xml");
    check(hasXmlDateDisplayFunctions(imported.project, true), "XML active");
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
    options.reportId = "xml-date-display";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS FormatDateTime: precedence/rounding/domain/range/Null, scopes/summary/roll-down, "
                 "schema68/archives/recovery/XML/XER/reports; 7370 date-display vectors\n";
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
    check(model.data(model.index(0, model.columnForKey("field:display"))).toString() == "11/13/2026 9:30:12 AM",
          "date-display table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlDateDisplayFunction");
            check(fn && fn->count() == 4, "date-display examples");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlDateDisplayFormula")->click();
            check(source->toPlainText() == "FormatDateTime(#2024-02-29T13:04:05#,3)", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "1:04:05 PM")
                        found = true;
            check(found, "date-display preview");
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
    check(done && value(*doc.project(), "display") == "1:04:05 PM", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("FormatDateTime(Null)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:display", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS FormatDateTime UI: examples/async preview/save/undo/cancel and small layout\n";
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
