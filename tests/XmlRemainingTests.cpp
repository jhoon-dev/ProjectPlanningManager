#include "XmlRemainingFixture.h"
#include "application/Document.h"
#include "calculation/ScheduleFormulaScope.h"
#include "calculation/Usage.h"
#include "calculation/ResourceViewReport.h"
#include <array>
#include "domain/XmlRollUp.h"
#include "domain/LedgerIdentity.h"
#include "domain/ProjectCurrency.h"
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
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
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write output");
}
XmlFormulaResult get(const Project &p, FieldScope s, const std::string &id, int row = 0) {
    return xmlFormulaValue(p, s, "field:" + id, s == FieldScope::Task ? &p.tasks.at(row) : nullptr,
                           s == FieldScope::Resource ? &p.resources.at(row) : nullptr,
                           s == FieldScope::Assignment ? &p.assignments.at(row) : nullptr);
}
void verify(const Project &p) {
    ScheduleFormulaScope scope(p);
    for (int i = 0; i < 6; ++i) {
        const std::array<std::string, 6> durations{"0", "4", "3", "8", "3", "0"};
        const std::array<std::string, 6> work{"0", "5.32", "1.5", "5.01", "3", "0"};
        check(get(p, FieldScope::Task, "DN", i).value == durations[i], "effective remaining duration");
        check(get(p, FieldScope::Task, "RW", i).value == work[i], "effective remaining work");
        check(parseFieldDuration(*get(p, FieldScope::Task, "RD", i).value).minutes ==
                  std::stoll(durations[i]) * fieldNumberScale,
              "typed duration");
    }
    check(get(p, FieldScope::Project, "RW").value == "14.83" && get(p, FieldScope::Resource, "RW").value == "10.51" &&
              get(p, FieldScope::Resource, "RW", 1).value == "4.32" &&
              get(p, FieldScope::Resource, "RW", 2).value == "0",
          "scope totals and unused resource");
    check(get(p, FieldScope::Assignment, "RW", 1).value == "4" &&
              get(p, FieldScope::Assignment, "RW", 2).value == "1.32" &&
              get(p, FieldScope::Assignment, "TD", 1).value == "4" &&
              get(p, FieldScope::Assignment, "TW", 1).value == "5.32" &&
              get(p, FieldScope::Assignment, "RWResource", 1).value == "10.51" &&
              get(p, FieldScope::Assignment, "PW", 1).value == "14.83",
          "assignment and owner references");
    RollUpValues wbs(p);
    check(wbs.cell("G", "field:RW").value == "14.83" && wbs.cell("H", "field:RW").value == "0" &&
              wbs.cell("G", "field:RD").value.has_value(),
          "group work and unsupported duration");
    RollUpValues groups(p, FieldScope::Task, {{"one", "", {1}}, {"empty", "", {}}});
    check(groups.cell("one", "field:RW").value == "5.32" && !groups.cell("empty", "field:RW").value,
          "filtered/empty summaries");
    RollUpValues resources(p, FieldScope::Resource, {{"all", "", {0, 1, 2}}});
    check(resources.cell("all", "field:RW").value == "14.83", "resource group totals");
}
void contract() {
    auto p = xmlRemainingFixture();
    validate(p);
    check(hasXmlRemainingReferences(p, true), "dependency detection");
    verify(p);
    {
        auto reported = p;
        PeriodActualEntry e;
        e.id = "PA";
        e.taskId = "B";
        e.resourceId = "R";
        e.resourceType = "labor";
        e.date = "2026-11-12";
        e.statusAt = *p.statusAt;
        e.recordedAt = "2026-11-12T10:00:00Z";
        e.reporter = "PM";
        e.reason = "잔여량과 독립인 관측 공수";
        e.sourceDocumentId = p.documentId;
        e.recordedRevision = p.revision;
        e.currency = p.currency;
        e.workUnitMinutes = 99999;
        reported.periodActuals.push_back(e);
        validate(reported);
        ScheduleFormulaScope scope(reported);
        check(get(reported, FieldScope::Task, "RW", 1).value == "5.32",
              "actual ledger is not subtracted from planned work");
    }
    auto result = calculate(p);
    check(result.status == CalculationStatus::Valid && result.isForecast, "valid forecast");
    {
        auto partial = std::make_shared<XmlScheduleValues>(*makeScheduleFormulaValues(p, &result));
        partial->remainingTaskWork.erase("B");
        XmlScheduleScope scope(p, partial);
        check(!RollUpValues(p).cell("G", "field:RW").value, "summary cannot silently omit unknown remaining work");
    }
    for (bool leveled : {false, true}) {
        auto selected = p;
        if (leveled) {
            LevelingSelection selection;
            selection.policy = levelingPolicyFor(selected);
            selection.adoptedAt = "2026-11-12T09:00:00Z";
            selection.reason = "잔여 선택 검사";
            selection.sourceDocumentId = p.documentId;
            selection.sourceRevision = p.revision;
            selected.remainingLeveling = selection;
        } else {
            selected.progressPolicy = ProgressPolicySelection{
                overrideProgressPolicy, "PM", "2026-11-12T09:00:00Z", "진척 선택 검사", p.documentId, p.revision};
        }
        ScheduleFormulaScope scope(selected);
        check(get(selected, FieldScope::Project, "RW").value == "14.83", "selected progress/remaining leveling path");
    }
    auto plan = calculatePlan(p);
    {
        auto stalePlan = plan;
        ++stalePlan.inputRevision;
        ScheduleFormulaScope scope(p, &stalePlan, ScheduleFormulaMode::Plan);
        auto v = get(p, FieldScope::Project, "RW");
        check(!v.value && !v.missing, "explicit stale plan cannot be silently replaced by fresh remaining forecast");
    }
    {
        ScheduleFormulaScope scope(p, &plan, ScheduleFormulaMode::Plan);
        check(get(p, FieldScope::Project, "RW").value == "14.83", "plan-mode exports still use remaining forecast");
        check(get(p, FieldScope::Task, "start", 1).value == normalizeFieldDateTime(plan.tasks[1].start),
              "mixed export keeps planned Start");
    }
    for (auto status : {CalculationStatus::Incomplete, CalculationStatus::Conflict, CalculationStatus::Stale,
                        CalculationStatus::Cancelled}) {
        auto invalid = result;
        invalid.status = status;
        ScheduleFormulaScope scope(p, &invalid, ScheduleFormulaMode::Active, false);
        auto v = get(p, FieldScope::Project, "RW");
        check(!v.value && !v.issue.empty() &&
                  v.missing == (status == CalculationStatus::Incomplete || status == CalculationStatus::Cancelled),
              "invalid forecast state");
    }
    for (int kind = 0; kind < 5; ++kind) {
        auto bad = result;
        if (kind == 0)
            ++bad.inputRevision;
        if (kind == 1)
            bad.statusAt = "2026-11-12 09:01";
        if (kind == 2)
            bad.tasks.back().taskId = bad.tasks.front().taskId;
        if (kind == 3)
            bad.tasks[1].remainingStart = "2026-11-12 09:01";
        if (kind == 4)
            bad.tasks.pop_back();
        ScheduleFormulaScope scope(p, &bad, ScheduleFormulaMode::Active, false);
        auto v = get(p, FieldScope::Project, "RW");
        check(!v.value && !v.missing, "stale/malformed rows rejected");
    }
    {
        ScheduleFormulaScope scope(p, &plan, ScheduleFormulaMode::Active, false);
        check(get(p, FieldScope::Project, "RW").missing, "plan is not remaining");
    }
    auto missing = p;
    missing.statusAt.reset();
    {
        ScheduleFormulaScope scope(missing);
        check(get(missing, FieldScope::Task, "RW").missing, "status required");
    }
    auto corrected = p;
    auto correction = corrected.progress[1];
    correction.id = "B1";
    correction.previousEntryId = "B0";
    correction.remainingMinutes = 2;
    corrected.progress.push_back(correction);
    {
        ScheduleFormulaScope scope(corrected);
        check(get(corrected, FieldScope::Task, "DN", 1).value == "2" &&
                  get(corrected, FieldScope::Task, "RW", 1).value == "3.66",
              "same-status correction and contour tail");
        check(!get(p, FieldScope::Task, "RW", 1).value, "input context identity");
    }
    auto dated = p;
    dated.remainingWork = {{"W", "R", *p.statusAt, {{"2026-11-13", 201}}}};
    {
        ScheduleFormulaScope scope(dated);
        check(get(dated, FieldScope::Task, "RW", 3).value == "5.01" &&
                  get(dated, FieldScope::Task, "DN", 3).value == "13",
              "dated work conservation and effective duration");
    }
    auto usage = buildUsage(p, {"2026-11-12", "2026-11-13", {}, {}, UsagePeriod::Day});
    std::int64_t total = 0;
    for (const auto &row : usage.leaves)
        if (!row.resourceId.empty())
            total += usageTotal(row.values[2]).labor.value();
    check(total == 1483, "Usage reconciliation");
    check(nativeSchemaFor(p) == 74 && json::decodeProject(json::encodeProject(p), 49) == p, "schema49");
    bool rejected = false;
    try {
        json::decodeProject(json::encodeProject(p), 48);
    } catch (const AppError &) {
        rejected = true;
    }
    check(rejected, "schema48 rejects new bindings");
    for (auto s : {FieldScope::Resource, FieldScope::Assignment})
        check(bindXmlFormula(p, s, "[Remaining Duration]").references.empty(), "unsupported bare duration scope");
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
    const auto out = QDir::currentPath() + "/xml-remaining-qa";
    QDir().mkpath(out);
    save(out + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    QJsonArray rows;
    for (const auto &row : result.tasks)
        rows.append(QJsonObject{{"taskId", QString::fromStdString(row.taskId)},
                                {"start", QString::fromStdString(row.start)},
                                {"finish", QString::fromStdString(row.finish)},
                                {"remainingStart", QString::fromStdString(row.remainingStart.value_or(row.start))},
                                {"completed", row.completedActual}});
    save(out + "/forecast.json", QJsonDocument(rows).toJson());
    auto xml = exportProjectXml(p);
    save(out + "/remaining.xml", xml.xml);
    save(out + "/remaining.xer", exportPrimaveraXer(p).xer);
    save(out + "/mapping.json", QJsonDocument(xml.report).toJson());
    auto imported = importProjectXml(xml.xml, "remaining.xml").project;
    check(imported.progress.empty() && hasXmlRemainingReferences(imported, true),
          "XML definitions without fabricated actuals");
    for (const auto *name : {"fields.xlsx", "fields.pdf", "resources.xlsx", "source.ppm"})
        QFile::remove(out + '/' + name);
    ReportOptions options;
    options.reportId = "xml-remaining";
    options.generatedAt = "2026-11-12T10:00:00Z";
    auto report = buildReport(p, options);
    exportXlsx(report, out + "/fields.xlsx");
    exportPdf(report, out + "/fields.pdf");
    exportXlsx(resourceViewReport(p, {}, options), out + "/resources.xlsx");
    JsonProjectStore store;
    const auto path = (out + "/source.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    verify(*doc.project());
    doc.captureBaseline("잔여 정의", "PM", "동결", "2026-11-12T10:01:00Z");
    doc.saveScenario("잔여 원장", "task-order", 260, "2026-11-12T10:02:00Z", doc.project()->documentId,
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
    auto decoded = json::decodeProject(json::encodeProject(*doc.project()), 49);
    check(decoded == *doc.project(), "nested schema49");
    verify(decoded);
    auto migrated = migrateLedgerIdentities(decoded, {{"task", "W", "NEW"}});
    check(json::decodeProject(json::encodeProject(migrated), 49) == migrated, "ID archive schema49");
    verify(migrated);
    QTemporaryDir tmp;
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-12T10:04:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "recovery49");
    std::cout << "PASS remaining references: effective calendar/curve/work-driven/dated forecast, scopes/summaries, "
                 "missing/stale/cancel, plan export isolation, schema49/archives/recovery/outputs\n";
}
void ui() {
    auto p = xmlRemainingFixture();
    QTemporaryDir tmp;
    JsonProjectStore store;
    auto path = tmp.filePath("remaining.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    TaskTableModel model(doc);
    doc.applyCalculation(calculate(*doc.project()));
    model.synchronize({0, 1, 2, 3, 4, 5});
    const auto col = model.columnForKey("field:RW");
    check(model.data(model.index(1, col)).toString().contains("5.32"), "table current forecast");
    doc.setStatusAt({});
    model.synchronize({0, 1, 2, 3, 4, 5});
    check(!model.data(model.index(1, col)).toString().contains("5.32"), "status invalidates remaining cells");
    doc.undo();
    doc.applyCalculation(calculate(*doc.project()));
    model.synchronize({0, 1, 2, 3, 4, 5});
    check(model.data(model.index(1, col)).toString().contains("5.32"), "undo/recalculation restores forecast");
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
        check(table->rowCount() == 6 && table->item(1, 1)->text().contains("5.32"), "worker preview");
        auto *refs = dialog->findChild<QComboBox *>("xmlFormulaReferences");
        check(refs->findData("[Remaining Duration]") >= 0 && refs->findData("[Remaining Work]") >= 0,
              "reference insertion");
        check(dialog->findChild<QLabel *>("xmlFormulaSummary")->text().contains("잔여 예측"), "forecast provenance");
        auto out = QDir::currentPath() + "/xml-remaining-qa";
        QDir().mkpath(out);
        dialog->resize(800, 730);
        QApplication::processEvents();
        check(dialog->grab().save(out + "/remaining-small.png"), "screenshot");
        finished = true;
        poll.stop();
        dialog->reject();
    });
    poll.start();
    QTimer::singleShot(15000, [&] {
        if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            d->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:RW", nullptr);
    check(finished, "preview finished");
    std::cout << "PASS remaining UI: calculated table, worker preview, insertion/provenance/small layout\n";
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
