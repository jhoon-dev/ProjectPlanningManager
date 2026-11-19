#include "XmlCalendarFixture.h"
#include "domain/XmlCalendar.h"
#include "domain/ProjectCurrency.h"
#include "domain/LedgerIdentity.h"
#include "domain/XmlRollUp.h"
#include "domain/XmlRollDown.h"
#include "domain/FormulaFields.h"
#include "calculation/ScheduleFormulaScope.h"
#include "calculation/WorkCalendar.h"
#include "calculation/Reports.h"
#include "calculation/ResourceViewReport.h"
#include "application/Document.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include "ui/TaskTableModel.h"
#include "ui/XmlFormulaDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QTimer>
#include <QTemporaryDir>
#include <iostream>
#include <cstdio>
namespace {
using namespace ppm;
using namespace ppm::test;
void check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write");
}
std::optional<std::string> value(const Project &p, const std::string &key, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + key);
}
XmlFormulaResult evaluate(Project &p, const std::string &source, CustomFieldType type = CustomFieldType::Number) {
    auto i = std::find_if(p.customFields.begin(), p.customFields.end(), [](const auto &f) { return f.id == "probe"; });
    if (i == p.customFields.end()) {
        p.customFields.push_back({"probe", "probe", type, "", {}});
        i = std::prev(p.customFields.end());
    }
    i->type = type;
    i->xmlFormula = bindXmlFormula(p, FieldScope::Task, source);
    i->xmlFormula->enabled = true;
    return xmlFormulaValue(p, FieldScope::Task, "field:probe", &p.tasks[0]);
}
void contract() {
    auto p = xmlCalendarFixture();
    validate(p);
    EvaluationScope evaluation;
    check(value(p, "literal") == "960" && value(p, "literal", 1) == "360",
          "calendar literals need no schedule context");
    {
        ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Plan);
        check(value(p, "work") == "480" && value(p, "work", 1) == "360", "task calendars / project holiday override");
        check(value(p, "wall") == "540" && value(p, "wall", 1) == "5940", "civil functions unchanged");
        check(value(p, "next") == "2026-11-13T18:00:00" && value(p, "next", 1) == "2026-11-19T11:00:00",
              "working days add");
        check(value(p, "prev") == "2026-11-10T09:00:00" && value(p, "prev", 1) == "2026-11-09T10:00:00",
              "subtract before project start");
        check(value(p, "explicit") == "180" && value(p, "explicit", 1) == "360", "explicit calendar");
        check(value(p, "first") == "2026-11-11T13:30:00" && value(p, "first", 1) == "2026-11-12T09:30:00",
              "lunch / custom daily periods");
        check(value(p, "elapsed") == "2026-11-12T18:00:00" && value(p, "elapsed", 1) == "2026-11-17T12:00:00",
              "elapsed duration bypasses nonwork");
        check(value(p, "scope", 1) == "1320" && value(p, "native") == "960", "defining field scope and native chain");
        check(xmlFormulaValue(p, FieldScope::Resource, "field:nested", nullptr, &p.resources[0]).value == "1320",
              "resource/project lexical scope");
        check(xmlFormulaValue(p, FieldScope::Assignment, "field:mix", nullptr, nullptr, &p.assignments[1]).value ==
                  "1680",
              "assignment task/resource/project scope");
        check(RollDownValues(p).value(p.assignments[0], "field:copy") == "480", "roll-down");
        RollUpValues groups(p);
        check(groups.cell("G", "field:work").value == "1140" && groups.cell("G", "field:literal").value == "960" &&
                  groups.cell("G", "field:explicit").value == "540",
              "summary uses project default / explicit named calendar");
    }
    auto q = p;
    check(evaluate(q, "ProjDateAdd(#2026-11-13T18:00:00#,\"1h\")", CustomFieldType::Date).value ==
              "2026-11-16T10:00:00",
          "weekend");
    check(evaluate(q, "ProjDateAdd(#2026-11-13T23:00:00#,\"2h\",\"야간\")", CustomFieldType::Date).value ==
              "2026-11-15T01:00:00",
          "night / midnight / holiday");
    check(evaluate(q, "ProjDateDiff(#2026-11-11T13:30:00#,#2026-11-11T11:30:00#)").value == "-60", "signed diff");
    check(evaluate(q, "ProjDateAdd(#2026-11-11T11:59:59.999999#,0.000001)", CustomFieldType::Date).value ==
              "2026-11-11T13:00:00.000059",
          "fractional minute and microseconds");
    check(evaluate(q, "ProjDateSub(#2026-11-11T13:00:00.000059#,0.000001)", CustomFieldType::Date).value ==
              "2026-11-11T11:59:59.999999",
          "microsecond inverse");
    check(evaluate(q, "ProjDateAdd(#2026-11-14T15:00:00#,0)", CustomFieldType::Date).value == "2026-11-14T15:00:00",
          "zero identity on nonworking date");
    check(evaluate(q, "ProjDateAdd(#2026-11-11T09:00:00#,\"-1h\")", CustomFieldType::Date).value ==
              "2026-11-10T17:00:00",
          "negative add");
    check(evaluate(q, "ProjDateAdd(#2026-11-11T09:00:00#,ProjDurValue(\"60 em\"))", CustomFieldType::Date).value ==
              "2026-11-11T10:00:00",
          "typed duration");
    for (auto source : {"ProjDateDiff(#2026-11-11#,#2026-11-12#,\"없는 달력\")", "ProjDateAdd(#2026-11-11#,True)",
                        "ProjDateAdd(#2026-11-11#,\"1mo\")", "ProjDateAdd(#2026-11-11#,999999999999)",
                        "ProjDateDiff(#2000-01-01#,#2050-01-01#)"}) {
        auto v = evaluate(q, source, CustomFieldType::Date);
        check(!v.value && !v.missing && !v.issue.empty(), "invalid args / bounds error");
    }
    auto duplicate = q;
    duplicate.calendars.push_back(duplicate.calendars[0]);
    duplicate.calendars.back().id = "OTHER";
    check(!evaluate(duplicate, "ProjDateDiff(#2026-11-11#,#2026-11-12#,\"오전\")").value, "ambiguous name rejected");
    auto empty = q;
    empty.holidays.clear();
    for (int i = 0; i < 21 * 366; ++i) {
        auto d = std::chrono::sys_days{std::chrono::year{2026} / 11 / 11} + std::chrono::days{i};
        auto ymd = std::chrono::year_month_day{d};
        char text[16];
        std::snprintf(text, sizeof(text), "%04d-%02u-%02u", int(ymd.year()), unsigned(ymd.month()),
                      unsigned(ymd.day()));
        empty.holidays.emplace_back(text);
    }
    check(!evaluate(empty, "ProjDateAdd(#2026-11-11T09:00:00#,1)", CustomFieldType::Date).value,
          "bounded no availability");
    auto split = xmlCalendarFixture();
    split.tasks[0].breaks = {{"2026-11-11 10:00", "2026-11-11 11:00"}};
    split.resources[0].exceptions = {{"2026-11-11", 0}};
    check(value(split, "literal") == "960", "calendar functions ignore task splits / capacity exceptions");
    check(evaluate(q, "ProjDateDiff(#2026-11-11T09:00:00#,#2026-11-13T18:00:00#,\"Standard\")").value == "960",
          "standard alias");
    auto aliasXml = exportProjectXml(q);
    auto aliasCopy = importProjectXml(aliasXml.xml, "alias.xml").project;
    auto aliasField = std::find_if(aliasCopy.customFields.begin(), aliasCopy.customFields.end(),
                                   [](const auto &f) { return f.name == "probe"; });
    check(aliasField != aliasCopy.customFields.end() &&
              xmlFormulaValue(aliasCopy, FieldScope::Task, "field:" + aliasField->id, &aliasCopy.tasks[0]).value ==
                  "960",
          "standard alias after XML import");
    auto before = value(p, "literal");
    p.holidays.clear();
    check(value(p, "literal") == "1440" && before == "960", "cache includes project holidays");
    p = xmlCalendarFixture();
    p.tasks[0].calendarId = "AM";
    check(value(p, "literal") == "360", "cache includes default task calendar");
    p = xmlCalendarFixture();
    p.calendars[0].periods = {{540, 660}};
    check(value(p, "literal", 1) == "240", "cache includes custom periods");
    p = xmlCalendarFixture();
    check(nativeSchemaFor(p) == 46 && json::decodeProject(json::encodeProject(p), 46) == p, "schema46");
    bool old = false;
    try {
        json::decodeProject(json::encodeProject(p), 45);
    } catch (const AppError &) {
        old = true;
    }
    check(old, "old schema rejected");
    check(!xmlCalendarFormula(XmlFormula{"\"ProjDateDiff(\" & [ProjDateAdd(x)]", {}, false}) &&
              xmlCalendarFormula(XmlFormula{" pRoJdAtEaDd (#2026-11-11#,1)", {}, false}),
          "function lexing excludes strings/refs");
    auto global = xmlCalendarFixture();
    global.xmlFormulaCalendarId = "AM";
    check(xmlFormulaValue(global, FieldScope::Project, "field:p").value == "360" && value(global, "literal") == "960",
          "imported project default does not override native leaf calendar");
    check(json::decodeProject(json::encodeProject(global), 46) == global, "project calendar snapshot");
    auto globalRoundtrip = importProjectXml(exportProjectXml(global).xml, "global.xml").project;
    auto projectField = std::find_if(globalRoundtrip.projectFields.begin(), globalRoundtrip.projectFields.end(),
                                     [](const auto &f) { return f.name == "p"; });
    check(xmlFormulaValue(globalRoundtrip, FieldScope::Project, "field:" + projectField->id).value == "360",
          "custom project calendar XML roundtrip");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope cancelledScope(control);
        evaluate(q, "ProjDateDiff(#2026-11-11#,#2026-11-12#)");
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cancel propagates");
    const auto out = QDir::currentPath() + "/xml-calendar-qa";
    QDir().mkpath(out);
    p.statusAt = "2026-11-16 18:00";
    auto xml = exportProjectXml(p);
    save(out + "/plan.xml", xml.xml);
    save(out + "/plan.xer", exportPrimaveraXer(p).xer);
    save(out + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    auto imported = importProjectXml(xml.xml, "calendar.xml");
    check(hasXmlCalendarFunctions(imported.project, true), "XML imports calendar functions enabled");
    check(!imported.project.xmlFormulaCalendarId.empty() &&
              xmlFormulaValue(imported.project, FieldScope::Project, "field:" + imported.project.projectFields[0].id)
                      .value == "960",
          "XML project calendar default preserved");
    save(out + "/reexport.xml", exportProjectXml(imported.project).xml);
    ReportOptions options;
    options.reportId = "xml-calendar";
    options.generatedAt = "2026-11-11T09:00:00Z";
    for (auto name : {"forecast.xlsx", "forecast.pdf", "resources.xlsx", "source.ppm"})
        QFile::remove(out + '/' + name);
    auto report = buildReport(p, options);
    exportXlsx(report, out + "/forecast.xlsx");
    exportPdf(report, out + "/forecast.pdf");
    exportXlsx(resourceViewReport(p, {}, options), out + "/resources.xlsx");
    JsonProjectStore store;
    store.save((out + "/source.ppm").toStdString(), p, {});
    Document doc(store);
    doc.open((out + "/source.ppm").toStdString());
    doc.captureBaseline("동결", "PM", "달력 식", "2026-11-11T09:01:00Z");
    doc.saveScenario("사본", "task-order", 260, "2026-11-11T09:02:00Z", doc.project()->documentId,
                     doc.project()->revision);
    check(json::decodeProject(json::encodeProject(*doc.project()), 46) == *doc.project(), "frozen schema46");
    QTemporaryDir dir;
    JsonRecoveryStore recovery(dir.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-11T09:03:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "recovery46");
    QTemporaryDir fxDir;
    auto fxPath = fxDir.filePath("fx.ppm").toStdString();
    store.save(fxPath, global, {});
    Document fxDoc(store);
    fxDoc.open(fxPath);
    fxDoc.captureBaseline("달력 동결", "PM", "달력 원본", "2026-11-11T09:04:00Z");
    CurrencyConversionSubmission fx;
    fx.rate = {"KRW", "USD", "2026-11-11", "달력 보존"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 1400;
    fx.reporter = "PM";
    fx.reason = "환산";
    fx.recordedAt = "2026-11-11T09:05:00Z";
    fx.confirmed = true;
    fxDoc.convertCurrency(fx);
    check(fxDoc.project()->xmlFormulaCalendarId == "AM" &&
              json::decodeProject(json::encodeProject(*fxDoc.project()), 46) == *fxDoc.project(),
          "FX snapshots retain project calendar");
    save(out + "/archives.json", QJsonDocument(json::encodeProject(*fxDoc.project())).toJson());
    auto observed = global;
    observed.costEntries = {{"fact",
                             "A",
                             "2026-11-11 09:00",
                             "2026-11-11T09:06:00Z",
                             "PM",
                             "원장",
                             {},
                             global.documentId,
                             global.currency,
                             1,
                             100,
                             {}}};
    auto migrated = migrateLedgerIdentities(observed, {{"task", "A", "NEW"}});
    check(migrated.xmlFormulaCalendarId == "AM" && json::decodeProject(json::encodeProject(migrated), 46) == migrated,
          "identity snapshots retain project calendar");
    // Differential check against the schedule engine's working coordinates, and independent oracle vectors.
    QByteArray vectors = "calendar\tfrom\tto\tdiff\tamount\tadded\tsubtracted\n";
    auto sample = xmlCalendarFixture();
    for (int i = 0; i < 180; ++i) {
        auto &task = sample.tasks[0];
        task.calendarId = i % 3 == 0 ? "" : i % 3 == 1 ? "AM" : "NIGHT";
        const auto from = "2026-11-" + std::string(i % 15 + 1 < 10 ? "0" : "") + std::to_string(i % 15 + 1) + "T" +
                          std::string(i % 24 < 10 ? "0" : "") + std::to_string(i % 24) + ":15:00";
        const auto to =
            "2026-11-" + std::string(i % 15 + 10 < 10 ? "0" : "") + std::to_string(i % 15 + 10) + "T17:45:00";
        const auto amount = (i % 17 - 8) * 30;
        auto actual = xmlCalendarDifference(sample, FieldScope::Task, &task, nullptr, "", from, to);
        auto added = xmlCalendarShift(sample, FieldScope::Task, &task, nullptr, "", from, amount * 1000000LL, false);
        auto sub = xmlCalendarShift(sample, FieldScope::Task, &task, nullptr, "", from, -amount * 1000000LL, false);
        auto anchored = sample;
        anchored.startDate = "2026-11-01";
        WorkCalendar calendar(anchored, anchored.tasks[0]);
        check(actual == (calendar.offset(to.substr(0, 10) + " " + to.substr(11, 5)) -
                         calendar.offset(from.substr(0, 10) + " " + from.substr(11, 5))) *
                            1000000LL,
              "scheduler differential");
        vectors += QByteArray::fromStdString((task.calendarId.empty() ? "STANDARD" : task.calendarId) + "\t" + from +
                                             "\t" + to + "\t" + formatFieldNumber(actual) + "\t" +
                                             std::to_string(amount) + "\t" + added + "\t" + sub + "\n");
    }
    save(out + "/vectors.tsv", vectors);
    std::cout << "PASS calendar functions: calendars, scopes, working/elapsed math, precision/bounds/cache, "
                 "schema/XML/recovery/report, 180 scheduler vectors\n";
}
void ui() {
    auto p = xmlCalendarFixture();
    QTemporaryDir tmp;
    JsonProjectStore store;
    auto path = tmp.filePath("calendar.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    doc.applyCalculation(calculate(*doc.project()));
    TaskTableModel model(doc);
    model.synchronize({0, 1});
    check(model.data(model.index(0, model.columnForKey("field:work"))).toString() == "480", "table calendar result");
    QTimer timer;
    timer.setInterval(25);
    bool finished = false;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->property("xmlFormulaBusy").toBool())
            return;
        auto *table = dialog->findChild<QTableWidget *>("xmlFormulaPreview");
        if (!table)
            return;
        check(table->rowCount() == 2 && table->item(0, 1)->text() == "480" && table->item(1, 1)->text() == "360",
              "async work preview");
        auto out = QDir::currentPath() + "/xml-calendar-qa";
        QDir().mkpath(out);
        dialog->resize(800, 650);
        QApplication::processEvents();
        check(dialog->grab().save(out + "/calendar-small.png"), "UI screenshot");
        timer.stop();
        finished = true;
        dialog->reject();
    });
    timer.start();
    QTimer::singleShot(15000, [&] {
        if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:work", nullptr);
    check(finished, "preview finished");
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        check(dialog, "dialog");
        auto *source = dialog->findChild<QPlainTextEdit *>("xmlFormulaSource");
        source->clear();
        dialog->findChild<QComboBox *>("xmlCalendarFunction")->setCurrentIndex(0);
        auto *calendar = dialog->findChild<QComboBox *>("xmlFormulaCalendar");
        calendar->setCurrentIndex(calendar->findText("오전"));
        dialog->findChild<QPushButton *>("insertXmlCalendarFormula")->click();
        check(source->toPlainText() == "ProjDateDiff([Start], [Finish], \"오전\")", "insert named calendar expression");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    editXmlFormula(doc, FieldScope::Task, "field:work", nullptr);
    {
        ScheduleFormulaScope scope(*doc.project());
        check(value(*doc.project(), "work") == "180", "save active calendar expression");
    }
    doc.undo();
    {
        ScheduleFormulaScope scope(*doc.project());
        check(value(*doc.project(), "work") == "480", "undo");
    }
    std::cout << "PASS calendar UI: worker preview, calendar/function insertion, save/undo, small layout\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    check(font >= 0, "font");
    QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        if (app.arguments().contains("--ui"))
            ui();
        else
            contract();
    } catch (const AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << ' ' << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
