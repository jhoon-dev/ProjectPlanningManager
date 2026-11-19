#include "XmlQuantityFixture.h"
#include "application/Document.h"
#include "calculation/ScheduleFormulaScope.h"
#include "calculation/Reports.h"
#include "calculation/ResourceViewReport.h"
#include "domain/XmlRollUp.h"
#include "domain/LedgerIdentity.h"
#include "domain/ProjectCurrency.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include "ui/TaskTableModel.h"
#include "ui/XmlFormulaDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
namespace {
using namespace ppm;
using namespace ppm::test;
void check(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
void save(const QString &name, const QByteArray &bytes) {
    QFile f(name);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write output");
}
XmlFormulaResult get(const Project &p, FieldScope s, const std::string &id, int row = 0) {
    return xmlFormulaValue(p, s, "field:" + id, s == FieldScope::Task ? &p.tasks.at(row) : nullptr,
                           s == FieldScope::Resource ? &p.resources.at(row) : nullptr,
                           s == FieldScope::Assignment ? &p.assignments.at(row) : nullptr);
}
void verify(const Project &p) {
    ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
    check(get(p, FieldScope::Task, "DN").value == "300", "reported duration, not date difference");
    check(get(p, FieldScope::Task, "AD").value &&
              parseFieldDuration(*get(p, FieldScope::Task, "AD").value).minutes == 300000000,
          "typed duration output");
    check(get(p, FieldScope::Task, "DN", 1).value == "60" && !get(p, FieldScope::Task, "DN", 2).value &&
              get(p, FieldScope::Task, "DN", 3).value == "0",
          "reported zero and unreported duration differ");
    check(get(p, FieldScope::Task, "AW").value == "75.01" && get(p, FieldScope::Task, "AW", 1).value == "0" &&
              !get(p, FieldScope::Task, "AW", 2).value,
          "dated work/correction/future selection, exact fractional minutes, zero/null");
    check(get(p, FieldScope::Project, "AW").value == "75.01" && get(p, FieldScope::Resource, "AW").value == "45.01" &&
              get(p, FieldScope::Resource, "AW", 1).value == "30",
          "project and resource reported totals");
    check(get(p, FieldScope::Assignment, "AW").value == "45.01" &&
              get(p, FieldScope::Assignment, "AW", 4).value == "30" &&
              get(p, FieldScope::Assignment, "TD").value == "300" &&
              get(p, FieldScope::Assignment, "TW").value == "75.01" &&
              get(p, FieldScope::Assignment, "RW").value == "45.01" &&
              get(p, FieldScope::Assignment, "PW").value == "75.01",
          "assignment own work and explicit owner/global references");
    RollUpValues wbs(p);
    check(wbs.cell("G", "field:AW").value == "75.01" && !wbs.cell("H", "field:AW").value &&
              wbs.cell("", "field:AW").value == "75.01" && !wbs.cell("G", "field:AD").value,
          "reported work summaries, unknown groups, no parallel duration sum");
    RollUpValues resources(p, FieldScope::Resource, {{"all", "", {0, 1}}});
    check(resources.cell("all", "field:AW").value == "75.01", "resource group sum");
}
void contract() {
    auto p = xmlQuantityFixture();
    validate(p);
    check(hasXmlActualQuantityReferences(p, true) && !hasXmlComputedReferences(p, true), "dependencies");
    verify(p);
    for (auto status : {CalculationStatus::Incomplete, CalculationStatus::Conflict, CalculationStatus::Stale,
                        CalculationStatus::Cancelled}) {
        ScheduleResult invalid;
        invalid.status = status;
        ScheduleFormulaScope scope(p, &invalid, ScheduleFormulaMode::Active, false);
        check(get(p, FieldScope::Task, "AW").value == "75.01", "work independent of plan result");
    }
    auto corrected = p;
    corrected.periodActuals.push_back(
        quantityEntry(corrected, "W4", "A", "R", "2026-11-11", "2026-11-12 18:00", 2501, "W3"));
    {
        ScheduleFormulaScope scope(corrected);
        check(get(corrected, FieldScope::Task, "AW").value == "55.01", "same status last correction wins");
        check(!get(p, FieldScope::Task, "AW").value, "context isolated by input identity");
    }
    corrected.periodActuals.push_back(
        quantityEntry(corrected, "W5", "A", "R", "2026-11-11", "2026-11-13 18:00", {}, "W4"));
    {
        ScheduleFormulaScope scope(corrected);
        check(!get(corrected, FieldScope::Assignment, "AW").value &&
                  get(corrected, FieldScope::Task, "AW").value == "30" &&
                  get(corrected, FieldScope::Resource, "AW").value == "0",
              "cleared work does not backfill, explicit zero retained");
    }
    auto history = p;
    history.statusAt = "2026-11-11 18:00";
    {
        ScheduleFormulaScope scope(history);
        check(get(history, FieldScope::Task, "AW").value == "11", "late historical correction");
    }
    history.statusAt.reset();
    {
        ScheduleFormulaScope scope(history);
        auto v = get(history, FieldScope::Task, "AW");
        check(v.missing && !v.value, "missing status is Null");
    }
    auto bad = p;
    bad.periodActuals[1].workUnitMinutes = maxWork;
    {
        ScheduleFormulaScope scope(bad);
        auto v = get(bad, FieldScope::Project, "AW");
        check(!v.value && !v.missing && !v.issue.empty() && get(bad, FieldScope::Task, "DN").value == "300",
              "overflow is error, duration unaffected");
    }
    bad = p;
    bad.periodActuals[1].workUnitMinutes = -1;
    {
        ScheduleFormulaScope scope(bad);
        check(!get(bad, FieldScope::Task, "AW").value && !get(bad, FieldScope::Task, "AW").missing,
              "invalid work rejected without snapshot fallback");
    }
    check(nativeSchemaFor(p) == 74 && json::decodeProject(json::encodeProject(p), 48) == p, "schema48 roundtrip");
    bool rejected = false;
    try {
        json::decodeProject(json::encodeProject(p), 47);
    } catch (const AppError &) {
        rejected = true;
    }
    check(rejected && nativeSchemaFor(xmlActualFixture()) == 47, "old schema rejected, date-only writer retained");
    for (auto s : {FieldScope::Resource, FieldScope::Assignment})
        check(bindXmlFormula(p, s, "[Actual Duration]").references.empty(), "unsupported duration scope");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope evaluation(control);
        ScheduleFormulaScope scope(p);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cancellation propagated");
    const auto out = QDir::currentPath() + "/xml-quantity-qa";
    QDir().mkpath(out);
    save(out + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    auto xml = exportProjectXml(p);
    save(out + "/quantities.xml", xml.xml);
    save(out + "/quantities.xer", exportPrimaveraXer(p).xer);
    save(out + "/mapping.json", QJsonDocument(xml.report).toJson());
    auto imported = importProjectXml(xml.xml, "quantities.xml").project;
    check(imported.progress.empty() && imported.periodActuals.empty() && hasXmlActualQuantityReferences(imported, true),
          "XML formula preserved without fabricated actuals");
    {
        ScheduleFormulaScope scope(imported);
        for (const auto &f : imported.customFields)
            if (f.name == "AW")
                check(!resolvedAttributeValue(imported, imported.tasks[0], "field:" + f.id), "reimport unknown");
    }
    for (const auto *name : {"fields.xlsx", "fields.pdf", "resources.xlsx", "source.ppm"})
        QFile::remove(out + '/' + name);
    ReportOptions options;
    options.reportId = "xml-quantity";
    options.generatedAt = "2026-11-12T10:00:00Z";
    auto report = buildReport(p, options);
    exportXlsx(report, out + "/fields.xlsx");
    exportPdf(report, out + "/fields.pdf");
    exportXlsx(resourceViewReport(p, {}, options), out + "/resources.xlsx");
    JsonProjectStore store;
    Document doc(store);
    const auto path = (out + "/source.ppm").toStdString();
    store.save(path, p, {});
    doc.open(path);
    verify(*doc.project());
    doc.captureBaseline("실적 정의", "PM", "동결", "2026-11-12T10:01:00Z");
    doc.saveScenario("실적 원장", "task-order", 260, "2026-11-12T10:02:00Z", doc.project()->documentId,
                     doc.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"KRW", "USD", "2026-11-12", "환산"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 1400;
    fx.reporter = "PM";
    fx.reason = "이력";
    fx.recordedAt = "2026-11-12T10:03:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    auto decoded = json::decodeProject(json::encodeProject(*doc.project()), 48);
    check(decoded == *doc.project(), "FX/frozen nested schema48");
    verify(decoded);
    auto migrated = migrateLedgerIdentities(decoded, {{"task", "A", "NEW"}});
    check(json::decodeProject(json::encodeProject(migrated), 48) == migrated, "ID nested schema48");
    verify(migrated);
    QTemporaryDir tmp;
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-12T10:04:00Z"));
    session.reset();
    auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema48 recovery");
    std::cout << "PASS actual quantities: as-of corrections/null/zero, scope totals/summaries, exact units, "
                 "schedule independence/cancel, schema48/archives/recovery, XML/XER/reports\n";
}
void ui() {
    auto p = xmlQuantityFixture();
    QTemporaryDir tmp;
    JsonProjectStore store;
    auto path = tmp.filePath("quantity.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    model.synchronize({0, 1, 2, 3});
    auto col = model.columnForKey("field:AW");
    check(model.data(model.index(0, col)).toString().contains("75.01"), "table quantity");
    doc.setStatusAt("2026-11-11 18:00");
    model.synchronize({0, 1, 2, 3});
    check(model.data(model.index(0, col)).toString().contains("11"), "status refresh");
    doc.undo();
    model.synchronize({0, 1, 2, 3});
    check(model.data(model.index(0, col)).toString().contains("75.01"), "status undo");
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
        check(table->rowCount() == 4 && table->item(0, 1)->text().contains("75.01") &&
                  table->item(2, 1)->text() == "미설정",
              "worker preview");
        auto *refs = dialog->findChild<QComboBox *>("xmlFormulaReferences");
        check(refs->findData("[Actual Duration]") >= 0 && refs->findData("[Actual Work]") >= 0,
              "quantity reference insertion choices");
        check(dialog->findChild<QLabel *>("xmlFormulaSummary")->text().contains("실적"), "provenance");
        const auto out = QDir::currentPath() + "/xml-quantity-qa";
        QDir().mkpath(out);
        dialog->resize(800, 700);
        QApplication::processEvents();
        check(dialog->grab().save(out + "/quantity-small.png"), "UI capture");
        finished = true;
        poll.stop();
        dialog->reject();
    });
    poll.start();
    QTimer::singleShot(15000, [&] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:AW", nullptr);
    check(finished, "quantity preview finished");
    std::cout << "PASS quantity UI: table/status undo, worker preview/insertion/provenance/small layout\n";
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
        if (auto *error = dynamic_cast<const AppError *>(&e))
            for (const auto &a : error->diagnostic().arguments)
                std::cerr << ' ' << a;
        return 1;
    }
}
