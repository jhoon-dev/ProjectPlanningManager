#include "XmlBaselineFixture.h"
#include "application/Document.h"
#include "calculation/ScheduleFormulaScope.h"
#include "calculation/ResourceViewReport.h"
#include "domain/XmlRollUp.h"
#include "domain/LedgerIdentity.h"
#include "persistence/ProjectFile.h"
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
using namespace jobs;
using namespace jobs::test;
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write output");
}
XmlFormulaResult get(const Project &p, FieldScope s, const std::string &id, int row = 0) {
    return xmlFormulaValue(p, s, "field:" + id, s == FieldScope::Task ? &p.tasks.at(row) : nullptr,
                           s == FieldScope::Resource ? &p.resources.at(row) : nullptr,
                           s == FieldScope::Assignment ? &p.assignments.at(row) : nullptr);
}
template <class F> void rejects(F f, const char *message) {
    bool rejected = false;
    try {
        f();
    } catch (const AppError &) {
        rejected = true;
    }
    check(rejected, message);
}
void verify(const Project &p) {
    ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
    check(get(p, FieldScope::Task, "BS").value == "2026-11-12T09:00:00" &&
              get(p, FieldScope::Task, "BF").value == "2026-11-13T09:01:00",
          "frozen dates");
    check(get(p, FieldScope::Task, "DN").value == "8" && get(p, FieldScope::Task, "DN", 1).value == "6" &&
              get(p, FieldScope::Task, "DN", 2).value == "0",
          "effective frozen duration and milestone");
    check(parseFieldDuration(*get(p, FieldScope::Task, "BD").value).minutes == 8 * fieldNumberScale, "typed duration");
    check(get(p, FieldScope::Task, "BW").value == "5.64" && get(p, FieldScope::Task, "BW", 1).value == "5.01" &&
              get(p, FieldScope::Task, "BW", 3).value == "0",
          "curve and work-driven frozen work");
    check(get(p, FieldScope::Project, "BW").value == "10.65" &&
              get(p, FieldScope::Project, "BF").value == "2026-11-13T09:01:00",
          "full baseline totals");
    check(get(p, FieldScope::Resource, "BW").value == "3" && get(p, FieldScope::Resource, "BW", 1).value == "7.65" &&
              get(p, FieldScope::Resource, "BW", 2).value == "0",
          "frozen resource totals");
    check(get(p, FieldScope::Assignment, "BW").value == "3" && get(p, FieldScope::Assignment, "TW").value == "5.64" &&
              get(p, FieldScope::Assignment, "TD").value == "8" &&
              get(p, FieldScope::Assignment, "PW").value == "10.65" &&
              get(p, FieldScope::Assignment, "RW").value == "3",
          "assignment/owner totals");
    for (auto s : {FieldScope::Task, FieldScope::Resource, FieldScope::Assignment}) {
        const auto row = s == FieldScope::Task ? 4 : 3;
        auto v = get(p, s, "BW", row);
        check(!v.value && v.missing && !v.issue.empty(), "post-baseline object is unknown rather than zero");
    }
    RollUpValues wbs(p);
    check(wbs.cell("G", "field:BW").value == "10.65" && wbs.cell("H", "field:BW").value == "0" &&
              wbs.cell("G", "field:BS").value == "2026-11-12T09:00:00" &&
              wbs.cell("G", "field:BF").value == "2026-11-13T09:01:00" && wbs.cell("G", "field:BD").value.has_value() &&
              !wbs.cell("", "field:BW").value,
          "WBS ranges, unsupported duration and strict missing propagation");
    RollUpValues groups(p, FieldScope::Task, {{"one", "", {0}}, {"empty", "", {}}});
    check(groups.cell("one", "field:BW").value == "5.64" && !groups.cell("empty", "field:BS").value,
          "filtered and empty groups");
    RollUpValues resources(p, FieldScope::Resource, {{"old", "", {0, 1, 2}}});
    RollUpValues newResources(p, FieldScope::Resource, {{"new", "", {0, 3}}});
    check(resources.cell("old", "field:BW").value == "10.65" && !newResources.cell("new", "field:BW").value,
          "resource group frozen totals and unknown rows");
    check(xmlScheduleCaption(p).find("BASE-1") != std::string::npos &&
              xmlScheduleCaption(p).find("BASE-2") == std::string::npos,
          "baseline provenance");
}
void contract() {
    auto p = xmlBaselineFixture();
    validate(p);
    for (const auto &b : p.baselines)
        verifyBaseline(b);
    verify(p);
    check(hasXmlBaselineReferences(p, true) && !hasXmlComputedReferences(p, true), "independent dependency");
    check(!get(p, FieldScope::Task, "BS").missing && !get(p, FieldScope::Task, "BS").value,
          "domain requires explicit context");
    auto stale = calculatePlan(p);
    stale.status = CalculationStatus::Stale;
    {
        ScheduleFormulaScope scope(p, &stale);
        check(get(p, FieldScope::Task, "BW").value == "5.64", "stale current schedule does not replace baseline");
    }
    auto missing = p;
    missing.baselines.clear();
    {
        ScheduleFormulaScope scope(missing);
        auto v = get(missing, FieldScope::Task, "BS");
        check(v.missing && !v.issue.empty() && !v.value, "missing baseline");
    }
    for (int kind = 0; kind < 5; ++kind) {
        auto bad = p;
        auto &b = bad.baselines.front();
        if (kind == 0)
            b.version = 2;
        if (kind == 1)
            b.schedule.back().taskId = b.schedule.front().taskId;
        if (kind == 2)
            b.projectFinish = "2026-11-14 09:00";
        if (kind == 3)
            b.schedule.front().finish = "2026-11-11 09:00";
        if (kind == 4)
            b.schedule.pop_back();
        ScheduleFormulaScope scope(bad);
        auto v = get(bad, FieldScope::Task, "BW");
        check(!v.value && !v.missing && !v.issue.empty(), "malformed baseline cannot fall back to another version");
    }
    auto removed = p;
    removed.tasks.erase(removed.tasks.begin() + 1);
    removed.assignments.erase(removed.assignments.begin() + 2);
    {
        ScheduleFormulaScope scope(removed);
        check(get(removed, FieldScope::Project, "BW").value == "10.65", "project includes frozen removed tasks");
        check(RollUpValues(removed).cell("G", "field:BW").value == "5.64", "current group uses current membership");
    }
    for (auto s : {FieldScope::Resource, FieldScope::Assignment})
        check(bindXmlFormula(p, s, "[Baseline Duration]").references.empty(), "unsupported bare duration scope");
    check(bindXmlFormula(p, FieldScope::Resource, "[Baseline Start]").references.empty() &&
              bindXmlFormula(p, FieldScope::Task, "[Baseline11 Start]").references.empty(),
          "unsupported baseline slots");
    auto invalid = p;
    invalid.customFields[2].xmlFormula->references.front().scope = FieldScope::Resource;
    rejects([&] { validate(invalid); }, "invalid binding scope");
    check(nativeSchemaFor(p) == 74 && json::decodeProject(json::encodeProject(p), 50) == p, "schema50 roundtrip");
    rejects([&] { json::decodeProject(json::encodeProject(p), 49); }, "schema49 rejects baseline references");
    Project plain;
    plain.documentId = "plain";
    plain.name = "기존";
    plain.startDate = p.startDate;
    plain.tasks = {{"A", "A", 1, {}}};
    check(nativeSchemaFor(plain) == 9, "existing minimum writer unchanged");
    auto inactive = p;
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(inactive, s))
            f.xmlFormula->enabled = false;
    check(!hasXmlBaselineReferences(inactive, true) && nativeSchemaFor(inactive) == 74,
          "inactive bindings retain new schema");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope evaluation(control);
        ScheduleFormulaScope scope(p);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cancel propagates");
    const auto out = QDir::currentPath() + "/xml-baseline-qa";
    QDir().mkpath(out);
    save(out + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    const auto xml = exportProjectXml(p);
    save(out + "/baseline.xml", xml.xml);
    save(out + "/baseline.xer", exportPrimaveraXer(p).xer);
    save(out + "/mapping.json", QJsonDocument(xml.report).toJson());
    auto imported = importProjectXml(xml.xml, "baseline.xml").project;
    check(imported.baselines.empty() && hasXmlBaselineReferences(imported, true), "XML preserves definitions only");
    {
        ScheduleFormulaScope scope(imported);
        auto v = get(imported, FieldScope::Task, imported.customFields.front().id);
        check(!v.value && v.missing, "XML must not fabricate native baselines");
    }
    for (const auto *name : {"fields.xlsx", "fields.pdf", "resources.xlsx", "source.ppm"})
        QFile::remove(out + '/' + name);
    ReportOptions options;
    options.reportId = "xml-baseline";
    options.generatedAt = "2026-11-12T10:00:00Z";
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
    doc.captureBaseline("후속", "PM", "새 버전", "2026-11-12T10:01:00Z");
    verify(*doc.project());
    doc.saveScenario("기준선 참조", "task-order", 260, "2026-11-12T10:02:00Z", doc.project()->documentId,
                     doc.project()->revision);
    verify(json::decodeProject(json::encodeProject(*doc.project()), 50));
    CurrencyConversionSubmission fx;
    fx.rate = {"KRW", "USD", "2026-11-12", "기준선 수식 보존"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 1400;
    fx.reporter = "PM";
    fx.reason = "이력";
    fx.recordedAt = "2026-11-12T10:03:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    check(json::decodeProject(json::encodeProject(*doc.project()), 50) == *doc.project(), "currency archive schema50");
    verify(*doc.project());
    auto observed = *doc.project();
    observed.statusAt = "2026-11-16 09:01";
    observed.progress.push_back(actualEntry(observed, "A0", "A", *observed.statusAt, "2026-11-16 09:00", {}, 1, 3));
    observed.progress.back().planDurationMinutes = 4;
    auto migrated = migrateLedgerIdentities(observed, {{"task", "A", "NEW-A"}});
    check(json::decodeProject(json::encodeProject(migrated), 50) == migrated, "archive nested schema50");
    verify(migrated);
    QTemporaryDir tmp;
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-12T10:03:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "schema50 recovery");
    std::cout << "PASS baseline: frozen v1 dates/calendars/curves/work-driven work, scopes/summaries, "
                 "missing/malformed/cancel, schema50/archives/recovery/outputs\n";
}
void ui() {
    auto p = xmlBaselineFixture();
    QTemporaryDir tmp;
    JsonProjectStore store;
    auto path = tmp.filePath("baseline.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    model.synchronize({0, 1, 2, 3, 4});
    const auto col = model.columnForKey("field:BW");
    check(model.data(model.index(0, col)).toString().contains("5.64"), "table works without current calculation");
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
        check(table->rowCount() == 5 && table->item(0, 1)->text().contains("5.64") &&
                  table->item(4, 1)->text().contains("미설정"),
              "worker preview frozen/unknown");
        auto *refs = dialog->findChild<QComboBox *>("xmlFormulaReferences");
        check(refs->findData("[Baseline Start]") >= 0 && refs->findData("[Baseline Duration]") >= 0 &&
                  refs->findData("[Baseline Work]") >= 0,
              "reference insertion");
        check(dialog->findChild<QLabel *>("xmlFormulaSummary")->text().contains("BASE-1"), "preview provenance");
        auto out = QDir::currentPath() + "/xml-baseline-qa";
        QDir().mkpath(out);
        dialog->resize(800, 800);
        QApplication::processEvents();
        check(dialog->grab().save(out + "/baseline-small.png"), "screenshot");
        finished = true;
        poll.stop();
        dialog->reject();
    });
    poll.start();
    QTimer::singleShot(15000, [&] {
        if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:BW", nullptr);
    check(finished, "preview finished");
    std::cout << "PASS baseline UI: table without current result, worker preview, insertion/provenance\n";
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
        if (const auto *error = dynamic_cast<const AppError *>(&e))
            for (const auto &arg : error->diagnostic().arguments)
                std::cerr << ' ' << arg;
        return 1;
    }
}
