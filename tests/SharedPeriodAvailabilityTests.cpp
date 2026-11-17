#include "calculation/SharedAvailabilityQuery.h"
#include "calculation/SharedResources.h"
#include "calculation/IntegratedWorkspace.h"
#include "domain/ResourceAvailability.h"
#include "domain/NativeSchema.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonWorkspaceStore.h"
#include "persistence/WorkspaceFile.h"
#include "persistence/ProjectJson.h"
#include "reporting/SharedAvailabilityExport.h"
#include "reporting/IntegratedExport.h"
#include "reporting/ReportExport.h"
#include "ui/DesktopWorkspace.h"
#include "ui/WorkspaceDialog.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
namespace {
using namespace jobs;
void check(bool v, const char *why) {
    if (!v)
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
    check(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size(), "save artifact");
}
QByteArray read(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "read artifact");
    return f.readAll();
}
QByteArray sign(QJsonObject root, QJsonObject payload) {
    root["payload"] = payload;
    root["sha256"] = QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(payload).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex());
    return QJsonDocument(root).toJson();
}
Project project(std::string id) {
    Project p;
    p.documentId = id;
    p.name = "프로젝트 " + id;
    p.startDate = "2026-11-16";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "연속 작업", id == "P" ? 1200 : 960, {}}};
    if (id == "Q") {
        p.tasks[0].constraintType = ConstraintType::SNET;
        p.tasks[0].constraintAt = "2026-11-16 09:30";
    }
    p.resources = {{"R", "로컬 자원", "labor", 100, {}}};
    p.assignments = {{"T", "R", id == "P" ? 75 : 50}};
    validate(p);
    return p;
}
Workspace config() {
    Workspace w;
    w.id = "shared-periods";
    w.projects = {{"P", "", "프로젝트 P"}, {"Q", "", "프로젝트 Q"}};
    w.resources = {{"pool", "부분·열린 공유 풀", "labor", 100}};
    w.bindings = {{"P", "R", "pool"}, {"Q", "R", "pool"}};
    auto &r = w.resources[0];
    r.availableFrom = "2026-11-16 09:15";
    r.availableTo = "2026-11-18 10:29";
    r.availability = {
        {{}, "2026-11-16 09:29", 50}, {"2026-11-16 09:30", "2026-11-16 10:14", {}}, {"2026-11-16 10:15", {}, 200}};
    r.exceptions = {{"2026-11-17", 75}};
    return w;
}
std::vector<std::shared_ptr<const Project>> snapshots(const Project &p, const Project &q) {
    return {std::make_shared<const Project>(p), std::make_shared<const Project>(q)};
}
void contracts() {
    auto p = project("P"), q = project("Q");
    auto w = config();
    auto inputs = snapshots(p, q);
    const auto frozen = w;
    const auto pp = p, qq = q;
    SharedAvailabilityQuery query(w, inputs, false);
    for (const auto &[at, capacity] : std::vector<std::pair<std::string, std::optional<int>>>{{"2026-11-16 09:14", 0},
                                                                                              {"2026-11-16 09:15", 50},
                                                                                              {"2026-11-16 09:29", 50},
                                                                                              {"2026-11-16 09:30", {}},
                                                                                              {"2026-11-16 10:14", {}},
                                                                                              {"2026-11-16 10:15", 200},
                                                                                              {"2026-11-16 12:00", 0},
                                                                                              {"2026-11-17 09:30", 75},
                                                                                              {"2026-11-18 10:29", 200},
                                                                                              {"2026-11-18 10:30", 0}})
        check(query.at("pool", at).value.capacity == capacity, "inclusive/open/bounds/day exception/calendar capacity");
    check(!query.at("pool", "2026-11-16 09:45").value.remaining &&
              query.at("pool", "2026-11-16 09:45").value.demand == 125,
          "unknown supply keeps known demand");
    auto daily = calculateSharedResources(w, inputs, false);
    check(daily.days.size() == 3 && daily.status == CalculationStatus::Incomplete, "partial capacity daily result");
    check(daily.days[0].variableCapacity && !daily.days[0].capacity && daily.days[0].unknownCapacityMinutes == 45 &&
              daily.days[0].overloadMinutes == 30 && daily.days[0].excessUnitMinutes == 1500,
          "split daily integration at inclusive capacity changes");
    check(!daily.days[1].variableCapacity && daily.days[1].capacity == 75 && daily.days[1].overloadMinutes == 480 &&
              daily.days[1].excessUnitMinutes == 24000,
          "daily override wins over open period");
    check(daily.days[2].variableCapacity && daily.days[2].overloadMinutes == 150 &&
              daily.days[2].excessUnitMinutes == 11250,
          "global end clips open period on the next minute");
    auto gap = w;
    auto &g = gap.resources[0];
    g.availableFrom.reset();
    g.availableTo.reset();
    g.exceptions.clear();
    g.availability = {{"2026-11-16 10:00", "2026-11-16 10:30", 50}};
    auto adapter = sharedResourceCapacity(g);
    check(resourceUnitsAt(adapter, "2026-11-16 09:59") == 100 && resourceUnitsAt(adapter, "2026-11-16 10:30") == 50 &&
              resourceUnitsAt(adapter, "2026-11-16 10:31") == 100,
          "period gap returns base capacity");
    g.availability = {{{}, {}, maxSharedResourceUnits}};
    validateWorkspace(gap);
    check(SharedAvailabilityQuery(gap, inputs, false).at("pool", "2026-11-16 09:45").value.capacity ==
              maxSharedResourceUnits,
          "fully open shared ceiling");
    g.availability[0].units.reset();
    g.exceptions = {{"2026-11-16", 150}};
    check(SharedAvailabilityQuery(gap, inputs, false).at("pool", "2026-11-16 09:45").value.capacity == 150,
          "day exception overrides unknown period");
    g.exceptions[0].units.reset();
    check(!SharedAvailabilityQuery(gap, inputs, false).at("pool", "2026-11-16 09:45").value.capacity,
          "unknown day remains unknown");
    auto holiday = w;
    holiday.resources[0].calendar.holidays = {"2026-11-16"};
    check(SharedAvailabilityQuery(holiday, inputs, false).at("pool", "2026-11-16 09:45").value.capacity == 0,
          "calendar closure overrides unknown configuration");
    for (int variant = 0; variant < 8; ++variant) {
        auto bad = w;
        auto &r = bad.resources[0];
        if (variant == 0)
            r.availableFrom = "2026-11-18 10:30";
        if (variant == 1)
            r.availableTo = "bad";
        if (variant == 2)
            r.availability[1].from = "2026-11-16 09:29";
        if (variant == 3)
            r.availability[0].to.reset();
        if (variant == 4)
            r.availability[1].to = "2026-11-16 09:00";
        if (variant == 5)
            r.availability[1].units = -1;
        if (variant == 6)
            r.availability[1].units = maxSharedResourceUnits + 1;
        if (variant == 7)
            r.availability.resize(10001);
        rejects([&] { validateWorkspace(bad); }, "invalid shared availability accepted");
    }
    auto minute = w;
    minute.resources[0].availableFrom = minute.resources[0].availableTo = "2026-11-16 10:15";
    check(SharedAvailabilityQuery(minute, inputs, false).at("pool", "2026-11-16 10:15").value.capacity == 200 &&
              SharedAvailabilityQuery(minute, inputs, false).at("pool", "2026-11-16 10:16").value.capacity == 0,
          "single inclusive minute");
    // Exercise the existing pooled scheduler's capacity timeline using new shared fields.
    auto lp = p, lq = q;
    lp.tasks[0].durationMinutes = lq.tasks[0].durationMinutes = 60;
    lq.tasks[0].constraintAt = "2026-11-16 09:00";
    auto level = w;
    auto &lr = level.resources[0];
    lr.maxUnits = 0;
    lr.exceptions.clear();
    lr.availableFrom = "2026-11-16 09:30";
    lr.availableTo = "2026-11-16 10:59";
    lr.availability = {{"2026-11-16 10:00", {}, 200}};
    level.integrated.forecast = false;
    level.integrated.levelShared = true;
    level.integrated.horizonWorkDays = 2;
    auto leveled = calculateIntegratedWorkspace(level, snapshots(lp, lq));
    if (leveled.status != CalculationStatus::Valid)
        for (const auto &d : leveled.diagnostics)
            std::cerr << d.code << ' ';
    check(leveled.status == CalculationStatus::Valid && leveled.tasks.size() == 2 &&
              leveled.shared.status == CalculationStatus::Valid,
          "shared leveling applies bounds/periods");
    for (const auto &t : leveled.tasks)
        check(t.schedule.start == "2026-11-16 10:00" && t.schedule.finish == "2026-11-16 11:00",
              "pool availability shifted both projects");
    auto large = level;
    large.resources[0].availability[0].units = 100001;
    check(calculateIntegratedWorkspace(large, snapshots(lp, lq)).status != CalculationStatus::Valid,
          "local leveling ceiling includes period capacity");
    auto logical = calculateIntegratedWorkspace(w, inputs);
    check(logical.status == CalculationStatus::Valid && logical.shared.days[0].variableCapacity,
          "logical integrated report retains variable capacity");
    QTemporaryDir temp;
    JsonProjectStore files;
    JsonWorkspaceStore store(files);
    auto stored = w;
    stored.projects[0].path = temp.filePath("p.ppm").toStdString();
    stored.projects[1].path = temp.filePath("q.ppm").toStdString();
    const auto path = temp.filePath("test.ppmworkspace");
    auto fingerprint = store.save(path.toStdString(), stored, {});
    check(store.load(path.toStdString()).workspace == stored, "schema4 exact roundtrip");
    auto root = QJsonDocument::fromJson(read(path)).object();
    check(root["schemaVersion"] == 4, "conditional workspace4 writer");
    for (int version = 1; version <= 3; ++version) {
        auto legacy = stored;
        legacy.resources[0].availableFrom.reset();
        legacy.resources[0].availableTo.reset();
        legacy.resources[0].availability.clear();
        if (version >= 2)
            legacy.integrated.forecast = false;
        if (version >= 3) {
            legacy.governance.members = {{"owner", "관리자", ""}};
            legacy.governance.grants = {{"owner", "", "owner"}};
        }
        fingerprint = store.save(path.toStdString(), legacy, fingerprint);
        check(QJsonDocument::fromJson(read(path)).object()["schemaVersion"] == version &&
                  store.load(path.toStdString()).workspace == legacy,
              "legacy minimum writer retained");
        legacy.resources[0].availability = {{{}, {}, 50}};
        fingerprint = store.save(path.toStdString(), legacy, fingerprint);
        check(QJsonDocument::fromJson(read(path)).object()["schemaVersion"] == 4 &&
                  store.load(path.toStdString()).workspace == legacy,
              "schema4 preserves integrated/governance settings");
    }
    const auto payload = root["payload"].toObject();
    for (int version = 1; version <= 3; ++version) {
        auto old = root;
        old["schemaVersion"] = version;
        rejects([&] { decodeWorkspaceFile(sign(old, payload), temp.path()); },
                "old schema cannot hide new capacity meaning");
    }
    auto future = root;
    future["schemaVersion"] = 5;
    rejects([&] { decodeWorkspaceFile(QJsonDocument(future).toJson(), temp.path()); },
            "future workspace schema rejected");
    for (int variant = 0; variant < 8; ++variant) {
        auto body = payload;
        auto rows = body["resources"].toArray();
        auto r = rows[0].toObject();
        auto periods = r["availability"].toArray();
        auto v = periods[1].toObject();
        if (variant == 0)
            r.remove("availableFrom");
        if (variant == 1)
            r["availableTo"] = 7;
        if (variant == 2)
            v.remove("units");
        if (variant == 3)
            v["units"] = 1.5;
        if (variant == 4)
            v["unknown"] = true;
        if (variant == 5)
            v["from"] = "2026-11-16 09:29";
        if (variant == 6)
            v["to"] = "2026-11-16 10:14+09:00";
        if (variant == 7)
            r["availability"] = false;
        periods[1] = v;
        if (variant != 7)
            r["availability"] = periods;
        rows[0] = r;
        body["resources"] = rows;
        rejects([&] { decodeWorkspaceFile(sign(root, body), temp.path()); },
                "malformed signed shared availability rejected");
    }
    const auto before = read(path);
    auto bad = stored;
    bad.resources[0].availability[1].from = "2026-11-16 09:29";
    rejects([&] { store.save(path.toStdString(), bad, fingerprint); }, "invalid save rejected");
    check(read(path) == before, "failed save changed old bytes");
    check(p == pp && q == qq && w == frozen && nativeSchemaFor(p) == nativeSchemaFor(pp),
          "shared availability does not mutate local input/storage");
    auto dir = QDir::currentPath() + "/shared-periods-qa";
    QDir().mkpath(dir);
    for (auto *input : {&p, &q}) {
        auto file = dir + "/" + QString::fromStdString(input->documentId).toLower() + ".ppm";
        QFile::remove(file);
        files.save(file.toStdString(), *input, {});
    }
    stored.projects[0].path = (dir + "/p.ppm").toStdString();
    stored.projects[1].path = (dir + "/q.ppm").toStdString();
    auto target = dir + "/workspace.ppmworkspace";
    QFile::remove(target);
    store.save(target.toStdString(), stored, {});
    QByteArray vectors = "at\tcapacity\tdemand\tremaining\tfree\tshortage\n";
    for (int day = 16; day <= 18; ++day)
        for (int minute = 0; minute < 1440; ++minute) {
            char stamp[24];
            std::snprintf(stamp, sizeof(stamp), "2026-11-%02d %02d:%02d", day, minute / 60, minute % 60);
            const auto point = query.at("pool", stamp).value;
            const auto n = [](auto v) { return v ? QByteArray::number(*v) : QByteArray("?"); };
            vectors += QByteArray(stamp) + '\t' + n(point.capacity) + '\t' + n(point.demand) + '\t' +
                       n(point.remaining) + '\t' + n(point.freeUnits) + '\t' + n(point.shortage) + '\n';
        }
    save(dir + "/points.tsv", vectors);
    QJsonArray days;
    for (const auto &d : daily.days)
        days.append(QJsonObject{{"date", QString::fromStdString(d.date)},
                                {"capacity", d.capacity ? QJsonValue(*d.capacity) : QJsonValue(QJsonValue::Null)},
                                {"variableCapacity", d.variableCapacity},
                                {"demandUnitMinutes", double(d.demandUnitMinutes)},
                                {"overloadMinutes", double(d.overloadMinutes)},
                                {"excessUnitMinutes", double(d.excessUnitMinutes)},
                                {"unknownCapacityMinutes", double(d.unknownCapacityMinutes)}});
    save(dir + "/daily.json", QJsonDocument(days).toJson());
    save(dir + "/integrated.json", integratedWorkspaceJson(w, logical, 5));
    for (const auto &[name, at] :
         std::vector<std::pair<QString, std::string>>{{"unknown", "2026-11-16 09:45"}, {"known", "2026-11-16 10:30"}}) {
        auto result = query.allAt(at);
        save(dir + "/" + name + ".json", sharedAvailabilityJson(w, result, 5));
        auto report = sharedAvailabilityReport(w, result, 5, "2026-11-16T03:00:00Z");
        QFile::remove(dir + "/" + name + ".xlsx");
        QFile::remove(dir + "/" + name + ".pdf");
        exportXlsx(report, dir + "/" + name + ".xlsx");
        exportPdf(report, dir + "/" + name + ".pdf");
    }
    auto report = integratedWorkspaceReport(w, logical, 5, "2026-11-16T03:00:00Z");
    QFile::remove(dir + "/integrated.xlsx");
    QFile::remove(dir + "/integrated.pdf");
    exportXlsx(report, dir + "/integrated.xlsx");
    exportPdf(report, dir + "/integrated.pdf");
    std::cout << "PASS shared open/inclusive capacity periods/bounds/day precedence/calendar/daily integration/pooled "
                 "leveling/schema1..4/strict parsing/atomic save/report exports\n";
}
void ui() {
    QTemporaryDir temp;
    auto files = std::make_shared<JsonProjectStore>();
    auto p = project("P"), q = project("Q");
    auto pp = temp.filePath("p.ppm"), qp = temp.filePath("q.ppm");
    files->save(pp.toStdString(), p, {});
    files->save(qp.toStdString(), q, {});
    DesktopWorkspace workspace(files, {});
    workspace.openProject(pp.toStdString());
    workspace.openProject(qp.toStdString());
    auto cfg = config();
    cfg.projects[0].path = pp.toStdString();
    cfg.projects[1].path = qp.toStdString();
    cfg.resources[0].availableFrom.reset();
    cfg.resources[0].availableTo.reset();
    cfg.resources[0].availability.clear();
    workspace.applyConfiguration(cfg);
    WorkspaceDialog parent(workspace);
    parent.show();
    parent.findChild<QTableWidget *>("workspaceResources")->selectRow(0);
    auto generation = workspace.generation();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        check(d && d->objectName() == "sharedResourceDialog", "pool editor opened");
        auto *from = d->findChild<QLineEdit *>("sharedResourceAvailableFrom");
        auto *to = d->findChild<QLineEdit *>("sharedResourceAvailableTo");
        auto *periods = d->findChild<QPlainTextEdit *>("sharedResourceAvailabilityPeriods");
        auto *ok = d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        from->setText("2026-11-16 09:15");
        to->setText("2026-11-18 10:29");
        periods->setPlainText(" ~ 2026-11-16 09:30=50\n2026-11-16 09:30 ~ =200");
        ok->click();
        check(!d->findChild<QLabel *>("sharedResourceError")->text().isEmpty() &&
                  workspace.generation() == generation && workspace.configuration() == cfg,
              "overlap validation leaves configuration unchanged");
        periods->setPlainText(" ~ 2026-11-16 09:29=50\n2026-11-16 09:30 ~ 2026-11-16 10:14=?\n2026-11-16 10:15 ~ =200");
        d->resize(800, 600);
        QTest::qWait(50);
        d->findChild<QScrollArea *>()->ensureWidgetVisible(periods);
        auto dir = QDir::currentPath() + "/shared-periods-qa";
        QDir().mkpath(dir);
        check(d->grab().save(dir + "/editor.png"), "editor screenshot");
        ok->click();
    });
    parent.findChild<QPushButton *>("workspaceEditResource")->click();
    check(workspace.configuration().resources[0] == config().resources[0] && workspace.generation() > generation &&
              workspace.dirty(),
          "pool periods committed");
    const auto saved = workspace.configuration();
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        check(d->findChild<QLineEdit *>("sharedResourceAvailableFrom")->text() == "2026-11-16 09:15" &&
                  d->findChild<QPlainTextEdit *>("sharedResourceAvailabilityPeriods")->toPlainText().contains("=?"),
              "editor reload retains open/unknown values");
        d->findChild<QLineEdit *>("sharedResourceAvailableTo")->clear();
        d->reject();
    });
    parent.findChild<QPushButton *>("workspaceEditResource")->click();
    check(workspace.configuration() == saved, "cancel is atomic");
    QTimer::singleShot(0, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        auto *periods = d->findChild<QPlainTextEdit *>("sharedResourceAvailabilityPeriods");
        d->findChild<QLineEdit *>("sharedResourceName")->setText("추가 풀");
        auto *ok = d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        periods->setPlainText(" ~ =100\n2026-11-16 09:00 ~ =50");
        ok->click();
        check(workspace.configuration() == saved, "invalid new pool did not leak");
        periods->clear();
        ok->click();
    });
    parent.findChild<QPushButton *>("workspaceAddResource")->click();
    check(workspace.configuration().resources.size() == 2, "retry adds one valid pool");
    workspace.applyConfiguration(saved);
    int stage = 0;
    QTimer timer;
    timer.setInterval(25);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d || d->objectName() != "sharedAvailabilityDialog" || d->property("sharedAvailabilityBusy").toBool())
            return;
        auto *table = d->findChild<QTableWidget *>("sharedAvailabilityPoints");
        auto *date = d->findChild<QLineEdit *>("sharedAvailabilityAt");
        auto *run = d->findChild<QPushButton *>("calculateSharedAvailability");
        if (stage == 0 && table->rowCount() == 1) {
            stage = 1;
            date->setText("2026-11-16 09:45");
            run->click();
        } else if (stage == 1 && table->rowCount() == 1) {
            check(table->item(0, 2)->text() == "미정" && table->item(0, 3)->text() == "125%" &&
                      table->item(0, 5)->text() == "미정",
                  "unknown period visible with known load");
            stage = 2;
            date->setText("2026-11-16 10:30");
            run->click();
        } else if (stage == 2 && table->rowCount() == 1) {
            check(table->item(0, 2)->text() == "200%" && table->item(0, 5)->text() == "75%",
                  "period capacity used by query UI");
            stage = 3;
            auto changed = workspace.configuration();
            changed.resources[0].availability[2].units = 300;
            workspace.applyConfiguration(changed);
        } else if (stage == 3 && table->rowCount() == 0) {
            check(!d->findChild<QPushButton *>("exportSharedAvailabilityJSON")->isEnabled(),
                  "period edit invalidates previous export");
            stage = 4;
            run->click();
        } else if (stage == 4 && table->rowCount() == 1) {
            check(table->item(0, 2)->text() == "300%" && table->item(0, 5)->text() == "175%",
                  "fresh period generation used");
            stage = 5;
            d->reject();
        }
    });
    timer.start();
    parent.findChild<QPushButton *>("workspaceRemainingAvailability")->click();
    timer.stop();
    check(stage == 5, "query invalidation UI completed");
    parent.reject();
    std::cout << "PASS shared period editor/overlap repair/cancel/create retry/query null/capacity invalidation\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    auto font = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "shared period timeout\n";
        std::_Exit(2);
    });
    watchdog.start(90000);
    try {
        if (app.arguments().contains("--ui"))
            ui();
        else
            contracts();
    } catch (const std::exception &e) {
        std::cerr << e.what();
        if (auto *error = dynamic_cast<const AppError *>(&e))
            for (const auto &a : error->diagnostic().arguments)
                std::cerr << ' ' << a;
        return 1;
    }
}
