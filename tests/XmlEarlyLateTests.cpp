#include "XmlEarlyLateFixture.h"
#include "XmlRemainingFixture.h"
#include "application/Document.h"
#include "calculation/ScheduleFormulaScope.h"
#include "domain/XmlRollUp.h"
#include "domain/XmlRollDown.h"
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
    ScheduleFormulaScope scope(p);
    const bool forecast = p.statusAt.has_value();
    const auto bStart = forecast ? "2026-11-12T09:07:00" : "2026-11-12T09:06:00";
    const auto bFinish = forecast ? "2026-11-13T09:03:00" : "2026-11-13T09:02:00";
    const auto bLateStart = forecast ? "2026-11-13T09:00:00" : bStart;
    const auto finish = forecast ? "2026-11-13T09:04:00" : bFinish;
    check(get(p, FieldScope::Task, "ES", 1).value == bStart && get(p, FieldScope::Task, "EF", 1).value == bFinish &&
              get(p, FieldScope::Task, "LS", 1).value == bLateStart &&
              get(p, FieldScope::Task, "LF", 1).value == finish,
          "task forward/resume and backward dates");
    check(get(p, FieldScope::Task, "ES", 2).value == (forecast ? "2026-11-13T09:02:00" : "2026-11-12T09:02:00") &&
              get(p, FieldScope::Task, "LS", 2).value == (forecast ? "2026-11-13T09:02:00" : "2026-11-12T09:04:00"),
          "mixed calendar boundaries");
    check(get(p, FieldScope::Task, "delay", 1).value == (forecast ? "1" : "0") &&
              get(p, FieldScope::Task, "delay", 2).value == (forecast ? "0" : "2"),
          "working-minute backward distance");
    check(get(p, FieldScope::Task, "LS", 3).value == finish && get(p, FieldScope::Task, "LF", 3).value == finish,
          "zero-duration milestone boundary");
    for (const auto *key : {"ES", "EF", "LS", "LF"}) {
        const auto a = get(p, FieldScope::Task, key);
        check(forecast ? (!a.value && a.missing) : a.value.has_value(), "completed actuals have no future dates");
        check(get(p, FieldScope::Assignment, key, 1).value == get(p, FieldScope::Task, key, 1).value,
              "assignment owner");
        check(get(p, FieldScope::Resource, key).value == get(p, FieldScope::Project, key).value,
              "resource project reference");
    }
    check(get(p, FieldScope::Project, "ES").value == (forecast ? bStart : "2026-11-12T09:00:00") &&
              get(p, FieldScope::Project, "LF").value == finish,
          "eligible project range");
    RollUpValues groups(p);
    check(groups.cell("G", "field:ES").value == get(p, FieldScope::Project, "ES").value &&
              groups.cell("G", "field:LF").value == finish,
          "WBS excludes completed actuals");
    RollUpValues filtered(p, FieldScope::Task, {{"one", "", {2}}, {"empty", "", {}}});
    check(filtered.cell("one", "field:LS").value == get(p, FieldScope::Task, "LS", 2).value &&
              !filtered.cell("empty", "field:LS").value,
          "filtered and empty ranges");
}
void contract() {
    auto p = xmlEarlyLateFixture();
    validate(p);
    verify(p);
    verify(xmlEarlyLateFixture(false));
    check(!get(p, FieldScope::Task, "ES").value && !get(p, FieldScope::Task, "ES").missing, "explicit schedule scope");
    auto result = calculate(p);
    for (auto status : {CalculationStatus::Incomplete, CalculationStatus::Conflict, CalculationStatus::Stale,
                        CalculationStatus::Cancelled}) {
        auto invalid = result;
        invalid.status = status;
        ScheduleFormulaScope scope(p, &invalid, ScheduleFormulaMode::Active, false);
        auto v = get(p, FieldScope::Task, "ES", 1);
        check(!v.value && !v.issue.empty() &&
                  v.missing == (status == CalculationStatus::Incomplete || status == CalculationStatus::Cancelled),
              "unavailable current schedule");
    }
    for (int kind = 0; kind < 4; ++kind) {
        auto invalid = result;
        if (kind == 0)
            ++invalid.inputRevision;
        if (kind == 1)
            invalid.tasks.back().taskId = invalid.tasks.front().taskId;
        if (kind == 2)
            invalid.tasks.pop_back();
        if (kind == 3)
            invalid.statusAt = "2026-11-13 09:00";
        ScheduleFormulaScope scope(p, &invalid, ScheduleFormulaMode::Active, false);
        auto v = get(p, FieldScope::Task, "LF", 1);
        check(!v.value && !v.missing, "stale or malformed result rejected");
    }
    {
        auto unavailable = result;
        unavailable.forecastFloatCalculated = false;
        ScheduleFormulaScope scope(p, &unavailable, ScheduleFormulaMode::Active, false);
        check(get(p, FieldScope::Task, "ES", 1).value.has_value() && get(p, FieldScope::Task, "LS", 1).missing,
              "missing backward calculation does not erase Early");
    }
    for (int kind = 0; kind < 3; ++kind) {
        auto bad = result;
        if (kind == 0)
            bad.tasks[1].lateStartMinutes = -1;
        if (kind == 1)
            bad.tasks[1].lateFinishMinutes = 999999999;
        if (kind == 2)
            bad.tasks[1].lateStartMinutes = 0;
        ScheduleFormulaScope scope(p, &bad, ScheduleFormulaMode::Active, false);
        auto v = get(p, FieldScope::Task, "LS", 1);
        check(!v.value && !v.missing && get(p, FieldScope::Task, "ES", 1).value.has_value(),
              "invalid Late isolated from Early");
    }
    auto base = xmlEarlyLateFixture(false);
    auto alap = base;
    alap.calculationProfile = constraintProfile;
    alap.tasks[2].constraintType = ConstraintType::ALAP;
    {
        ScheduleFormulaScope scope(alap);
        check(get(alap, FieldScope::Task, "ES", 2).value == "2026-11-12T09:04:00" &&
                  get(alap, FieldScope::Task, "ES", 2).value == get(alap, FieldScope::Task, "LS", 2).value,
              "Early follows ALAP placement, not an invented ASAP schedule");
    }
    auto leveled = base;
    leveled.resources[0].maxUnits = 100;
    LevelingSelection selection;
    selection.policy = levelingPolicyFor(leveled);
    selection.adoptedAt = "2026-11-12T10:00:00Z";
    selection.reason = "후진 검증";
    selection.sourceDocumentId = leveled.documentId;
    selection.sourceRevision = leveled.revision;
    leveled.planLeveling = selection;
    {
        auto r = calculatePlan(leveled);
        check(r.status == CalculationStatus::Valid && r.resourceLeveled && r.levelingFloatCalculated,
              "adopted leveling result");
        ScheduleFormulaScope scope(leveled, &r);
        check(get(leveled, FieldScope::Task, "LS").value.has_value(), "verified resource-order Late");
        r.levelingFloatCalculated = false;
        ScheduleFormulaScope missing(leveled, &r);
        check(get(leveled, FieldScope::Task, "LS").missing, "unavailable resource-order Late is not logical Late");
    }
    auto basic = base;
    basic.calculationProfile = "native-fs0-workminutes-v1";
    basic.calendars.clear();
    basic.holidays = {"2026-11-13"};
    for (auto &t : basic.tasks)
        t.calendarId.clear();
    basic.resources[0].calendarId.clear();
    basic.tasks[0].durationMinutes = basic.tasks[1].durationMinutes = 480;
    basic.tasks[2].durationMinutes = 0;
    {
        ScheduleFormulaScope scope(basic);
        check(get(basic, FieldScope::Task, "ES", 3).value == "2026-11-12T18:00:00" &&
                  get(basic, FieldScope::Task, "LS", 3).value == "2026-11-16T18:00:00" &&
                  get(basic, FieldScope::Task, "LF", 1).value == "2026-11-16T18:00:00",
              "compressed work clock and holiday/weekend milestone");
    }
    for (bool dated : {false, true}) {
        auto effort = xmlRemainingFixture();
        effort.customFields = base.customFields;
        effort.projectFields = base.projectFields;
        effort.resourceFields = base.resourceFields;
        effort.assignmentFields = base.assignmentFields;
        if (dated)
            effort.remainingWork = {{"W", "R", *effort.statusAt, {{"2026-11-13", 201}}}};
        const auto r = calculate(effort);
        check(r.status == CalculationStatus::Valid && r.isForecast, "curve/work-driven/dated forecast");
        ScheduleFormulaScope scope(effort, &r);
        for (std::size_t i = 0; i < r.tasks.size(); ++i) {
            if (r.tasks[i].completedActual)
                continue;
            check(get(effort, FieldScope::Task, "ES", int(i)).value ==
                      normalizeFieldDateTime(r.tasks[i].remainingStart.value_or(r.tasks[i].start)),
                  "effort Early uses exact remaining boundary");
            check(hasScheduleFloat(r, r.tasks[i]) == get(effort, FieldScope::Task, "LS", int(i)).value.has_value(),
                  "effort Late follows verified curved/dated capability");
        }
    }
    auto allDone = p;
    allDone.progress.clear();
    for (const auto &t : p.tasks) {
        auto e = actualEntry(allDone, t.id, t.id, *p.statusAt, "2026-11-12 09:00", "2026-11-12 09:01", 1, 0);
        e.planDurationMinutes = t.durationMinutes;
        allDone.progress.push_back(e);
    }
    {
        ScheduleFormulaScope scope(allDone);
        check(get(allDone, FieldScope::Project, "ES").missing && !RollUpValues(allDone).cell("G", "field:LS").value,
              "all completed project and group have no future range");
    }
    check(nativeSchemaFor(p) == 51 && json::decodeProject(json::encodeProject(p), 51) == p, "schema51");
    rejects([&] { json::decodeProject(json::encodeProject(p), 50); }, "schema50 rejects new bindings");
    auto invalid = p;
    invalid.customFields.front().xmlFormula->references.front().scope = FieldScope::Resource;
    rejects([&] { validate(invalid); }, "invalid bare reference scope");
    check(bindXmlFormula(p, FieldScope::Resource, "[Early Start]").references.empty() &&
              bindXmlFormula(p, FieldScope::Assignment, "[Late Finish]").references.empty(),
          "unsupported bare references");
    auto cancel = std::make_shared<CalculationControl>();
    cancel->cancel();
    bool cancelled = false;
    try {
        EvaluationScope evaluation(cancel);
        ScheduleFormulaScope scope(p);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cancel propagates");
    const auto out = QDir::currentPath() + "/xml-early-late-qa";
    QDir().mkpath(out);
    save(out + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    const auto xml = exportProjectXml(p);
    save(out + "/timing.xml", xml.xml);
    save(out + "/timing.xer", exportPrimaveraXer(p).xer);
    save(out + "/mapping.json", QJsonDocument(xml.report).toJson());
    check(hasXmlEarlyLateReferences(importProjectXml(xml.xml, "timing.xml").project, true), "XML definition roundtrip");
    for (const auto *name : {"fields.xlsx", "fields.pdf", "source.ppm"})
        QFile::remove(out + '/' + name);
    ReportOptions options;
    options.reportId = "xml-early-late";
    options.generatedAt = "2026-11-12T10:00:00Z";
    auto report = buildReport(p, options);
    exportXlsx(report, out + "/fields.xlsx");
    exportPdf(report, out + "/fields.pdf");
    JsonProjectStore store;
    auto path = (out + "/source.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    verify(*doc.project());
    doc.captureBaseline("일정", "PM", "동결", "2026-11-12T10:01:00Z");
    doc.saveScenario("후진 참조", "task-order", 260, "2026-11-12T10:02:00Z", doc.project()->documentId,
                     doc.project()->revision);
    CurrencyConversionSubmission fx;
    fx.rate = {"KRW", "USD", "2026-11-12", "이력"};
    fx.rate.numerator = 1;
    fx.rate.denominator = 1400;
    fx.reporter = "PM";
    fx.reason = "보존";
    fx.recordedAt = "2026-11-12T10:03:00Z";
    fx.confirmed = true;
    doc.convertCurrency(fx);
    check(json::decodeProject(json::encodeProject(*doc.project()), 51) == *doc.project(),
          "currency/baseline/scenario archives");
    verify(*doc.project());
    auto migrated = migrateLedgerIdentities(*doc.project(), {{"task", "A", "NEW-A"}});
    check(json::decodeProject(json::encodeProject(migrated), 51) == migrated, "ID archive");
    verify(migrated);
    QTemporaryDir tmp;
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-12T10:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates.front().valid, "schema51 recovery");
    std::cout << "PASS Early/Late: plan/remaining/ALAP/resource-order/clock/milestones, capabilities/stale/cancel, "
                 "schema51/archives/recovery/outputs\n";
}
void ui() {
    auto p = xmlEarlyLateFixture();
    QTemporaryDir tmp;
    JsonProjectStore store;
    auto path = tmp.filePath("timing.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    doc.applyCalculation(calculate(*doc.project()));
    model.synchronize({0, 1, 2, 3});
    auto col = model.columnForKey("field:LS");
    check(model.data(model.index(1, col)).toString().contains("2026-11-13"), "current table forecast");
    doc.setStatusAt({});
    model.synchronize({0, 1, 2, 3});
    check(!model.data(model.index(1, col)).toString().contains("2026-11-13"), "stale cells cleared after edit");
    doc.undo();
    doc.applyCalculation(calculate(*doc.project()));
    model.synchronize({0, 1, 2, 3});
    check(model.data(model.index(1, col)).toString().contains("2026-11-13"),
          "undo and fresh calculation restore dates");
    QTimer poll;
    poll.setInterval(25);
    bool done = false;
    QObject::connect(&poll, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d || d->property("xmlFormulaBusy").toBool())
            return;
        auto *table = d->findChild<QTableWidget *>("xmlFormulaPreview");
        if (!table)
            return;
        check(table->rowCount() == 4 && table->item(0, 1)->text().contains("미설정") &&
                  table->item(1, 1)->text().contains("09:00"),
              "worker preview");
        auto *refs = d->findChild<QComboBox *>("xmlFormulaReferences");
        check(refs->findData("[Early Start]") >= 0 && refs->findData("[Late Finish]") >= 0, "reference insertion");
        check(d->findChild<QLabel *>("xmlFormulaSummary")->text().contains("Early/Late"), "provenance");
        auto out = QDir::currentPath() + "/xml-early-late-qa";
        QDir().mkpath(out);
        d->resize(800, 800);
        QApplication::processEvents();
        check(d->grab().save(out + "/timing-small.png"), "screenshot");
        done = true;
        poll.stop();
        d->reject();
    });
    poll.start();
    QTimer::singleShot(15000, [&] {
        if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:LS", nullptr);
    check(done, "preview finished");
    std::cout << "PASS Early/Late UI: active result, worker preview, completed null, references/provenance\n";
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
