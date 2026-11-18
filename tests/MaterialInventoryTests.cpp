#include "calculation/MaterialInventory.h"
#include "calculation/IntegratedWorkspace.h"
#include "calculation/Resources.h"
#include "calculation/Reports.h"
#include "calculation/Costs.h"
#include "domain/Classification.h"
#include "domain/NativeSchema.h"
#include "domain/ResourcePlan.h"
#include "application/Document.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "reporting/ReportExport.h"
#include "ui/ResourcePlanDialog.h"
#include "ui/MaterialInventoryDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <iostream>
namespace {
using namespace jobs;
void check(bool b, const char *why) {
    if (!b)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f, const char *why) {
    bool bad = false;
    try {
        f();
    } catch (const AppError &) {
        bad = true;
    }
    check(bad, why);
}
void save(const QString &path, const QByteArray &data) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size(), "write artifact");
}
Project fixture() {
    Project p;
    p.documentId = "inventory";
    p.name = "계획 입고·잔여 재고";
    p.startDate = "2026-11-17";
    p.calculationProfile = effortProfile;
    p.tasks = {{"S", "시작 소모", 60, {}}, {"F", "완료 소모", 60, {}}, {"U", "균등 소모", 61, {}}};
    p.resourcePlan.materials = {{"M", "철근", "kg", 100, 2000}, {"N", "미정 재고", "개", 0, {}}};
    p.resourcePlan.materials[0].stockAt = "2026-11-17 09:00";
    p.resourcePlan.uses = {
        {"s", "S", "M", 3000, "start"}, {"f", "F", "M", 3000, "finish"}, {"u", "U", "M", 1001, "uniform"}};
    p.resourcePlan.receipts = {{"r1", "M", "2026-11-17 09:00", 2000},
                               {"r2", "M", "2026-11-17 09:30", 1000},
                               {"r3", "M", "2026-11-17 10:00", 2000},
                               {"r4", "M", "2026-11-17 10:02", 1},
                               {"rn", "N", "2026-11-17 09:00", 1000}};
    validate(p);
    return p;
}
void contract() {
    auto p = fixture();
    auto before = p;
    auto schedule = calculatePlan(p);
    check(schedule.status == CalculationStatus::Valid, "fixture schedule");
    MaterialInventoryQuery query(p, schedule);
    for (const auto &[at, left] : std::vector<std::pair<std::string, std::int64_t>>{{"2026-11-17 09:00", 1000},
                                                                                    {"2026-11-17 09:30", 1508},
                                                                                    {"2026-11-17 10:00", 15},
                                                                                    {"2026-11-17 10:01", -1},
                                                                                    {"2026-11-17 10:02", 0}})
        check(query.at("M", at).remaining == left, "receipt/instant/cumulative half-up/signed remaining");
    check(query.firstShortage("M") == "2026-11-17 10:01", "shortage before recovery not hidden by later receipt");
    auto unknown = query.at("N", "2026-11-17 10:00");
    check(unknown.received == 1000 && !unknown.remaining && !unknown.available,
          "unknown opening stays unknown after receipt");
    auto prior = query.at("M", "2026-11-17 08:59");
    check(!prior.consumed && !prior.remaining && !prior.received, "before opening is unknown");
    auto uniform = p;
    uniform.resourcePlan.uses = {{"u", "U", "M", 1001, "uniform"}};
    uniform.resourcePlan.receipts.clear();
    uniform.resourcePlan.materials[0].stockMilli = 0;
    check(MaterialInventoryQuery(uniform, calculatePlan(uniform)).firstShortage("M") == "2026-11-17 09:01",
          "binary search finds first rounded consumption");
    uniform.tasks[2].durationMinutes = 241;
    MaterialInventoryQuery lunch(uniform, calculatePlan(uniform));
    check(lunch.at("M", "2026-11-17 12:00").consumed == 748 && lunch.at("M", "2026-11-17 12:59").consumed == 748 &&
              lunch.at("M", "2026-11-17 13:00").consumed == 748,
          "uniform uses working minutes, not wall time");
    uniform.tasks[2].durationMinutes = 61;
    uniform.tasks[2].breaks = {{"2026-11-17 09:30", "2026-11-17 10:00"}};
    uniform.resources = {{"R", "R", "labor", 200, {}}, {"R2", "R2", "labor", 200, {}}};
    uniform.assignments = {{"U", "R", 100}, {"U", "R2", 100}};
    MaterialInventoryQuery split(uniform, calculatePlan(uniform));
    check(split.at("M", "2026-11-17 09:45").consumed == 492 && split.at("M", "2026-11-17 10:31").consumed == 1001,
          "execution union/breaks do not double uniform material demand");
    auto later = p;
    later.resourcePlan.receipts.clear();
    later.resourcePlan.materials[0].stockAt = "2026-11-17 09:30";
    later.resourcePlan.materials[0].stockMilli = 5000;
    MaterialInventoryQuery opening(later, calculatePlan(later));
    check(opening.at("M", "2026-11-17 09:30").consumed == 16 && opening.at("M", "2026-11-17 10:01").consumed == 3525,
          "opening stock does not reapply prior consumption");
    auto stale = schedule;
    ++stale.inputRevision;
    rejects([&] { MaterialInventoryQuery bad(p, stale); }, "stale result");
    auto partial = schedule;
    partial.tasks.pop_back();
    rejects([&] { MaterialInventoryQuery bad(p, partial); }, "partial result");
    auto forecast = schedule;
    forecast.isForecast = true;
    rejects([&] { MaterialInventoryQuery bad(p, forecast); }, "forecast cannot invent actual consumption");
    rejects([&] { query.at("M", "bad"); }, "bad query date");
    rejects([&] { query.at("missing", "2026-11-17 09:00"); }, "missing material");
    for (int i = 0; i < 7; ++i) {
        auto bad = p;
        if (i == 0)
            bad.resourcePlan.receipts[0].materialId = "missing";
        if (i == 1)
            bad.resourcePlan.receipts[0].at = "2026-11-17 08:59";
        if (i == 2)
            bad.resourcePlan.receipts[0].quantityMilli = 0;
        if (i == 3)
            bad.resourcePlan.receipts[0].quantityMilli = 1000000000001LL;
        if (i == 4)
            bad.resourcePlan.receipts[1].id = bad.resourcePlan.receipts[0].id;
        if (i == 5)
            bad.resourcePlan.materials[0].stockAt = "2026-11-17 09:00+09:00";
        if (i == 6)
            bad.resourcePlan.receipts[0].at = "bad";
        rejects([&] { validate(bad); }, "invalid receipt/opening accepted");
    }
    check(nativeSchemaFor(p) == 81 && json::decodeProject(json::encodeProject(p), 81) == p, "schema81 exact roundtrip");
    rejects([&] { json::decodeProject(json::encodeProject(p), 80); }, "old schema cannot hide receipts");
    auto legacy = p;
    legacy.resourcePlan.receipts.clear();
    legacy.resourcePlan.materials[0].stockAt.reset();
    check(nativeSchemaFor(legacy) == 20, "legacy minimum writer");
    for (int i = 0; i < 5; ++i) {
        Project archived;
        archived.name = "archive";
        archived.startDate = p.startDate;
        if (i == 0) {
            archived.baselines.push_back({});
            archived.baselines.back().input = p;
        }
        if (i == 1) {
            archived.scenarios.push_back({});
            archived.scenarios.back().input = p;
        }
        if (i == 2) {
            archived.xerScheduleAdoptions.push_back({});
            archived.xerScheduleAdoptions.back().after = p;
        }
        if (i == 3) {
            archived.currencyConversions.push_back({});
            archived.currencyConversions.back().before = std::make_shared<const Project>(p);
        }
        if (i == 4) {
            archived.ledgerIdMigrations.push_back({});
            archived.ledgerIdMigrations.back().after = std::make_shared<const Project>(p);
        }
        check(nativeSchemaFor(archived) == 81, "schema81 scans archived plans/full copies");
    }
    auto body = json::encodeProject(p);
    auto rp = body["resourcePlan"].toObject();
    auto receipts = rp["receipts"].toArray();
    auto receipt = receipts[0].toObject();
    receipt["quantityMilli"] = 1.5;
    receipts[0] = receipt;
    rp["receipts"] = receipts;
    body["resourcePlan"] = rp;
    rejects([&] { json::decodeProject(body, 81); }, "fractional native quantity rejected");
    auto lp = p;
    lp.tasks = {{"S", "자재 필요", 60, {}}};
    lp.resourcePlan.uses = {{"s", "S", "M", 3000, "start"}};
    lp.resourcePlan.materials.resize(1);
    lp.resourcePlan.materials[0].stockMilli = 0;
    lp.resourcePlan.receipts = {{"r", "M", "2026-11-17 10:00", 3000}};
    lp.resources = {{"R", "R", "labor", 100, {}}};
    lp.assignments = {{"S", "R", 100}};
    auto failed = levelResources(lp, "task-order", 2);
    check(failed.leveled.status == CalculationStatus::Conflict && failed.leveled.tasks.empty(),
          "late receipt cannot make infeasible leveling valid");
    lp.tasks[0].constraintType = ConstraintType::SNET;
    lp.tasks[0].constraintAt = "2026-11-17 10:00";
    auto feasible = levelResources(lp, "task-order", 2);
    check(feasible.leveled.status == CalculationStatus::Valid && feasible.leveled.levelingFloatCalculated,
          "manual date adjustment validates stock and calculates material float");
    Workspace w;
    w.id = "stock-pool";
    w.projects = {{lp.documentId, "", lp.name}};
    w.resources = {{"pool", "시공조"}};
    w.bindings = {{lp.documentId, "R", "pool"}};
    w.integrated.forecast = false;
    w.integrated.levelShared = true;
    w.integrated.horizonWorkDays = 2;
    auto integrated = calculateIntegratedWorkspace(w, {std::make_shared<const Project>(lp)});
    check(integrated.status == CalculationStatus::Valid && integrated.tasks[0].floatAvailable &&
              integrated.floatPolicy.ends_with("/resource-material-float-v1"),
          "integrated manual stock checked and float calculated");
    lp.tasks[0].constraintAt = "2026-11-17 09:00";
    check(calculateIntegratedWorkspace(w, {std::make_shared<const Project>(lp)}).status == CalculationStatus::Conflict,
          "integrated late receipt rejected");
    auto control = std::make_shared<CalculationControl>();
    MaterialInventoryQuery cancelled(p, schedule, control);
    control->cancel();
    bool stopped = false;
    try {
        cancelled.at("M", "2026-11-17 10:00");
    } catch (const CalculationCancelled &) {
        stopped = true;
    }
    check(stopped, "cancel after construction");
    QTemporaryDir temp;
    JsonProjectStore files;
    auto path = temp.filePath("inventory.ppm").toStdString();
    files.save(path, p, {});
    Document doc(files);
    doc.open(path);
    auto changed = p.resourcePlan;
    changed.receipts[0].quantityMilli = 3000;
    doc.setResourcePlan(changed);
    doc.undo();
    check(doc.project()->resourcePlan == p.resourcePlan, "receipt undo");
    doc.redo();
    check(doc.project()->resourcePlan == changed, "receipt redo");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-17T01:00:00Z"));
    session.reset();
    check(recovery.scan().candidates.front().valid, "receipt recovery");
    const auto baseline = doc.captureBaseline("입고 기준 계획", "PM", "검산", "2026-11-17T02:00:00Z");
    check(!baseline.empty() && doc.project()->baselines.back().input.resourcePlan == changed,
          "baseline freezes dated stock/receipts");
    const auto scenario = doc.saveScenario("입고 시나리오", "task-order", 2, "2026-11-17T02:00:00Z",
                                           doc.project()->documentId, doc.project()->revision);
    check(!scenario.empty() && doc.project()->scenarios.back().input.resourcePlan == changed,
          "scenario retains dated stock/receipts");
    doc.save();
    check(files.load(path).project == *doc.project(), "schema81 archived exact file roundtrip");
    check(p == before, "query mutated input");
    auto priced = p;
    for (const auto &t : priced.tasks) {
        TaskCost cost;
        cost.taskId = t.id;
        cost.fixedMinor = 0;
        priced.taskCosts.push_back(cost);
    }
    auto oldPrices = priced;
    oldPrices.resourcePlan.receipts.clear();
    oldPrices.resourcePlan.materials[0].stockAt.reset();
    check(analyzeCosts(priced, "").plan == 700 && analyzeCosts(oldPrices, "").plan == 700,
          "receipts do not double count procurement cost");
    auto dir = QDir::currentPath() + "/material-inventory-qa";
    QDir().mkpath(dir);
    QFile::remove(dir + "/source.ppm");
    files.save((dir + "/source.ppm").toStdString(), p, {});
    QByteArray vectors = "at\treceived\tconsumed\tremaining\tavailable\tshortage\n";
    for (int n = 0; n < 1440; ++n) {
        char at[24];
        std::snprintf(at, sizeof(at), "2026-11-17 %02d:%02d", n / 60, n % 60);
        auto v = query.at("M", at);
        auto num = [](auto n) { return n ? QByteArray::number(*n) : QByteArray("?"); };
        vectors += QByteArray(at) + '\t' + num(v.received) + '\t' + num(v.consumed) + '\t' + num(v.remaining) + '\t' +
                   num(v.available) + '\t' + num(v.shortage) + '\n';
    }
    save(dir + "/points.tsv", vectors);
    ReportOptions options;
    options.reportId = "material-inputs";
    options.generatedAt = "2026-11-17T01:00:00Z";
    auto report = buildReport(p, options);
    QFile::remove(dir + "/inputs.xlsx");
    QFile::remove(dir + "/inputs.pdf");
    exportXlsx(report, dir + "/inputs.xlsx");
    exportPdf(report, dir + "/inputs.pdf");
    std::cout << "PASS material receipts/stock baseline/inclusive events/uniform "
                 "allocation/breaks/unknown/shortage/leveling/schema81/undo/recovery\n";
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore files;
    auto p = fixture();
    auto path = temp.filePath("source.ppm").toStdString();
    files.save(path, p, {});
    Document doc(files);
    doc.open(path);
    auto original = doc.project()->resourcePlan;
    ResourcePlanDialog editor(doc);
    auto *receipts = editor.findChild<QTableWidget *>("resourcePlanTable5");
    check(receipts && receipts->rowCount() == 5, "receipt editor loads");
    receipts->item(0, 1)->setText("bad");
    editor.findChild<QPushButton *>("saveResourcePlan")->click();
    check(doc.project()->resourcePlan == original && !editor.findChild<QLabel *>("resourcePlanError")->text().isEmpty(),
          "invalid receipt leaves source unchanged");
    receipts->item(0, 1)->setText("2026-11-17 09:00");
    receipts->item(0, 2)->setText("3");
    editor.findChild<QPushButton *>("saveResourcePlan")->click();
    check(doc.project()->resourcePlan.receipts[0].quantityMilli == 3000, "receipt edit committed");
    doc.undo();
    check(doc.project()->resourcePlan == original, "receipt editor one undo");
    int stage = 0;
    auto dir = QDir::currentPath() + "/material-inventory-qa";
    QDir().mkpath(dir);
    QString pendingExtension;
    QTimer fileTimer;
    fileTimer.setInterval(50);
    QObject::connect(&fileTimer, &QTimer::timeout, [&] {
        auto *file = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (!file)
            return;
        file->setOption(QFileDialog::DontConfirmOverwrite);
        auto *name = file->findChild<QLineEdit *>("fileNameEdit");
        check(name, "inventory export filename input missing");
        // Enter an absolute filename, as a user would, without depending on
        // the asynchronously populated filesystem view's selection.
        name->setText(dir + "/point." + pendingExtension);
        QMetaObject::invokeMethod(file, "accept");
    });
    fileTimer.start();
    const auto chooseFile = [&](QString extension) { pendingExtension = extension; };
    QTimer timer;
    timer.setInterval(30);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *active = QApplication::activeModalWidget();
        auto *d = qobject_cast<QDialog *>(active);
        if (!d || d->objectName() != "materialInventoryDialog" || d->property("materialInventoryBusy").toBool())
            return;
        auto *table = d->findChild<QTableWidget *>("materialInventoryTable");
        auto *date = d->findChild<QLineEdit *>("materialInventoryAt");
        auto *run = d->findChild<QPushButton *>("calculateMaterialInventory");
        if (stage == 0 && table->rowCount() == 2) {
            stage = 1;
            date->setText("2026-11-17 10:01");
            run->click();
        } else if (stage == 1 && table->rowCount() == 2) {
            check(table->item(0, 6)->text() == "-0.001" && table->item(1, 6)->text() == "미정",
                  "signed/unknown inventory displayed");
            check(d->grab().save(dir + "/review.png"), "inventory screenshot");
            stage = 2;
            QFile::remove(dir + "/point.json");
            chooseFile("json");
            d->findChild<QPushButton *>("exportMaterialInventoryJSON")->click();
        } else if (stage == 2) {
            check(QFile::exists(dir + "/point.json"), "JSON saved");
            stage = 3;
            QFile::remove(dir + "/point.xlsx");
            chooseFile("xlsx");
            d->findChild<QPushButton *>("exportMaterialInventoryXLSX")->click();
        } else if (stage == 3) {
            check(QFile::exists(dir + "/point.xlsx"), "XLSX saved");
            stage = 4;
            QFile::remove(dir + "/point.pdf");
            chooseFile("pdf");
            d->findChild<QPushButton *>("exportMaterialInventoryPDF")->click();
        } else if (stage == 4) {
            check(QFile::exists(dir + "/point.pdf"), "PDF saved");
            auto plan = doc.project()->resourcePlan;
            plan.receipts[0].quantityMilli += 1000;
            doc.setResourcePlan(plan);
            check(table->rowCount() == 0 && !d->findChild<QPushButton *>("exportMaterialInventoryJSON")->isEnabled(),
                  "receipt edit invalidates output");
            stage = 5;
            run->click();
        } else if (stage == 5 && table->rowCount() == 2) {
            check(table->item(0, 6)->text() == "0.999", "fresh receipt generation");
            date->setText("bad");
            run->click();
            check(table->rowCount() == 0, "bad query clears output");
            stage = 6;
            d->reject();
        }
    });
    timer.start();
    showMaterialInventory(doc, nullptr);
    timer.stop();
    fileTimer.stop();
    check(stage == 6, "inventory query/export workflow completed");
    std::cout << "PASS receipt editor/null/signed stock/three exports/revision invalidation\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
#ifdef _WIN32
    auto font = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "inventory timeout\n";
        std::_Exit(2);
    });
    watchdog.start(90000);
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
