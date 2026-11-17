#include "application/Document.h"
#include "calculation/ResourceAvailabilityQuery.h"
#include "calculation/ScheduleFormulaScope.h"
#include "calculation/Reports.h"
#include "domain/Classification.h"
#include "domain/XmlFormula.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "reporting/ReportExport.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <algorithm>
#include <cstdio>
#include <tuple>
namespace {
using namespace jobs;
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f, const char *why) {
    bool rejected = false;
    try {
        f();
    } catch (const AppError &) {
        rejected = true;
    }
    check(rejected, why);
}
void save(const QString &path, const QByteArray &data) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size(), "save artifact");
}
void add(Project &p, FieldScope scope, const std::string &id, const std::string &source) {
    auto &fields = customFields(p, scope);
    fields.push_back({id, id, CustomFieldType::Number, "", {}});
    fields.back().xmlFormula = bindXmlFormula(p, scope, source);
    fields.back().xmlFormula->enabled = true;
}
Project fixture() {
    Project p;
    p.documentId = "remaining-availability";
    p.name = "현재 부하와 잔여 가용량";
    p.startDate = "2026-11-16";
    p.calculationProfile = effortProfile;
    p.wbsNodes = {{"G", "선택 작업", ""}, {"H", "다른 작업", ""}};
    p.tasks = {{"A", "A", 120, {}}, {"B", "B", 60, {}}, {"C", "C", 60, {}}};
    p.tasks[0].wbsId = "G";
    p.tasks[1].wbsId = "H";
    p.tasks[2].wbsId = "H";
    Resource r;
    r.id = "R";
    r.name = "R";
    r.availableFrom = "2026-11-16 09:00";
    r.availableTo = "2026-11-17 17:59";
    r.availability = {{{"2026-11-16 09:30"}, {"2026-11-16 10:29"}, 50}};
    r.exceptions = {{"2026-11-17", 0}};
    p.resources = {r};
    r.id = "S";
    r.name = "S";
    r.maxUnits.reset();
    r.availableFrom.reset();
    r.availableTo.reset();
    r.availability.clear();
    r.exceptions.clear();
    p.resources.push_back(r);
    r.id = "I";
    r.name = "I";
    r.maxUnits = 200;
    p.resources.push_back(r);
    p.assignments = {{"A", "R", 75}, {"B", "R", 50}, {"C", "S", 25}};
    p.resourceFields.push_back({"at", "at", CustomFieldType::Date, "", {}});
    for (auto &resource : p.resources)
        resource.attributes = {{"field:at", "2026-11-16T09:30:00"}};
    add(p, FieldScope::Resource, "load", "PPMResourceLoad([field:at])");
    add(p, FieldScope::Resource, "remaining", "PPMRemainingAvailability([field:at])");
    p.resourceFields.push_back({"native", "native", CustomFieldType::Number, "", {}, "[field:remaining]*100"});
    add(p, FieldScope::Resource, "chain", "[field:native]+1");
    add(p, FieldScope::Assignment, "remaining", "PPMRemainingAvailability(#2026-11-16T09:30:00#)");
    add(p, FieldScope::Task, "empty", "PPMResourceLoad(#2026-11-16T09:30:00#)");
    validate(p);
    return p;
}
XmlFormulaResult field(const Project &p, const std::string &name, std::size_t resource = 0) {
    const auto it =
        std::find_if(p.resourceFields.begin(), p.resourceFields.end(), [&](const auto &f) { return f.name == name; });
    check(it != p.resourceFields.end(), "formula exists");
    return xmlFormulaValue(p, FieldScope::Resource, "field:" + it->id, nullptr, &p.resources[resource]);
}
void contract() {
    auto p = fixture();
    const auto plan = calculatePlan(p);
    check(plan.status == CalculationStatus::Valid, "valid source plan");
    ResourceAvailabilityQuery query(p, plan);
    for (const auto &[at, capacity, demand, remaining] :
         std::vector<std::tuple<std::string, int, int, int>>{{"2026-11-16 09:00", 100, 125, -25},
                                                             {"2026-11-16 09:30", 50, 125, -75},
                                                             {"2026-11-16 10:00", 50, 75, -25},
                                                             {"2026-11-16 10:30", 100, 75, 25},
                                                             {"2026-11-16 11:00", 100, 0, 100},
                                                             {"2026-11-16 12:00", 0, 0, 0},
                                                             {"2026-11-16 18:00", 0, 0, 0},
                                                             {"2026-11-17 09:00", 0, 0, 0},
                                                             {"2026-11-18 09:00", 0, 0, 0}}) {
        const auto v = query.at("R", at);
        check(v.capacity == capacity && v.demand == demand && v.remaining == remaining,
              "point capacity minus actual simultaneous demand");
        check(v.freeUnits == std::max(0, remaining) && v.shortage == std::max(0, -remaining),
              "signed deficit and nonnegative allocation/shortage");
    }
    auto unknown = query.at("S", "2026-11-16 09:30");
    check(!unknown.capacity && unknown.demand == 25 && !unknown.remaining, "known demand survives unknown capacity");
    check(query.at("I", "2026-11-16 09:30").freeUnits == 200, "unassigned resource has zero demand");
    check(query.at("S", "2026-11-16 12:00").remaining == 0, "nonworking time has known zero effective capacity");
    {
        auto calendarInput = p;
        calendarInput.calendars.push_back({"afternoon", "Afternoon", {1, 2, 3, 4, 5}, {{780, 1080}}, {}});
        calendarInput.resources[2].calendarId = "afternoon";
        ResourceAvailabilityQuery custom(calendarInput, calculatePlan(calendarInput));
        check(custom.at("I", "2026-11-16 09:30").capacity == 0 && custom.at("I", "2026-11-16 13:00").freeUnits == 200,
              "resource calendar controls effective capacity");
        calendarInput.calendars.back().holidays = {"2026-11-20", "2026-11-16"};
        ResourceAvailabilityQuery holiday(calendarInput, calculatePlan(calendarInput));
        check(holiday.at("I", "2026-11-16 13:00").capacity == 0, "resource holidays override nominal capacity");
    }
    check(field(p, "remaining").missing, "missing schedule context does not pretend zero demand");
    {
        EvaluationScope evaluation;
        ScheduleFormulaScope scope(p, &plan, ScheduleFormulaMode::Active, false);
        check(field(p, "load").value == "1.25" && field(p, "remaining").value == "-0.75" &&
                  field(p, "chain").value == "-74",
              "ratio functions and mixed native/XML chain");
        check(
            xmlFormulaValue(p, FieldScope::Assignment, "field:remaining", nullptr, nullptr, &p.assignments[0]).value ==
                "-0.75",
            "assignment includes itself and peer assignments in pool demand");
        check(xmlFormulaValue(p, FieldScope::Task, "field:empty", &p.tasks[0]).missing,
              "task query does not choose an arbitrary resource");
        // S has unknown capacity, so level a reduced, fully known copy instead.
        auto known = p;
        known.resources[1].maxUnits = 100;
        auto knownLevel = levelResources(known, "task-order", 5);
        check(knownLevel.leveled.status == CalculationStatus::Valid, "leveled schedule");
        {
            ScheduleFormulaScope other(known, &knownLevel.leveled, ScheduleFormulaMode::Active, false);
            check(field(known, "remaining").value == "0", "leveled occupancy is used at the queried instant");
        }
        check(field(p, "remaining").value == "-0.75", "scope restoration keeps cache isolated");
    }
    rejects(
        [&] {
            auto stale = plan;
            ++stale.inputRevision;
            ResourceAvailabilityQuery invalid(p, stale);
        },
        "stale result rejected");
    rejects(
        [&] {
            auto missing = plan;
            missing.tasks.pop_back();
            ResourceAvailabilityQuery invalid(p, missing);
        },
        "partial result rejected");
    rejects([&] { query.at("R", "bad"); }, "invalid timestamp rejected");
    check(nativeSchemaFor(p) == 80 && json::decodeProject(json::encodeProject(p), 80) == p, "schema80 exact roundtrip");
    rejects([&] { json::decodeProject(json::encodeProject(p), 79); },
            "old schema rejects live resource load functions");
    auto disabled = p;
    for (auto scope : {FieldScope::Task, FieldScope::Resource, FieldScope::Assignment})
        for (auto &f : customFields(disabled, scope))
            if (f.xmlFormula)
                f.xmlFormula->enabled = false;
    for (auto &r : disabled.resources) {
        r.availableFrom.reset();
        r.availableTo.reset();
        r.availability.clear();
    }
    check(nativeSchemaFor(disabled) == 44, "disabled raw functions retain old minimum writer");
    for (const auto &formula : {"PPMResourceLoad()", "PPMRemainingAvailability(1,2)"}) {
        auto bad = p;
        add(bad, FieldScope::Resource, "bad", formula);
        rejects([&] { validate(bad); }, "invalid arity rejected");
    }
    auto cancelled = std::make_shared<CalculationControl>();
    cancelled->cancel();
    bool stopped = false;
    {
        EvaluationScope evaluation(cancelled);
        try {
            query.at("R", "2026-11-16 09:30");
        } catch (const CalculationCancelled &) {
            stopped = true;
        }
    }
    check(stopped, "query respects cancellation");
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("load.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    auto edited = p.resources[0];
    edited.maxUnits = 200;
    doc.putResource(edited);
    doc.undo();
    check(doc.project()->resources == p.resources, "capacity edit undo");
    doc.redo();
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-16T02:00:00Z"));
    session.reset();
    check(recovery.scan().candidates.front().valid, "schema80 recovery");
    doc.open(path);
    doc.setStatusAt("2026-11-16 10:00");
    for (const auto &id : {"A", "B"}) {
        ProgressEntry e;
        e.taskId = id;
        e.actualStart = "2026-11-16 09:00";
        e.actualDurationMinutes = 60;
        e.remainingMinutes = std::string(id) == "A" ? 60 : 0;
        if (std::string(id) == "B")
            e.actualFinish = "2026-11-16 10:00";
        e.reporter = "PM";
        e.reason = "보고";
        e.recordedAt = "2026-11-16T01:00:00Z";
        doc.recordProgress(e);
    }
    const auto forecast = calculate(*doc.project());
    check(forecast.status == CalculationStatus::Valid, "valid forecast");
    ResourceAvailabilityQuery remaining(*doc.project(), forecast);
    check(!remaining.at("R", "2026-11-16 09:30").demand && remaining.at("R", "2026-11-16 10:00").remaining == -25 &&
              remaining.at("R", "2026-11-16 11:00").demand == 0,
          "forecast excludes completed tasks and does not invent historical demand");
    auto xml = exportProjectXml(p);
    auto imported = importProjectXml(xml.xml, "load.xml").project;
    {
        ScheduleFormulaScope scope(imported, nullptr, ScheduleFormulaMode::Plan);
        check(field(imported, "load").value == "1.25" && field(imported, "remaining").value == "-0.75",
              "XML formulas recompute with the imported plan");
    }
    const auto dir = QDir::currentPath() + "/remaining-availability-qa";
    QDir().mkpath(dir);
    save(dir + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(dir + "/project.xml", xml.xml);
    save(dir + "/mapping.json", QJsonDocument(xml.report).toJson());
    auto xer = exportPrimaveraXer(p);
    save(dir + "/project.xer", xer.xer);
    QByteArray vectors = "at\tcapacity\tdemand\tremaining\tfree\tshortage\n";
    for (int minute = 0; minute < 1440; ++minute) {
        char time[24];
        std::snprintf(time, sizeof(time), "2026-11-16 %02d:%02d", minute / 60, minute % 60);
        const auto v = query.at("R", time);
        const auto value = [](auto n) { return n ? QByteArray::number(*n) : QByteArray("?"); };
        vectors += QByteArray(time) + '\t' + value(v.capacity) + '\t' + value(v.demand) + '\t' + value(v.remaining) +
                   '\t' + value(v.freeUnits) + '\t' + value(v.shortage) + '\n';
    }
    save(dir + "/points.tsv", vectors);
    ReportOptions options;
    options.reportId = "remaining";
    options.generatedAt = "2026-11-16T02:00:00Z";
    auto report = buildReport(p, options);
    QFile::remove(dir + "/fields.xlsx");
    QFile::remove(dir + "/fields.pdf");
    exportXlsx(report, dir + "/fields.xlsx");
    check(exportPdf(report, dir + "/fields.pdf") > 0, "reports");
    options.wbsId = "G";
    auto scoped = buildReport(p, options);
    auto table = std::find_if(scoped.tables.begin(), scoped.tables.end(),
                              [](const auto &t) { return t.id == "resourceRemainingAvailability"; });
    check(table != scoped.tables.end() && std::get<std::int64_t>(table->rows.front()[4].value) == 125,
          "WBS display filter does not subtract peer demand from other WBS");
    std::cout << "PASS remaining availability "
                 "capacity/demand/deficit/calendar/forecast/context/cache/XML/reports/schema80/undo/recovery\n";
}
void ui() {
    auto p = fixture();
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("ui.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    MainWindow window(doc);
    window.show();
    int stage = 0;
    QTimer timer;
    timer.setInterval(25);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "remainingAvailabilityDialog" ||
            dialog->property("availabilityBusy").toBool())
            return;
        auto *table = dialog->findChild<QTableWidget *>("remainingAvailabilityTable");
        auto *date = dialog->findChild<QLineEdit *>("availabilityQueryAt");
        auto *run = dialog->findChild<QPushButton *>("calculateRemainingAvailability");
        if (stage == 0 && table->rowCount() == 3) {
            check(table->item(0, 4)->text() == "-25%", "initial current demand displayed");
            stage = 1;
            date->setText("2026-11-16 10:30");
            run->click();
        } else if (stage == 1 && table->rowCount() == 3) {
            check(table->item(0, 4)->text() == "25%", "query time changes demand");
            const auto dir = QDir::currentPath() + "/remaining-availability-qa";
            QDir().mkpath(dir);
            check(dialog->grab().save(dir + "/review.png"), "screenshot");
            stage = 2;
            auto resource = doc.project()->resources[0];
            resource.maxUnits = 200;
            doc.putResource(resource);
            check(table->rowCount() == 0, "input edit invalidates displayed snapshot");
            run->click();
        } else if (stage == 2 && table->rowCount() == 3) {
            check(table->item(0, 4)->text() == "125%", "query recomputes after edit");
            date->setText("bad");
            run->click();
            check(table->rowCount() == 0 &&
                      !dialog->findChild<QLabel *>("remainingAvailabilityStatus")->text().isEmpty(),
                  "bad date does not display stale values");
            stage = 3;
            dialog->reject();
        }
    });
    timer.start();
    window.findChild<QAction *>("remainingAvailability")->trigger();
    timer.stop();
    check(stage == 3, "remaining query dialog completed");
    std::cout << "PASS remaining availability UI query/edit/invalidation\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
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
