#include "XmlFormulaFixture.h"
#include "application/Document.h"
#include "calculation/ScheduleFormulaScope.h"
#include "calculation/Reports.h"
#include "domain/FormulaFields.h"
#include "domain/XmlRollUp.h"
#include "domain/XmlRollDown.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/JsonProjectStore.h"
#include "reporting/ReportExport.h"
#include "ui/TaskTableModel.h"
#include "ui/XmlFormulaDialog.h"
#include "ui/ClassificationDialogs.h"
#include <QTableView>
#include <QApplication>
#include <QFontDatabase>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTableWidget>
#include <QTimer>
#include <QTest>
#include <QTemporaryDir>
#include <iostream>
using namespace ppm;
namespace {
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
void save(const QString &path, const QByteArray &data) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(data) == data.size(), "write");
}
void add(Project &p, FieldScope scope, std::string id, CustomFieldType type, std::string formula) {
    CustomField f{id, id, type, "", {}};
    customFields(p, scope).push_back(f);
    auto &out = customFields(p, scope).back();
    out.xmlFormula = bindXmlFormula(p, scope, formula);
    out.xmlFormula->enabled = true;
}
Project fixture() {
    Project p;
    p.documentId = "xml-schedule";
    p.name = "일정 참조";
    p.startDate = "2026-11-11";
    p.calculationProfile = pdmProfile;
    p.wbsNodes = {{"G", "요약", {}}};
    p.tasks = {{"A", "설계", 480, {}}, {"B", "검토", 120, {"A"}}};
    for (auto &t : p.tasks)
        t.wbsId = "G";
    Resource r;
    r.id = "R";
    r.name = "담당";
    p.resources = {r};
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    add(p, FieldScope::Task, "s", CustomFieldType::Start, "[Start]");
    add(p, FieldScope::Task, "f", CustomFieldType::Finish, "[Finish]");
    add(p, FieldScope::Task, "elapsed", CustomFieldType::Number, "DateDiff(\"n\",[Start],[Finish])");
    add(p, FieldScope::Task, "global", CustomFieldType::Finish, "[Project.Finish]");
    p.customFields[0].xmlRollUp = XmlRollUp{2, 0};
    p.customFields[1].xmlRollUp = XmlRollUp{2, 0};
    p.customFields[2].xmlRollUp = XmlRollUp{2, 0};
    p.customFields.push_back({"native", "native", CustomFieldType::Number, "", {}, "[field:elapsed]*2"});
    add(p, FieldScope::Project, "pf", CustomFieldType::Finish, "[Finish]");
    add(p, FieldScope::Resource, "rf", CustomFieldType::Finish, "[Project.Finish]");
    add(p, FieldScope::Assignment, "as", CustomFieldType::Start, "[Task.Start]");
    p.tasks[0].attributes = {{"field:s", "2000-01-01T00:00:00"}};
    p.assignmentFields.push_back({"copy", "copy", CustomFieldType::Start, "", {}});
    p.customFields[0].xmlRollDown = XmlRollDown{"copy", true};
    validate(p);
    return p;
}
std::optional<std::string> value(const Project &p, const std::string &key, int row = 0) {
    return resolvedAttributeValue(p, p.tasks.at(row), "field:" + key);
}
void contract() {
    auto p = fixture();
    EvaluationScope evaluation;
    check(!value(p, "s"), "no implicit scheduler/raw fallback");
    auto result = calculatePlan(p);
    check(result.status == CalculationStatus::Valid, "schedule valid");
    {
        ScheduleFormulaScope scope(p, &result, ScheduleFormulaMode::Active, false);
        check(value(p, "s") == "2026-11-11T09:00:00" && value(p, "f") == "2026-11-11T18:00:00", "planned A");
        check(value(p, "s", 1) == "2026-11-12T09:00:00" && value(p, "f", 1) == "2026-11-12T11:00:00", "dependency B");
        check(value(p, "elapsed") == "540" && value(p, "native") == "1080", "elapsed time and mixed native chain");
        check(value(p, "global") == "2026-11-12T11:00:00", "project range");
        check(xmlFormulaValue(p, FieldScope::Project, "field:pf").value == value(p, "global"), "project scope");
        check(xmlFormulaValue(p, FieldScope::Resource, "field:rf", nullptr, &p.resources[0]).value ==
                  value(p, "global"),
              "resource project scope");
        check(xmlFormulaValue(p, FieldScope::Assignment, "field:as", nullptr, nullptr, &p.assignments[1]).value ==
                  value(p, "s", 1),
              "assignment owner");
        check(RollDownValues(p).value(p.assignments[1], "field:copy") == value(p, "s", 1), "scheduled roll-down");
        RollUpValues wbs(p);
        check(wbs.cell("G", "field:s").value == value(p, "s") && wbs.cell("G", "field:f").value == value(p, "f", 1) &&
                  wbs.cell("G", "field:elapsed").value == "1560",
              "WBS calculated range");
        RollUpValues filtered(p, FieldScope::Task, {{"one", "", {1}}});
        check(filtered.cell("one", "field:s").value == value(p, "s", 1), "filtered range");
        auto other = p;
        check(!value(other, "s"), "exact snapshot identity");
        {
            XmlScheduleScope blocked;
            check(!value(p, "s"), "blocked context/cache isolation");
        }
        check(value(p, "s") == "2026-11-11T09:00:00", "nested scope restored");
    }
    check(!value(p, "s") && !value(p, "native"), "cache does not leak dates");
    {
        ScheduleFormulaScope scope(p, nullptr, ScheduleFormulaMode::Active, false);
        auto missing = xmlFormulaValue(p, FieldScope::Task, "field:s", &p.tasks[0]);
        check(missing.missing && missing.issue.find("계산한 후") != std::string::npos,
              "missing schedule reason survives field conversion");
    }
    auto invalid = result;
    invalid.inputRevision++;
    {
        ScheduleFormulaScope scope(p, &invalid, ScheduleFormulaMode::Active, false);
        check(!value(p, "s"), "stale rejected");
    }
    invalid = result;
    invalid.tasks.pop_back();
    {
        ScheduleFormulaScope scope(p, &invalid, ScheduleFormulaMode::Active, false);
        check(!value(p, "s"), "partial rejected");
    }
    invalid = result;
    invalid.tasks[1].taskId = "A";
    {
        ScheduleFormulaScope scope(p, &invalid, ScheduleFormulaMode::Active, false);
        check(!value(p, "s"), "duplicate rejected");
    }
    for (auto status : {CalculationStatus::Incomplete, CalculationStatus::Conflict, CalculationStatus::Stale}) {
        invalid = result;
        invalid.status = status;
        ScheduleFormulaScope scope(p, &invalid, ScheduleFormulaMode::Active, false);
        check(!value(p, "s"), "invalid schedule rejected");
    }
    invalid = result;
    invalid.status = CalculationStatus::Cancelled;
    {
        ScheduleFormulaScope scope(p, &invalid);
        check(!value(p, "s"), "cancelled result missing safely");
    }
    auto cancelled = std::make_shared<CalculationControl>();
    cancelled->cancel();
    bool escaped = false;
    try {
        EvaluationScope cancelledScope(cancelled);
        ScheduleFormulaScope scope(p);
    } catch (const CalculationCancelled &) {
        escaped = true;
    }
    check(escaped, "active cancellation propagates");
    for (auto scope : {FieldScope::Task, FieldScope::Resource, FieldScope::Assignment}) {
        auto x = bindXmlFormula(p, scope, scope == FieldScope::Task ? "[Resource.Start]" : "[Start]");
        check(x.references.empty(), "unsupported owner dates do not bind");
    }
    invalid = result;
    invalid.isForecast = true;
    invalid.statusAt = "2026-11-12 18:00";
    {
        ScheduleFormulaScope scope(p, &invalid, ScheduleFormulaMode::Active, false);
        check(!value(p, "s"), "forecast status mismatch");
    }
    auto holiday = p;
    holiday.holidays = {"2026-11-11"};
    {
        ScheduleFormulaScope scope(holiday, nullptr, ScheduleFormulaMode::Plan);
        check(value(holiday, "s") == "2026-11-12T09:00:00", "holiday schedule");
    }
    auto effort = p;
    effort.calculationProfile = effortProfile;
    effort.tasks[0].taskType = TaskType::FixedWork;
    effort.tasks[0].workDriven = true;
    effort.tasks[0].workUnitMinutes = 60000;
    {
        ScheduleFormulaScope scope(effort, nullptr, ScheduleFormulaMode::Plan);
        check(value(effort, "f") == "2026-11-12T11:00:00", "FixedWork computed finish");
    }
    auto overlap = p;
    overlap.tasks[1].dependencyRules = {{"A", DependencyType::SS, 60}};
    {
        ScheduleFormulaScope scope(overlap, nullptr, ScheduleFormulaMode::Plan);
        check(value(overlap, "s", 1) == "2026-11-11T10:00:00", "SS and lag calculated date");
    }
    auto milestone = p;
    milestone.tasks[1].durationMinutes = 0;
    {
        ScheduleFormulaScope scope(milestone, nullptr, ScheduleFormulaMode::Plan);
        check(value(milestone, "s", 1) == value(milestone, "f", 1), "milestone");
    }
    auto legacy = p;
    legacy.customFields.push_back({"old", "Project.Start", CustomFieldType::Start, "", {}});
    auto bound = bindXmlFormula(legacy, FieldScope::Task, "[Project.Start]",
                                {{"Project.Start", "field:old", FieldScope::Task, true}});
    check(bound.references.front().key == "field:old" &&
              bindXmlFormula(legacy, FieldScope::Task, "[Project.Start]").references.front().key == "builtin:start",
          "existing binding survives new builtin");
    auto body = json::encodeProject(p);
    check(json::decodeProject(body, 45) == p, "schema45 payload");
    bool old = false;
    try {
        json::decodeProject(body, 44);
    } catch (const AppError &) {
        old = true;
    }
    check(old, "schema44 rejects new bindings");
    p.statusAt = "2026-11-13 18:00";
    auto forecast = calculate(p);
    check(forecast.isForecast && forecast.tasks[0].start != result.tasks[0].start, "forecast differs");
    {
        ScheduleFormulaScope scope(p, &forecast, ScheduleFormulaMode::Active, false);
        check(value(p, "s") == normalizeFieldDateTime(forecast.tasks[0].start), "forecast reference");
        ScheduleFormulaScope plan(p, nullptr, ScheduleFormulaMode::Plan);
        check(value(p, "s") == "2026-11-11T09:00:00", "nested explicit plan");
    }
    auto actual = p;
    ProgressEntry entry;
    entry.id = "fact";
    entry.taskId = "A";
    entry.statusAt = *actual.statusAt;
    entry.recordedAt = "2026-11-13T10:00:00Z";
    entry.reporter = "PM";
    entry.reason = "완료";
    entry.sourceDocumentId = actual.documentId;
    entry.recordedRevision = actual.revision;
    entry.planDurationMinutes = 480;
    entry.actualStart = "2026-11-12 09:00";
    entry.actualFinish = "2026-11-12 18:00";
    entry.actualDurationMinutes = 480;
    actual.progress = {entry};
    {
        ScheduleFormulaScope scope(actual, nullptr, ScheduleFormulaMode::Active);
        check(value(actual, "s") == "2026-11-12T09:00:00" && value(actual, "f") == "2026-11-12T18:00:00",
              "completed actual forecast dates");
    }
    auto out = QDir::currentPath() + "/xml-schedule-qa";
    QDir().mkpath(out);
    auto xml = exportProjectXml(p);
    save(out + "/plan.xml", xml.xml);
    save(out + "/plan.xer", exportPrimaveraXer(p).xer);
    auto imported = importProjectXml(xml.xml, "schedule.xml");
    {
        ScheduleFormulaScope scope(imported.project, nullptr, ScheduleFormulaMode::Plan);
        check(resolvedAttributeValue(imported.project, imported.project.tasks[0],
                                     "field:" + imported.project.customFields[0].id)
                  .has_value(),
              "XML reimport enabled");
    }
    ReportOptions options;
    options.reportId = "xml-schedule";
    options.generatedAt = "2026-11-11T04:00:00Z";
    auto report = buildReport(p, options);
    QFile::remove(out + "/forecast.xlsx");
    QFile::remove(out + "/forecast.pdf");
    exportXlsx(report, out + "/forecast.xlsx");
    exportPdf(report, out + "/forecast.pdf");
    QJsonArray rows;
    for (auto *r : {&result, &forecast})
        for (const auto &t : r->tasks)
            rows.append(QJsonObject{{"basis", r->isForecast ? "forecast" : "plan"},
                                    {"id", QString::fromStdString(t.taskId)},
                                    {"start", QString::fromStdString(t.start)},
                                    {"finish", QString::fromStdString(t.finish)}});
    save(out + "/schedule.json", QJsonDocument(rows).toJson());
    JsonProjectStore store;
    QFile::remove(out + "/source.ppm");
    store.save((out + "/source.ppm").toStdString(), p, {});
    check(decodeProjectFile(QFile(out + "/source.ppm").exists() ? ([&] {
              QFile f(out + "/source.ppm");
              f.open(QIODevice::ReadOnly);
              return f.readAll();
          })()
                                                                : QByteArray{}) == p,
          "schema45 file");
    QTemporaryDir archive;
    auto archivedPath = archive.filePath("archived.ppm").toStdString();
    store.save(archivedPath, p, {});
    Document d(store);
    d.open(archivedPath);
    d.captureBaseline("동결", "PM", "일정 식", "2026-11-11T04:01:00Z");
    d.saveScenario("사본", "task-order", 260, "2026-11-11T04:02:00Z", d.project()->documentId, d.project()->revision);
    check(json::decodeProject(json::encodeProject(*d.project()), 45) == *d.project(), "frozen snapshots45");
    JsonRecoveryStore recovery(archive.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-11T04:03:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "recovery45");
    std::cout << "PASS schedule references: typed/date arithmetic, four scopes, summaries, cache isolation, "
                 "invalid/stale/cancel, plan/forecast, schema/XML/report\n";
}
void ui() {
    auto p = fixture();
    QTemporaryDir tmp;
    JsonProjectStore store;
    auto path = tmp.filePath("source.ppm");
    store.save(path.toStdString(), p, {});
    Document doc(store);
    doc.open(path.toStdString());
    TaskTableModel model(doc);
    doc.applyCalculation(calculate(*doc.project()));
    model.synchronize({0, 1});
    bool found = false;
    for (int c = 0; c < model.columnCount(); ++c)
        if (model.headerData(c, Qt::Horizontal, Qt::DisplayRole).toString() == "s") {
            check(model.data(model.index(0, c), Qt::DisplayRole).toString().contains("2026-11-11"), "table schedule");
            found = true;
        }
    check(found, "schedule column");
    auto c = model.columnForKey("field:s");
    doc.setDuration(0, 960);
    model.synchronize({0, 1});
    check(!model.data(model.index(1, c), Qt::DisplayRole).toString().contains("2026-11-12"), "stale table invalidated");
    doc.applyCalculation(calculate(*doc.project()));
    model.synchronize({0, 1});
    check(model.data(model.index(1, c), Qt::DisplayRole).toString().contains("2026-11-13"), "dependent row updates");
    doc.undo();
    doc.applyCalculation(calculate(*doc.project()));
    model.synchronize({0, 1});
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
        finished = true;
        check(table->rowCount() == 2 && table->item(0, 1)->text().contains("2026-11-11"), "async preview");
        auto out = QDir::currentPath() + "/xml-schedule-qa";
        QDir().mkpath(out);
        dialog->resize(800, 650);
        QApplication::processEvents();
        check(dialog->grab().save(out + "/schedule-small.png"), "screenshot");
        dialog->reject();
    });
    poll.start();
    QTimer::singleShot(15000, [&] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    editXmlFormula(doc, FieldScope::Task, "field:s", nullptr);
    check(finished, "preview finished");
    poll.stop();
    bool refreshed = false;
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        check(dialog, "object dialog");
        auto *values = dialog->findChild<QTableView *>("objectFieldValues");
        check(values && values->model()->data(values->model()->index(0, 2)).toString().contains("2026-11-12"),
              "project calculated value");
        auto cancelled = *doc.calculation();
        cancelled.status = CalculationStatus::Cancelled;
        doc.applyCalculation(cancelled);
        QTimer::singleShot(400, dialog, [&, dialog, values] {
            check(!values->model()->data(values->model()->index(0, 2)).toString().contains("2026-11-12"),
                  "object dialog generation refresh after cancelled result");
            refreshed = true;
            dialog->reject();
        });
    });
    editObjectAttributes(doc, FieldScope::Project, nullptr);
    check(refreshed, "object refresh finished");
    std::cout << "PASS schedule reference table and asynchronous preview\n";
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
            std::cerr << " " << s;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
