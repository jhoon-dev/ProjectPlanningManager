#include "XmlActualFixture.h"
#include "application/Document.h"
#include "calculation/ScheduleFormulaScope.h"
#include "calculation/Reports.h"
#include "calculation/ResourceViewReport.h"
#include "domain/FormulaFields.h"
#include "domain/XmlRollUp.h"
#include "domain/XmlRollDown.h"
#include "domain/ProjectCurrency.h"
#include "domain/LedgerIdentity.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "reporting/ReportExport.h"
#include "ui/TaskTableModel.h"
#include "ui/XmlFormulaDialog.h"
#include "ui/TaskViewProjection.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
namespace {
using namespace ppm;
using namespace ppm::test;
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
void save(const QString &name, const QByteArray &bytes) {
    QFile f(name);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write");
}
std::optional<std::string> value(const Project &p, const std::string &key, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + key);
}
CustomField &named(Project &p, const std::string &name, FieldScope scope = FieldScope::Task) {
    auto &fields = customFields(p, scope);
    auto i = std::find_if(fields.begin(), fields.end(), [&](const auto &f) { return f.name == name; });
    if (i == fields.end())
        throw std::runtime_error("field missing");
    return *i;
}
void verify(const Project &p) {
    ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
    check(value(p, "AS") == "2026-11-11T10:30:00" && value(p, "AF") == "2026-11-11T16:30:00",
          "same-status correction selected, future ignored");
    check(value(p, "AS", 1) == "2026-11-12T10:00:00" && !value(p, "AF", 1), "in progress has no actual finish");
    check(!value(p, "AS", 2) && !value(p, "AF", 2) && !value(p, "AS", 3) && !value(p, "AF", 3),
          "unreported/milestone not inferred from plan or zero remaining");
    check(value(p, "work") == "300" && value(p, "wall") == "360" && value(p, "native") == "600",
          "calendar/civil/native chain");
    check(value(p, "state") == "완료" && value(p, "state", 1) == "진행" && value(p, "state", 2) == "미착수",
          "Null handling");
    check(value(p, "globalAS", 3) == value(p, "AS") && !value(p, "globalAF"),
          "project earliest actual start / incomplete finish");
    check(xmlFormulaValue(p, FieldScope::Project, "field:AS").value == value(p, "AS") &&
              !xmlFormulaValue(p, FieldScope::Project, "field:AF").value,
          "project actual refs");
    check(xmlFormulaValue(p, FieldScope::Resource, "field:AS", nullptr, &p.resources[0]).value == value(p, "AS"),
          "resource global refs");
    check(xmlFormulaValue(p, FieldScope::Assignment, "field:AS", nullptr, nullptr, &p.assignments[1]).value ==
              value(p, "AS", 1),
          "assignment explicit task refs");
    check(RollDownValues(p).value(p.assignments[0], "field:copy") == value(p, "AS"), "actual roll-down");
    RollUpValues wbs(p);
    check(wbs.cell("G", "field:AS").value == value(p, "AS") && !wbs.cell("G", "field:AF").value &&
              !wbs.cell("H", "field:AS").value && !wbs.cell("", "field:AF").value &&
              wbs.cell("", "field:AS").value == value(p, "AS"),
          "partial start / all-complete finish across empty actual groups");
    RollUpValues filtered(p, FieldScope::Task, {{"one", "", {0}}, {"unknown", "", {2, 3}}, {"empty", "", {}}});
    check(filtered.cell("one", "field:AF").value == value(p, "AF") &&
              filtered.cell("one", "field:work").value == "300" && !filtered.cell("unknown", "field:AS").value &&
              !filtered.cell("empty", "field:AF").value,
          "filtered actual summaries / empty groups");
}
void contract() {
    auto p = xmlActualFixture();
    validate(p);
    EvaluationScope evaluation;
    check(hasXmlActualReferences(p, true) && !hasXmlComputedReferences(p, true),
          "separate actual/computed dependencies");
    check(!value(p, "AS"), "no context/raw fallback");
    verify(p);
    check(!value(p, "AS"), "scope/cache restored");
    for (auto status : {CalculationStatus::Incomplete, CalculationStatus::Conflict, CalculationStatus::Stale,
                        CalculationStatus::Cancelled}) {
        ScheduleResult invalid;
        invalid.status = status;
        invalid.documentId = "other";
        ScheduleFormulaScope scope(p, &invalid, ScheduleFormulaMode::Active, false);
        check(value(p, "AS") == "2026-11-11T10:30:00",
              "actual ledger independent of missing/stale/conflicting/cancelled schedule");
    }
    auto history = p;
    for (auto status : {"2026-11-10 18:00", "2026-11-11 18:00", "2026-11-12 18:00", "2026-11-17 18:00"}) {
        history.statusAt = status;
        ScheduleFormulaScope scope(history);
        auto entry = progressAsOf(history, "A");
        auto expected =
            entry && entry->actualStart ? std::optional{normalizeFieldDateTime(*entry->actualStart)} : std::nullopt;
        check(value(history, "AS") == expected, "as-of parity with progressAsOf");
    }
    history.statusAt.reset();
    {
        ScheduleFormulaScope scope(history);
        auto v = xmlFormulaValue(history, FieldScope::Task, "field:AS", &history.tasks[0]);
        check(!v.value && v.missing && v.issue.find("상태일") != std::string::npos, "no status is missing");
    }
    history = p;
    history.progress.push_back(actualEntry(history, "A4", "A", "2026-11-13 18:00", {}, {}, 0, 480, "A3"));
    {
        ScheduleFormulaScope scope(history);
        check(!value(history, "AS") && !value(history, "AF"),
              "latest clearing report does not backfill historical dates");
    }
    auto complete = actualComplete(p);
    validate(complete);
    {
        ScheduleFormulaScope scope(complete);
        check(value(complete, "globalAF") == "2026-11-13T15:00:00", "project all-complete finish");
        RollUpValues wbs(complete);
        check(wbs.cell("G", "field:AF").value == "2026-11-13T12:00:00" &&
                  wbs.cell("", "field:AF").value == "2026-11-13T15:00:00",
              "group vs whole finish");
    }
    auto incomplete = p;
    incomplete.tasks[2].durationMinutes.reset();
    {
        ScheduleFormulaScope scope(incomplete);
        check(value(incomplete, "AS") == "2026-11-11T10:30:00", "incomplete plan needs no scheduler");
    }
    auto early = p;
    early.progress[3].actualStart = "2026-11-10 10:30";
    early.progress[3].actualFinish = "2026-11-10 16:30";
    {
        ScheduleFormulaScope scope(early);
        check(value(early, "AS") == "2026-11-10T10:30:00" && value(early, "work") == "300",
              "actual dates before project start");
    }
    auto mix = p;
    actualField(mix, FieldScope::Task, "planned", CustomFieldType::Start, "[Start]");
    ScheduleResult stale;
    stale.status = CalculationStatus::Stale;
    {
        ScheduleFormulaScope scope(mix, &stale, ScheduleFormulaMode::Active, false);
        check(value(mix, "AS") == "2026-11-11T10:30:00" && !value(mix, "planned"),
              "mixed stale context separates actual and computed state");
    }
    auto copy = p;
    {
        ScheduleFormulaScope scope(p);
        check(!value(copy, "AS"), "snapshot identity isolation");
    }
    auto bad = p;
    bad.progress.back().actualStart = "invalid";
    {
        ScheduleFormulaScope scope(bad);
        auto v = xmlFormulaValue(bad, FieldScope::Task, "field:AS", &bad.tasks[0]);
        check(!v.value && !v.missing && !v.issue.empty(), "malformed selected ledger rejected");
    }
    for (auto scope : {FieldScope::Assignment, FieldScope::Resource})
        check(bindXmlFormula(p, scope, "[Actual Start]").references.empty(),
              "bare assignment/resource actuals not task proxies");
    check(bindXmlFormula(p, FieldScope::Assignment, "[Resource.Actual Start]").references.empty(),
          "resource actual unsupported");
    auto legacy = p;
    legacy.projectFields.push_back({"manual", "ActualStart", CustomFieldType::Start, "", {}});
    auto old = bindXmlFormula(legacy, FieldScope::Task, "[Project.ActualStart]",
                              {{"Project.ActualStart", "field:manual", FieldScope::Project, true}});
    check(old.references.front().key == "field:manual", "old binding preserved");
    check(nativeSchemaFor(p) == 47 && json::decodeProject(json::encodeProject(p), 47) == p, "schema47");
    bool rejected = false;
    try {
        json::decodeProject(json::encodeProject(p), 46);
    } catch (const AppError &) {
        rejected = true;
    }
    check(rejected, "schema46 rejects actual refs");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope cancel(control);
        ScheduleFormulaScope scope(p);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cancel propagates without scheduler");
    const auto out = QDir::currentPath() + "/xml-actual-qa";
    QDir().mkpath(out);
    save(out + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(out + "/complete.json", QJsonDocument(json::encodeProject(complete)).toJson());
    auto xml = exportProjectXml(p);
    save(out + "/actuals.xml", xml.xml);
    save(out + "/complete.xml", exportProjectXml(complete).xml);
    save(out + "/actuals.xer", exportPrimaveraXer(p).xer);
    save(out + "/mapping.json", QJsonDocument(xml.report).toJson());
    auto imported = importProjectXml(xml.xml, "actuals.xml").project;
    check(hasXmlActualReferences(imported, true) && imported.progress.empty(),
          "XML definitions active without inventing progress records");
    {
        ScheduleFormulaScope scope(imported);
        auto &f = named(imported, "AS");
        check(!resolvedAttributeValue(imported, imported.tasks[0], "field:" + f.id),
              "XML reimport cannot invent actuals from static snapshots");
    }
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(imported, scope))
            if (f.xmlFormula)
                f.xmlFormula->enabled = false;
    check(resolvedAttributeValue(imported, imported.tasks[0], "field:" + named(imported, "AS").id) ==
              "2026-11-11T10:30:00",
          "disable reveals imported static snapshot");
    for (auto name : {"fields.xlsx", "fields.pdf", "resources.xlsx", "source.ppm"})
        QFile::remove(out + '/' + name);
    ReportOptions options;
    options.reportId = "xml-actual";
    options.generatedAt = "2026-11-11T10:00:00Z";
    auto report = buildReport(p, options);
    exportXlsx(report, out + "/fields.xlsx");
    exportPdf(report, out + "/fields.pdf");
    exportXlsx(resourceViewReport(p, {}, options), out + "/resources.xlsx");
    JsonProjectStore store;
    auto path = (out + "/source.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    verify(*doc.project());
    doc.captureBaseline("실적식 동결", "PM", "정의 보존", "2026-11-11T10:01:00Z");
    doc.saveScenario("실적 사본", "task-order", 260, "2026-11-11T10:02:00Z", doc.project()->documentId,
                     doc.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"KRW", "USD", "2026-11-11", "환산"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 1400;
    fx.reporter = "PM";
    fx.reason = "이력";
    fx.recordedAt = "2026-11-11T10:03:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    check(json::decodeProject(json::encodeProject(*doc.project()), 47) == *doc.project(), "FX/frozen schema47");
    verify(*doc.project());
    save(out + "/archives.json", QJsonDocument(json::encodeProject(*doc.project())).toJson());
    QTemporaryDir tmp;
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-11T10:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "recovery47");
    auto observed = p;
    observed.costEntries = {{"fact",
                             "A",
                             "2026-11-11 18:00",
                             "2026-11-11T10:05:00Z",
                             "PM",
                             "이관",
                             {},
                             p.documentId,
                             p.currency,
                             1,
                             100,
                             {}}};
    auto migrated = migrateLedgerIdentities(observed, {{"task", "A", "NEW"}});
    check(json::decodeProject(json::encodeProject(migrated), 47) == migrated, "ID migration schema47");
    verify(migrated);
    std::cout << "PASS actual refs: as-of/corrections/nulls, scopes/summaries, schedule independence/cache, "
                 "schema/XML/static outputs, calendar/native/roll-down, archives/recovery/FX/IDs\n";
}
void ui() {
    auto p = xmlActualFixture();
    QTemporaryDir tmp;
    JsonProjectStore store;
    auto path = tmp.filePath("actual.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    model.synchronize({0, 1, 2, 3});
    auto c = model.columnForKey("field:AS");
    check(model.data(model.index(0, c)).toString().contains("10:30"), "actual table without calculation");
    doc.setStatusAt("2026-11-11 18:00");
    model.synchronize({0, 1, 2, 3});
    check(model.data(model.index(0, c)).toString().contains("09:00"), "status change invalidates actual cache");
    doc.undo();
    model.synchronize({0, 1, 2, 3});
    check(model.data(model.index(0, c)).toString().contains("10:30"), "undo status");
    QTimer poll;
    poll.setInterval(25);
    bool finished = false;
    QObject::connect(&poll, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->property("xmlFormulaBusy").toBool())
            return;
        auto *table = dialog->findChild<QTableWidget *>("xmlFormulaPreview");
        if (!table)
            return;
        check(table->rowCount() == 4 && table->item(0, 1)->text().contains("10:30") &&
                  table->item(2, 1)->text() == "미설정",
              "actual preview");
        check(dialog->findChild<QComboBox *>("xmlFormulaReferences")->findData("[Actual Finish]") >= 0,
              "actual insert choice");
        check(dialog->findChild<QLabel *>("xmlFormulaSummary")->text().contains("실적"), "actual provenance");
        auto out = QDir::currentPath() + "/xml-actual-qa";
        QDir().mkpath(out);
        dialog->resize(800, 650);
        QApplication::processEvents();
        check(dialog->grab().save(out + "/actual-small.png"), "screenshot");
        finished = true;
        poll.stop();
        dialog->reject();
    });
    poll.start();
    QTimer::singleShot(15000, [&] {
        if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:AS", nullptr);
    check(finished, "preview finished");
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        dialog->findChild<QCheckBox *>("xmlFormulaEnabled")->setChecked(false);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    editXmlFormula(doc, FieldScope::Task, "field:AS", nullptr);
    check(value(*doc.project(), "AS") == "2000-01-01T00:00:00", "disable reveals raw snapshot");
    doc.undo();
    {
        ScheduleFormulaScope scope(*doc.project());
        check(value(*doc.project(), "AS") == "2026-11-11T10:30:00", "undo activation");
    }
    doc.setStatusAt("2026-11-13 18:00");
    auto correction = actualEntry(*doc.project(), "unused", "A", *doc.project()->statusAt, "2026-11-11 11:00",
                                  "2026-11-11 17:00", 300, 0);
    doc.recordProgress(correction);
    model.synchronize({0, 1, 2, 3});
    check(model.data(model.index(0, c)).toString().contains("11:00"), "new actual report refreshes table");
    check(!doc.canUndo(), "submitted actuals retain immutable history");
    auto submitted = doc.project()->progress;
    auto amend = actualEntry(*doc.project(), "unused", "A", *doc.project()->statusAt, "2026-11-11 10:30",
                             "2026-11-11 16:30", 300, 0);
    doc.recordProgress(amend);
    model.synchronize({0, 1, 2, 3});
    check(model.data(model.index(0, c)).toString().contains("10:30") &&
              doc.project()->progress.size() == submitted.size() + 1 &&
              std::equal(submitted.begin(), submitted.end(), doc.project()->progress.begin()),
          "correction refreshes values and preserves history");
    std::cout << "PASS actual UI: no-scheduler table, status undo/report corrections, preview/provenance/disable, "
                 "small layout\n";
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
        for (const auto &s : e.diagnostic().arguments)
            std::cerr << ' ' << s;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
