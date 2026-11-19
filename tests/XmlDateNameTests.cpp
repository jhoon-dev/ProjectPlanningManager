#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlDateNames.h"
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
    return QDir::currentPath() + "/xml-date-names-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-date-names";
    p.name = "XML 월·요일 이름";
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "month", "월이름", CustomFieldType::Text, "MonthName(Month([입력일시]))");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedDateName", true};
    add(FieldScope::Task, "shortMonth", "월약어", CustomFieldType::Text, "MonthName([입력수],True)");
    add(FieldScope::Task, "weekdayName", "요일이름", CustomFieldType::Text,
        "WeekdayName(Weekday([입력일시],2),False,2)");
    add(FieldScope::Task, "nameLength", "월이름길이", CustomFieldType::Number, "Len(MonthName([입력수]))");
    add(FieldScope::Task, "costName", "금액월이름", CustomFieldType::Text, "MonthName(Int([입력금액]))");
    add(FieldScope::Task, "nameError", "이름오류", CustomFieldType::Text, "MonthName(13)");
    add(FieldScope::Task, "nameNull", "이름미설정", CustomFieldType::Text, "IIf(IsNull([미설정]),Null,MonthName(1))");
    add(FieldScope::Task, "nameSummary", "이름요약", CustomFieldType::Text, "WeekdayName(1)");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"nameNative", "이름혼합식", CustomFieldType::Number, "", {}, "[field:nameLength]*2"});
    add(FieldScope::Project, "nameProject", "프로젝트월이름", CustomFieldType::Text, "MonthName([프로젝트수]-4)");
    add(FieldScope::Resource, "nameResource", "자원요일이름", CustomFieldType::Text, "WeekdayName(Len([Name]))");
    p.assignmentFields.push_back({"receivedDateName", "월이름전파", CustomFieldType::Text, "", {}});
    add(FieldScope::Assignment, "nameAssignment", "배정월이름", CustomFieldType::Text, "MonthName([Task.입력수])");
    p.tasks[0].attributes.push_back({"field:month", "saved"});
    p.tasks[0].attributes.push_back({"field:nameError", "777"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source) {
    auto *f = const_cast<CustomField *>(customField(p, "field:month", FieldScope::Task));
    f->type = CustomFieldType::Text;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:month", &p.tasks[0]);
}
void verify(const Project &p) {
    for (const auto &[id, a, b] :
         std::vector<std::tuple<std::string, std::string, std::string>>{{"month", "November", "November"},
                                                                        {"shortMonth", "Feb", "Apr"},
                                                                        {"weekdayName", "Friday", "Friday"},
                                                                        {"nameLength", "8", "5"},
                                                                        {"nameNative", "16", "10"}})
        check(value(p, id) == a && value(p, id, 1) == b, "date names and native chain");
    check(value(p, "costName") == (p.currency == "EUR" ? "June" : "December") &&
              value(p, "costName", 1) == (p.currency == "EUR" ? "January" : "February"),
          "FX recompute");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:nameError", &p.tasks[0]);
    const auto n = xmlFormulaValue(p, FieldScope::Task, "field:nameNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error versus Null never snapshot");
    check(resolvedProjectAttributeValue(p, "field:nameProject") == "December" &&
              resolvedAttributeValue(p, p.resources[0], "field:nameResource") == "Monday",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:nameAssignment") == "February" &&
              roll.value(p.assignments[1], "field:nameAssignment") == "April" &&
              roll.value(p.assignments[1], "field:receivedDateName") == "November",
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:nameSummary").value == "Sunday", "summary");
}
void vectors() {
    QJsonArray rows;
    for (int month = 1; month <= 12; ++month)
        for (bool shortName : {false, true})
            rows.append(QJsonObject{{"kind", "month"},
                                    {"index", month},
                                    {"abbreviate", shortName},
                                    {"firstDay", 1},
                                    {"value", QString::fromStdString(xmlMonthName(month, shortName))}});
    for (int first = 0; first <= 7; ++first)
        for (int day = 1; day <= 7; ++day)
            for (bool shortName : {false, true})
                rows.append(QJsonObject{{"kind", "weekday"},
                                        {"index", day},
                                        {"abbreviate", shortName},
                                        {"firstDay", first},
                                        {"value", QString::fromStdString(xmlWeekdayName(day, shortName, first))}});
    using namespace std::chrono;
    const auto two = [](unsigned n) { return std::string(n < 10 ? "0" : "") + std::to_string(n); };
    for (auto d = sys_days(year(2024) / 1 / 1); d < sys_days(year(2025) / 1 / 1); d += days(1)) {
        const year_month_day ymd(d);
        const auto date =
            std::to_string(int(ymd.year())) + '-' + two(unsigned(ymd.month())) + '-' + two(unsigned(ymd.day()));
        for (int first = 0; first <= 7; ++first)
            for (bool shortName : {false, true}) {
                const auto index = int(xmlDatePart("w", date, first));
                rows.append(QJsonObject{
                    {"kind", "date"},
                    {"date", QString::fromStdString(date)},
                    {"index", index},
                    {"month", int(unsigned(ymd.month()))},
                    {"abbreviate", shortName},
                    {"firstDay", first},
                    {"value", QString::fromStdString(xmlWeekdayName(index, shortName, first))},
                    {"monthValue", QString::fromStdString(xmlMonthName(int(unsigned(ymd.month())), shortName))}});
            }
    }
    check(rows.size() == 5992, "month/week options and complete leap year");
    save(output() + "/vectors.json", QJsonDocument(rows).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool stopped = false;
        try {
            (void)xmlMonthName(1);
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
        check(stopped, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"MonthName(1)", "January"},
        {"MonthName(12)", "December"},
        {"MonthName(11,True)", "Nov"},
        {"MonthName(5,True)", "May"},
        {"MonthName(1.5)", "February"},
        {"MonthName(2.5)", "February"},
        {"MonthName(11.5)", "December"},
        {"MonthName(12.5)", "December"},
        {"MonthName(0.5+0.1)", "January"},
        {"MonthName(1,2)", "Jan"},
        {"MonthName(1,0)", "January"},
        {"WeekdayName(1)", "Sunday"},
        {"WeekdayName(7)", "Saturday"},
        {"WeekdayName(1,False,2)", "Monday"},
        {"WeekdayName(7,True,2)", "Sun"},
        {"WeekdayName(1,True,0)", "Sun"},
        {"WeekdayName(1.5,True,1.5)", "Tue"},
        {"WeekdayName(7.49,True,7)", "Fri"},
        {"WeekdayName(1,False,7.49)", "Saturday"},
        {"WeekdayName(1,False,0.5)", "Sunday"},
        {"MonthName(Month(#2024-02-29#))", "February"},
        {"WeekdayName(Weekday(#2024-02-29#,2),False,2)", "Thursday"},
        {"MonthName(CInt(2.5),True)", "Feb"},
        {"Len(MonthName(2))", "8"},
        {"IIf(True,MonthName(1),MonthName(2))", "January"},
        {"monthNAME(3;True)", "Mar"},
        {"WeekdayName(1,True,2)&\"/\"&MonthName(11,True)", "Mon/Nov"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto result = eval(probe, source);
        if (result.value != expected)
            std::cerr << source << " expected " << expected << " got " << result.value.value_or(result.issue) << '\n';
        check(result.value == expected, "date-name scalar");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*result.value)}});
    }
    for (const auto *source : {"MonthName(0)",
                               "MonthName(13)",
                               "MonthName(12.500001)",
                               "MonthName(-1)",
                               "MonthName(Null)",
                               "MonthName(1,Null)",
                               "MonthName(\"1\")",
                               "MonthName(1,\"True\")",
                               "MonthName(#2024-01-01#)",
                               "MonthName(True)",
                               "WeekdayName(0)",
                               "WeekdayName(8)",
                               "WeekdayName(7.5)",
                               "WeekdayName(1,False,7.5)",
                               "WeekdayName(1,False,-0.6)",
                               "WeekdayName(Null)",
                               "WeekdayName(1,Null)",
                               "WeekdayName(1,False,Null)",
                               "WeekdayName(1,False,\"2\")",
                               "MonthName()",
                               "WeekdayName(1,False,1,1)",
                               "MonthName1(1)",
                               "IIf(True,1,MonthName(13))"}) {
        const auto result = eval(probe, source);
        check(!result.value && !result.missing && !result.issue.empty(), "date-name type/range/Null/eager error");
    }
    check(eval(probe, "IIf(True,Null,MonthName(1))").missing, "explicit missing result");
    rejects([&] { xmlMonthName(0); });
    rejects([&] { xmlMonthName(13); });
    rejects([&] { xmlWeekdayName(0); });
    rejects([&] { xmlWeekdayName(8); });
    rejects([&] { xmlWeekdayName(1, false, -1); });
    rejects([&] { xmlWeekdayName(1, false, 8); });
    check(!xmlDateNameFormula({"\"MonthName(1)\"", {}, true}) &&
              !xmlDateNameFormula({"[field:WeekdayName\\]X]", {}, true}) &&
              !xmlDateNameFormula({"MonthName1(1)", {}, true}) && xmlDateNameFormula({"mOnThNaMe (1)", {}, true}),
          "scanner boundaries");
    check(nativeSchemaFor(p) == 67 && json::decodeProject(json::encodeProject(p), 67) == p, "schema67");
    rejects([&] { json::decodeProject(json::encodeProject(p), 66); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlDateNameFormula(*f.xmlFormula))
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
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "MonthName(1)");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:month", changed);
    check(value(*doc.project(), "month") == "January", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "month") == "January", "redo");
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
    check(json::decodeProject(json::encodeProject(*doc.project()), 67) == *doc.project(), "FX archives67");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 67 && json::decodeProject(json::encodeProject(frozen), 67) == frozen,
          "frozen-only67");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 66); });
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
    check(json::decodeProject(json::encodeProject(migrated), 67) == migrated, "ID archives67");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-14T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery67");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:month", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "operators.xml");
    check(hasXmlDateNameFunctions(imported.project, true), "XML active");
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
    options.reportId = "xml-date-names";
    options.generatedAt = "2026-11-14T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout << "PASS MonthName/WeekdayName: precedence/rounding/domain/range/Null, scopes/summary/roll-down, "
                 "schema67/archives/recovery/XML/XER/reports; 5992 date-name vectors\n";
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
    check(model.data(model.index(0, model.columnForKey("field:month"))).toString() == "November",
          "date-name table value");
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
            auto *fn = d->findChild<QComboBox *>("xmlDateNameFunction");
            check(fn && fn->count() == 4, "date-name examples");
            source->clear();
            fn->setCurrentIndex(0);
            d->findChild<QPushButton *>("insertXmlDateNameFormula")->click();
            check(source->toPlainText() == "MonthName(11)", "insert example");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text() == "November")
                        found = true;
            check(found, "date-name preview");
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
    editXmlFormula(doc, FieldScope::Task, "field:month", nullptr);
    check(done && value(*doc.project(), "month") == "November", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("MonthName(13)");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:month", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS MonthName/WeekdayName UI: examples/async preview/save/undo/cancel and small layout\n";
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
