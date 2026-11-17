#include "XmlAdditionalFunctionFixture.h"
#include "application/Document.h"
#include "calculation/Reports.h"
#include "domain/XmlDateFunctions.h"
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
    return QDir::currentPath() + "/xml-time-functions-qa";
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "evidence write");
}
Project fixture() {
    auto p = test::xmlAdditionalFunctionFixture();
    p.documentId = "xml-time-functions";
    p.name = "XML 시각 함수";
    p.customFields.push_back({"hours", "시간수", CustomFieldType::Number, "", {}});
    p.customFields.push_back({"minutes", "분수", CustomFieldType::Number, "", {}});
    p.customFields.push_back({"clock", "시각문자", CustomFieldType::Text, "", {}});
    for (int i = 0; i < 2; ++i) {
        for (auto &a : p.tasks[i].attributes)
            if (a.key == "field:when")
                a.value = i ? "2023-12-31T23:59:59.654321" : "2024-02-29T09:30:12.123456";
        p.tasks[i].attributes.push_back({"field:hours", i ? "-25" : "1.5"});
        p.tasks[i].attributes.push_back({"field:minutes", i ? "0" : "75"});
        p.tasks[i].attributes.push_back({"field:clock", i ? "0:30am" : "2:24PM"});
    }
    const auto add = [&](FieldScope scope, const std::string &id, const std::string &name, CustomFieldType type,
                         const std::string &source) {
        auto &fields = customFields(p, scope);
        fields.push_back({id, name, type, "", {}});
        fields.back().xmlFormula = bindXmlFormula(p, scope, source);
        fields.back().xmlFormula->enabled = true;
    };
    add(FieldScope::Task, "serial", "생성시각", CustomFieldType::Finish, "TimeSerial([시간수],[분수],0)");
    add(FieldScope::Task, "value", "추출시각", CustomFieldType::Finish, "TimeValue([입력일시])");
    customFields(p, FieldScope::Task).back().xmlRollDown = XmlRollDown{"receivedTime", true};
    add(FieldScope::Task, "parsed", "문자시각", CustomFieldType::Finish, "TimeValue([시각문자])");
    add(FieldScope::Task, "shown", "시각표시", CustomFieldType::Text, "Format(TimeValue([시각문자]),\"hh:nn:ss\")");
    add(FieldScope::Task, "folded", "음수시각", CustomFieldType::Finish, "TimeSerial(-25,0,0)");
    add(FieldScope::Task, "timeError", "시각오류", CustomFieldType::Finish, "TimeValue(\"2023-02-29 14:00\")");
    add(FieldScope::Task, "timeNull", "시각미설정", CustomFieldType::Finish, "TimeValue(Null)");
    add(FieldScope::Task, "timeHour", "추출시간수", CustomFieldType::Number, "Hour(TimeValue([입력일시]))");
    add(FieldScope::Task, "timeSummary", "시각요약", CustomFieldType::Finish, "TimeSerial(7,0,0)");
    customFields(p, FieldScope::Task).back().xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"timeNative", "시각혼합식", CustomFieldType::Number, "", {}});
    p.customFields.back().expression = "[field:timeHour]*10";
    add(FieldScope::Project, "timeProject", "프로젝트시각", CustomFieldType::Finish, "TimeSerial(0,75,0)");
    add(FieldScope::Resource, "timeResource", "자원시각", CustomFieldType::Finish, "TimeValue(\"4:35:17 PM\")");
    p.assignmentFields.push_back({"receivedTime", "시각전파", CustomFieldType::Finish, "", {}});
    add(FieldScope::Assignment, "timeAssignment", "배정시각", CustomFieldType::Finish, "TimeValue([Task.생성시각])");
    p.tasks[0].attributes.push_back({"field:serial", "1999-01-01T00:00:00"});
    p.tasks[0].attributes.push_back({"field:timeError", "2000-01-01T00:00:00"});
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &id, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + id);
}
XmlFormulaResult eval(Project &p, const std::string &source, CustomFieldType type = CustomFieldType::Finish) {
    auto *f = const_cast<CustomField *>(customField(p, "field:serial", FieldScope::Task));
    f->type = type;
    f->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    f->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:serial", &p.tasks[0]);
}
void verify(const Project &p) {
    check(value(p, "serial") == "1899-12-30T03:15:00" && value(p, "serial", 1) == "1899-12-29T01:00:00",
          "rounding, carry and negative OLE date");
    check(value(p, "value") == "1899-12-30T09:30:12.123456" && value(p, "value", 1) == "1899-12-30T23:59:59.654321",
          "strip date preserve micros");
    check(value(p, "parsed") == "1899-12-30T14:24:00" && value(p, "parsed", 1) == "1899-12-30T00:30:00" &&
              value(p, "shown") == "14:24:00",
          "AM/PM and Format chain");
    check(value(p, "folded") == "1899-12-29T01:00:00" && value(p, "timeNative") == "90" &&
              value(p, "timeNative", 1) == "230",
          "negative and native numeric chain");
    const auto e = xmlFormulaValue(p, FieldScope::Task, "field:timeError", &p.tasks[0]),
               n = xmlFormulaValue(p, FieldScope::Task, "field:timeNull", &p.tasks[0]);
    check(!e.value && !e.missing && !e.issue.empty() && !n.value && n.missing, "error and Null never snapshots");
    check(resolvedProjectAttributeValue(p, "field:timeProject") == "1899-12-30T01:15:00" &&
              resolvedAttributeValue(p, p.resources[0], "field:timeResource") == "1899-12-30T16:35:17",
          "project/resource");
    const RollDownValues roll(p);
    check(roll.value(p.assignments[0], "field:timeAssignment") == "1899-12-30T03:15:00" &&
              roll.value(p.assignments[1], "field:timeAssignment") == "1899-12-30T01:00:00" &&
              roll.value(p.assignments[1], "field:receivedTime") == value(p, "value", 1),
          "assignment/roll-down");
    check(RollUpValues(p).cell("", "field:timeSummary").value == "1899-12-30T07:00:00", "summary");
}
void vectors() {
    QJsonArray serials, values;
    std::uint64_t state = 173924;
    for (int i = 0; i < 1024; ++i) {
        const auto component = [&] {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return std::int64_t(state % 65536) - 32768;
        };
        const auto h = component(), m = component(), s = component();
        serials.append(QJsonObject{{"hour", qint64(h)},
                                   {"minute", qint64(m)},
                                   {"second", qint64(s)},
                                   {"value", QString::fromStdString(xmlTimeSerial(h, m, s))}});
        const int hour = int(state % 24), minute = int(state / 24 % 60), second = int(state / 1440 % 60),
                  micro = int(state / 86400 % 1000000);
        const auto clock = QString("%1:%2:%3.%4")
                               .arg(hour, 2, 10, QChar('0'))
                               .arg(minute, 2, 10, QChar('0'))
                               .arg(second, 2, 10, QChar('0'))
                               .arg(micro, 6, 10, QChar('0'));
        for (const auto &text : {clock, QString("2024-02-29T") + clock,
                                 QString("%1:%2:%3.%4 %5")
                                     .arg(hour % 12 ? hour % 12 : 12)
                                     .arg(minute, 2, 10, QChar('0'))
                                     .arg(second, 2, 10, QChar('0'))
                                     .arg(micro, 6, 10, QChar('0'))
                                     .arg(hour < 12 ? "AM" : "PM")})
            values.append(
                QJsonObject{{"input", text}, {"value", QString::fromStdString(xmlTimeValue(text.toStdString()))}});
    }
    save(output() + "/serials.json", QJsonDocument(serials).toJson());
    save(output() + "/values.json", QJsonDocument(values).toJson());
}
void contract() {
    {
        auto cancel = std::make_shared<CalculationControl>();
        EvaluationScope scope(cancel);
        cancel->cancel();
        bool unwound = false;
        try {
            (void)xmlTimeValue("14:24");
        } catch (const CalculationCancelled &) {
            unwound = true;
        }
        check(unwound, "cooperative cancellation");
    }
    auto p = fixture(), probe = p;
    validateXmlFormulas(p);
    verify(p);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"TimeSerial(0,0,0)", "1899-12-30T00:00:00"},
        {"TimeSerial(16,35,17)", "1899-12-30T16:35:17"},
        {"TimeSerial(0,75,90)", "1899-12-30T01:16:30"},
        {"TimeSerial(24,0,0)", "1899-12-31T00:00:00"},
        {"TimeSerial(12-6,-15,0)", "1899-12-30T05:45:00"},
        {"TimeSerial(-1,0,0)", "1899-12-30T01:00:00"},
        {"TimeSerial(0,-1,0)", "1899-12-30T00:01:00"},
        {"TimeSerial(-24,0,0)", "1899-12-29T00:00:00"},
        {"TimeSerial(-25,0,0)", "1899-12-29T01:00:00"},
        {"TimeSerial(1.5,0,0)", "1899-12-30T02:00:00"},
        {"TimeSerial(2.5,0,0)", "1899-12-30T02:00:00"},
        {"TimeSerial(-1.5,0,0)", "1899-12-30T02:00:00"},
        {"TimeSerial(0.5,0,0)", "1899-12-30T00:00:00"},
        {"TimeSerial(0,0,2.5)", "1899-12-30T00:00:02"},
        {"TimeSerial(32767,32767,32767)", "1903-10-19T10:13:07"},
        {"TimeSerial(-32768,-32768,-32768)", "1896-03-12T11:14:08"},
        {"TimeValue(\"14:24\")", "1899-12-30T14:24:00"},
        {"TimeValue(\"2:24PM\")", "1899-12-30T14:24:00"},
        {"TimeValue(\"4:35:17 PM\")", "1899-12-30T16:35:17"},
        {"TimeValue(\"12:00 AM\")", "1899-12-30T00:00:00"},
        {"TimeValue(\"12:00 PM\")", "1899-12-30T12:00:00"},
        {"TimeValue(\"0:30am\")", "1899-12-30T00:30:00"},
        {"TimeValue(\"2pm\")", "1899-12-30T14:00:00"},
        {"TimeValue(\"1:2:3\")", "1899-12-30T01:02:03"},
        {"TimeValue(\" 14:24 \")", "1899-12-30T14:24:00"},
        {"TimeValue(\"09:30:12.123400\")", "1899-12-30T09:30:12.1234"},
        {"TimeValue(\"2024-02-29\")", "1899-12-30T00:00:00"},
        {"TimeValue(\"2024-02-29 2:24PM\")", "1899-12-30T14:24:00"},
        {"TimeValue(#2024-02-29T09:30:12.123456#)", "1899-12-30T09:30:12.123456"},
        {"TimeValue(\"0001-01-01T23:59:59.999999\")", "1899-12-30T23:59:59.999999"},
        {"TimeValue(\"9999-12-31T00:00:00.000001\")", "1899-12-30T00:00:00.000001"},
        {"TimeValue(TimeSerial(-25,0,0))", "1899-12-30T01:00:00"},
        {"TimeValue(PPMTimeZone(#2024-01-01T20:00:00.123456#,\"UTC\",\"+09:00\"))", "1899-12-30T05:00:00.123456"},
        {"TimeValue(StrConv(\"１４：２４\",8,1041))", "1899-12-30T14:24:00"}};
    QJsonArray records;
    for (const auto &[source, expected] : cases) {
        const auto r = eval(probe, source);
        if (r.value != expected)
            std::cerr << source << " expected " << expected << " got " << r.value.value_or(r.issue) << '\n';
        check(r.value == expected, "time case");
        records.append(
            QJsonObject{{"source", QString::fromStdString(source)}, {"value", QString::fromStdString(*r.value)}});
    }
    for (const auto *source : {"TimeValue(Null)", "TimeSerial(Null,0,0)", "TimeSerial(0,Null,0)"}) {
        const auto r = eval(probe, source);
        check(!r.value && r.missing, "native Null propagation");
    }
    for (const auto *source : {"TimeSerial(32767.5,0,0)",
                               "TimeSerial(-32768.500001,0,0)",
                               "TimeSerial(0,40000,0)",
                               "TimeSerial(\"1\",0,0)",
                               "TimeSerial(Null,1/0,0)",
                               "TimeValue(0.5)",
                               "TimeValue(True)",
                               "TimeValue(\"\")",
                               "TimeValue(\"24:00\")",
                               "TimeValue(\"23:59:60\")",
                               "TimeValue(\"14:24.5\")",
                               "TimeValue(\"14:24:12.1234567\")",
                               "TimeValue(\"13:00PM\")",
                               "TimeValue(\"2023-02-29T14:24\")",
                               "TimeValue(\"2024-02-29T\")",
                               "TimeValue(\"2024-02-29T14:24Z\")",
                               "TimeValue(\"14:24+09:00\")",
                               "TimeValue(\"오후 2:24\")",
                               "TimeValue(\"2/29/2024 14:24\")",
                               "TimeValue(\"1 :24\")",
                               "TimeValue(\"14:24x\")"}) {
        const auto r = eval(probe, source);
        check(!r.value && !r.missing && !r.issue.empty(), "bad time explicit");
    }
    rejects([&] { xmlTimeSerial(32768, 0, 0); });
    rejects([&] { xmlTimeValue(std::string(4097, '1')); });
    check(!xmlTimeFormula({"CStr(\"TimeValue(1)\")", {}, true}) && !xmlTimeFormula({"[TimeSerial(1)]", {}, true}),
          "scanner ignores strings/refs");
    check(nativeSchemaFor(p) == 56 && json::decodeProject(json::encodeProject(p), 56) == p, "schema56");
    rejects([&] { json::decodeProject(json::encodeProject(p), 55); });
    auto legacy = p;
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(legacy, scope))
            if (f.xmlFormula) {
                if (xmlTimeFormula(*f.xmlFormula))
                    f.xmlFormula->enabled = false;
                else
                    f.xmlFormula.reset();
            }
    check(nativeSchemaFor(legacy) == 44 && json::decodeProject(json::encodeProject(legacy), 44) == legacy,
          "disabled legacy44");
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("times.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    auto changed = bindXmlFormula(*doc.project(), FieldScope::Task, "TimeSerial(24,0,0)");
    changed.enabled = true;
    doc.setXmlFormula(FieldScope::Task, "field:serial", changed);
    check(value(*doc.project(), "serial") == "1899-12-31T00:00:00", "edit");
    doc.undo();
    verify(*doc.project());
    doc.redo();
    check(value(*doc.project(), "serial") == "1899-12-31T00:00:00", "redo");
    doc.undo();
    auto renamed = *customField(*doc.project(), "field:clock");
    renamed.name = "명시 시각 입력";
    doc.putCustomField(renamed);
    verify(*doc.project());
    doc.undo();
    doc.captureBaseline("시각 동결", "PM", "원본 보존", "2026-11-13T03:00:00Z");
    doc.saveScenario("시각 사본", "task-order", 260, "2026-11-13T03:01:00Z", doc.project()->documentId,
                     doc.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"USD", "EUR", "2026-11-13", "원본 보존"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 2;
    fx.reporter = "PM";
    fx.reason = "시각 원본";
    fx.recordedAt = "2026-11-13T03:02:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    verify(*doc.project());
    check(json::decodeProject(json::encodeProject(*doc.project()), 56) == *doc.project(), "FX archives56");
    auto frozen = *doc.project();
    clearTransitionHistories(frozen);
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(frozen, scope))
            f.xmlFormula.reset();
    check(nativeSchemaFor(frozen) == 56 && json::decodeProject(json::encodeProject(frozen), 56) == frozen,
          "frozen-only56");
    rejects([&] { json::decodeProject(json::encodeProject(frozen), 55); });
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
    check(json::decodeProject(json::encodeProject(migrated), 56) == migrated, "ID archive56");
    verify(migrated);
    JsonRecoveryStore recovery(temp.filePath("recovery"));
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-13T03:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "recovery56");
    Document ro(store);
    ro.open(path, true);
    rejects([&] { ro.setXmlFormula(FieldScope::Task, "field:serial", {}); });
    const auto xml = exportProjectXml(p);
    const auto imported = importProjectXml(xml.xml, "times.xml");
    check(hasXmlTimeFunctions(imported.project, true), "XML active");
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
    save(output() + "/times.xml", xml.xml);
    save(output() + "/times.xer", exportPrimaveraXer(p).xer);
    save(output() + "/mapping.json", QJsonDocument(xml.report).toJson());
    save(output() + "/cases.json", QJsonDocument(records).toJson());
    vectors();
    for (const auto *n : {"source.ppm", "fields.xlsx", "fields.pdf"})
        QFile::remove(output() + '/' + n);
    store.save((output() + "/source.ppm").toStdString(), p, {});
    ReportOptions options;
    options.reportId = "xml-time-functions";
    options.generatedAt = "2026-11-13T03:00:00Z";
    const auto report = buildReport(p, options);
    exportXlsx(report, output() + "/fields.xlsx");
    check(exportPdf(report, output() + "/fields.pdf") > 0, "reports");
    std::cout
        << "PASS TimeSerial/TimeValue: OLE carry/negative, half-even, English/ISO/micros, four "
           "scopes/summary/roll-down, schema56/archives/recovery/XML/XER/reports; 1024 serial and 3072 time vectors\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("times.ppm").toStdString();
    store.save(path, fixture(), {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    model.synchronize({0, 1});
    check(model.data(model.index(0, model.columnForKey("field:serial"))).toString().contains("03:15"), "table time");
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
            auto *fn = d->findChild<QComboBox *>("xmlTimeFunction");
            check(fn && fn->count() == 6, "time examples");
            source->clear();
            fn->setCurrentIndex(4);
            d->findChild<QPushButton *>("insertXmlTimeFormula")->click();
            check(source->toPlainText() == "TimeValue(\"09:30:12.123456\")", "time insertion");
            d->findChild<QPushButton *>("previewXmlFormula")->click();
            phase = 1;
        } else if (!d->property("xmlFormulaBusy").toBool()) {
            auto *preview = d->findChild<QTableWidget *>("xmlFormulaPreview");
            bool found = false;
            for (int r = 0; r < preview->rowCount(); ++r)
                for (int c = 0; c < preview->columnCount(); ++c)
                    if (preview->item(r, c) && preview->item(r, c)->text().contains("1899-12-30T09:30:12.123456"))
                        found = true;
            check(found, "anchored microsecond preview");
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
    editXmlFormula(doc, FieldScope::Task, "field:serial", nullptr);
    check(done && value(*doc.project(), "serial", 1) == "1899-12-30T09:30:12.123456", "UI save");
    doc.undo();
    verify(*doc.project());
    const auto unchanged = *doc.project();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        d->findChild<QPlainTextEdit *>("xmlFormulaSource")->setPlainText("TimeValue(\"24:00\")");
        d->findChild<QPushButton *>("previewXmlFormula")->click();
        d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:serial", nullptr);
    check(*doc.project() == unchanged, "cancel unchanged");
    std::cout << "PASS time UI: examples, anchored micros async preview/save/undo/cancel and small layout\n";
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
