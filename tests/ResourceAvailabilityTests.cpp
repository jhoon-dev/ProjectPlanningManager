#include "domain/ResourceAvailability.h"
#include "domain/Classification.h"
#include "domain/XmlFormula.h"
#include "calculation/CapacityTimeline.h"
#include "calculation/Resources.h"
#include "calculation/Reports.h"
#include "persistence/ProjectXml.h"
#include "persistence/ProjectJson.h"
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
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QTimer>
#include <QTest>
#include <iostream>
namespace {
using namespace jobs;
void check(bool value, const char *why) {
    if (!value)
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
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size(), "write artifact");
}
Project fixture(const std::string &profile = effortProfile) {
    Project p;
    p.documentId = "resource-availability";
    p.name = "부분일·무기한 가용기간";
    p.startDate = "2026-11-16";
    p.calculationProfile = profile;
    p.tasks = {{"A", "A", 60, {}}, {"B", "B", 60, {}}};
    Resource r;
    r.id = "R";
    r.name = "R";
    r.availableFrom = "2026-11-16 09:00";
    r.availableTo = "2026-11-17 17:59";
    r.availability = {
        {{}, "2026-11-16 09:59", 0}, {"2026-11-16 10:00", "2026-11-16 10:59", 50}, {"2026-11-16 11:00", {}, 100}};
    r.exceptions = {{"2026-11-17", 0}};
    p.resources = {r};
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    p.resourceFields.push_back({"q", "시점 가용률", CustomFieldType::Number, "", {}});
    p.resourceFields.back().xmlFormula =
        bindXmlFormula(p, FieldScope::Resource, "PPMUnitAvailability(#2026-11-16T10:30:00#)");
    p.resourceFields.back().xmlFormula->enabled = true;
    validate(p);
    return p;
}
void contract() {
    auto p = fixture();
    auto &r = p.resources[0];
    for (const auto &[stamp, units] : std::vector<std::pair<std::string, int>>{{"2026-11-16 08:59", 0},
                                                                               {"2026-11-16 09:59", 0},
                                                                               {"2026-11-16 10:00", 50},
                                                                               {"2026-11-16 10:59", 50},
                                                                               {"2026-11-16 11:00", 100},
                                                                               {"2026-11-17 12:00", 0},
                                                                               {"2026-11-18 09:00", 0}})
        check(resourceUnitsAt(r, stamp) == units, "inclusive minute/open periods/global bounds/daily override");
    check(xmlFormulaValue(p, FieldScope::Resource, "field:q", nullptr, &r).value == "0.5", "date-time capacity query");
    {
        EvaluationScope scope;
        r.availability[1].units = 25;
        check(xmlFormulaValue(p, FieldScope::Resource, "field:q", nullptr, &r).value == "0.25",
              "period edit invalidates formula cache");
        r.availability[1].units.reset();
        check(xmlFormulaValue(p, FieldScope::Resource, "field:q", nullptr, &r).missing, "unknown period remains Null");
    }
    p = fixture();
    check(nativeSchemaFor(p) == 79 && json::decodeProject(json::encodeProject(p), 79) == p, "schema79 exact roundtrip");
    rejects([&] { json::decodeProject(json::encodeProject(p), 78); }, "schema78 rejects capacity periods");
    auto legacy = fixture();
    legacy.resources[0].availability.clear();
    legacy.resources[0].availableFrom.reset();
    legacy.resources[0].availableTo.reset();
    check(nativeSchemaFor(legacy) == 78, "old minimum writer retained");
    for (int kind = 0; kind < 5; ++kind) {
        auto frozen = legacy;
        if (kind == 0) {
            frozen.baselines.push_back({});
            frozen.baselines.back().input = p;
        }
        if (kind == 1) {
            frozen.scenarios.push_back({});
            frozen.scenarios.back().input = p;
        }
        if (kind == 2) {
            frozen.xerScheduleAdoptions.push_back({});
            frozen.xerScheduleAdoptions.back().after = p;
        }
        if (kind == 3) {
            frozen.currencyConversions.push_back({});
            frozen.currencyConversions.back().before = std::make_shared<Project>(p);
        }
        if (kind == 4) {
            frozen.ledgerIdMigrations.push_back({});
            frozen.ledgerIdMigrations.back().after = std::make_shared<Project>(p);
        }
        check(nativeSchemaFor(frozen) == 79, "new capacity semantics in frozen/history copies");
    }
    for (int kind = 0; kind < 3; ++kind) {
        auto bad = fixture();
        if (kind == 0)
            bad.resources[0].availability[1].from = "2026-11-16 09:59";
        if (kind == 1)
            bad.resources[0].availableTo = "2026-11-15 12:00";
        if (kind == 2)
            bad.resources[0].availability[1].units = -1;
        rejects([&] { validate(bad); }, "overlap/reversed bounds/invalid units rejected");
    }
    for (const auto &profile :
         {std::string(effortProfile), std::string(pdmProfile), std::string("native-fs0-workminutes-v1")}) {
        auto input = fixture(profile);
        auto result = levelResources(input, "task-order", 5);
        check(result.leveled.status == CalculationStatus::Valid && result.leveled.tasks.size() == 2,
              "capacity-aware leveling succeeds");
        check(result.leveled.tasks[0].start == "2026-11-16 11:00" &&
                  result.leveled.tasks[1].start == "2026-11-16 13:00",
              "all engines observe subday capacity transitions");
        check(result.after.status == CalculationStatus::Valid, "leveled load fits capacity");
        auto crossing = input;
        crossing.tasks.resize(1);
        crossing.assignments.resize(1);
        crossing.tasks[0].durationMinutes = 120;
        crossing.resources[0].availableFrom.reset();
        crossing.resources[0].availableTo.reset();
        crossing.resources[0].exceptions.clear();
        crossing.resources[0].availability = {{{"2026-11-16 10:00"}, {"2026-11-16 10:29"}, 0}};
        auto moved = levelResources(crossing, "task-order", 5);
        check(moved.leveled.status == CalculationStatus::Valid && moved.leveled.tasks[0].start == "2026-11-16 10:30",
              "capacity loss inside execution prevents an invalid early placement");
        auto spanning = input;
        spanning.tasks.resize(1);
        spanning.assignments.resize(1);
        spanning.tasks[0].durationMinutes = 240;
        auto load = resourceLoad(spanning, calculate(spanning));
        check(load.days.size() == 1 && load.days[0].capacityVaries && load.days[0].overloadMinutes == 120 &&
                  load.days[0].excessUnitMinutes == 9000,
              "load splits at capacity changes instead of using a daily approximation");
    }
    auto xml = exportProjectXml(p);
    {
        auto finalDate = fixture();
        finalDate.startDate = "9999-12-31";
        auto &resource = finalDate.resources[0];
        resource.availableFrom.reset();
        resource.availableTo = "9999-12-31 23:58";
        resource.availability.clear();
        resource.exceptions.clear();
        WorkCalendar clock(finalDate);
        CapacityTimeline timeline(resource, clock);
        check(timeline.at(clock.maxOffset() - 2) == 100 && timeline.at(clock.maxOffset() - 1) == 0,
              "capacity timeline includes the final supported minute without looking into year10000");
    }
    auto imported = importProjectXml(xml.xml, "availability.xml").project;
    for (const auto &at : {"2026-11-15 12:00", "2026-11-16 09:59", "2026-11-16 10:00", "2026-11-16 10:59",
                           "2026-11-16 11:00", "2026-11-17 09:00", "2026-11-18 09:00"})
        check(resourceUnitsAt(imported.resources[0], at) == resourceUnitsAt(p.resources[0], at),
              "canonical XML preserves effective capacity");
    check(hasXmlAvailabilityFunctions(imported, true), "partial/open XML retains live formulas");
    auto xmlAgain = exportProjectXml(imported);
    auto again = importProjectXml(xmlAgain.xml, "again.xml").project;
    check(resourceUnitsAt(again.resources[0], "2026-11-16 10:30") == 50, "second XML cycle");
    auto malformed = xml.xml;
    malformed.replace("T10:00:00</AvailableFrom>", "T10:00:30</AvailableFrom>");
    check(!hasXmlAvailabilityFunctions(importProjectXml(malformed, "seconds.xml").project, true),
          "subminute capacity does not round silently");
    auto veryLong = fixture();
    veryLong.resources[0].availableFrom.reset();
    veryLong.resources[0].availableTo.reset();
    veryLong.resources[0].availability = {{{"1900-01-01 00:00"}, {"9999-12-31 23:59"}, 200}};
    veryLong.resources[0].exceptions.clear();
    auto longInput = importProjectXml(exportProjectXml(veryLong).xml, "long.xml").project;
    check(longInput.resources[0].exceptions.empty() && longInput.resources[0].availability.size() < 5,
          "long periods stay compact");
    QTemporaryDir tmp;
    JsonProjectStore store;
    const auto path = tmp.filePath("availability.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    auto edited = p.resources[0];
    edited.availableFrom = "2026-11-16 10:30";
    doc.putResource(edited);
    doc.undo();
    check(doc.project()->resources == p.resources, "capacity edit undo");
    doc.redo();
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-16T00:00:00Z"));
    session.reset();
    check(recovery.scan().candidates.size() == 1 && recovery.scan().candidates[0].valid, "schema79 recovery");
    const auto dir = QDir::currentPath() + "/resource-availability-qa";
    QDir().mkpath(dir);
    save(dir + "/native.json", QJsonDocument(json::encodeProject(p)).toJson());
    save(dir + "/project.xml", xml.xml);
    save(dir + "/mapping.json", QJsonDocument(xml.report).toJson());
    save(dir + "/reexported.xml", xmlAgain.xml);
    ReportOptions options;
    options.reportId = "availability";
    options.generatedAt = "2026-11-16T00:00:00Z";
    const auto report = buildReport(p, options);
    QFile::remove(dir + "/fields.xlsx");
    QFile::remove(dir + "/fields.pdf");
    exportXlsx(report, dir + "/fields.xlsx");
    check(exportPdf(report, dir + "/fields.pdf") > 0, "reports");
    std::cout << "PASS resource availability minute boundaries/open ranges/precedence/three leveling "
                 "engines/load/XML/schema79/cache/undo/recovery/reports\n";
}
void ui() {
    QTemporaryDir tmp;
    JsonProjectStore store;
    auto p = fixture();
    const auto path = tmp.filePath("ui.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    MainWindow window(doc);
    window.show();
    QTimer::singleShot(60, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        check(dialog && dialog->objectName() == "resourceDialog", "resource dialog");
        auto *table = dialog->findChild<QTableWidget *>("resourceList");
        table->selectRow(0);
        auto *from = dialog->findChild<QLineEdit *>("resourceAvailableFrom");
        auto *periods = dialog->findChild<QPlainTextEdit *>("resourceAvailabilityPeriods");
        check(from->text() == "2026-11-16 09:00" && periods->toPlainText().contains("10:59=50"),
              "editor fills open capacity periods");
        from->setText("2026-11-16 09:30");
        dialog->findChild<QPushButton *>("saveResource")->click();
        check(doc.project()->resources[0].availableFrom == "2026-11-16 09:30", "editor saves bounds");
        const auto dir = QDir::currentPath() + "/resource-availability-qa";
        QDir().mkpath(dir);
        if (auto *scroll = dialog->findChild<QScrollArea *>())
            scroll->ensureWidgetVisible(periods);
        check(dialog->grab().save(dir + "/review.png"), "editor screenshot");
        dialog->reject();
    });
    window.findChild<QAction *>("manageResources")->trigger();
    std::cout << "PASS resource availability editor fill/save\n";
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
        std::cerr << e.what() << '\n';
        return 1;
    }
}
