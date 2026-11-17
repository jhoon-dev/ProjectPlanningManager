#include "application/Document.h"
#include "calculation/IntegratedWorkspace.h"
#include "calculation/Effort.h"
#include "calculation/LevelingFloat.h"
#include "calculation/MaterialFloatCheck.h"
#include "calculation/Resources.h"
#include "domain/Classification.h"
#include "domain/ResourcePlan.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/ProjectJson.h"
#include "reporting/IntegratedExport.h"
#include "reporting/ReportExport.h"
#include "ui/IntegratedWorkspaceDialog.h"
#include "ui/LevelingWitnessDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdio>
#include <iostream>
using namespace jobs;
namespace {
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
Project fixture(const std::string &profile = effortProfile) {
    Project p;
    p.documentId = "material-float";
    p.name = "자재 제약 여유·임계";
    p.startDate = "2026-11-17";
    p.calculationProfile = profile;
    p.tasks = {{"A", "자재 작업", 2, {}}, {"B", "후속 작업", 2, {"A"}}, {"H", "완료일", 10, {}}};
    p.resources = {{"R", "시공조", "labor", 100, {}}};
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    p.resourcePlan.materials = {{"M", "철근", "kg", 0, 0}};
    p.resourcePlan.uses = {{"uA", "A", "M", 1000, "start"}, {"uB", "B", "M", 1000, "finish"}};
    p.resourcePlan.receipts = {{"r", "M", "2026-11-17 09:02", 2000}};
    p.resourcePlan.autoDelayForReceipts = true;
    for (const auto &t : p.tasks)
        p.taskCosts.push_back({t.id, 0});
    validate(p);
    return p;
}
const TaskSchedule &row(const ScheduleResult &r, const std::string &id) {
    auto it = std::find_if(r.tasks.begin(), r.tasks.end(), [&](const auto &t) { return t.taskId == id; });
    check(it != r.tasks.end(), "missing row");
    return *it;
}
void calculated(const ScheduleResult &r) {
    if (!r.levelingFloatCalculated)
        std::cerr << r.levelingFloatReason << '\n';
    check(r.status == CalculationStatus::Valid && r.levelingFloatCalculated, "material float unavailable");
    check(r.calculationPolicy.ends_with("/resource-material-float-v1"), "material policy missing");
    for (const auto &t : r.tasks)
        if (!t.completedActual)
            check(t.totalFloatMinutes >= t.freeFloatMinutes && t.freeFloatMinutes >= 0 &&
                      t.critical == (t.totalFloatMinutes == 0),
                  "invalid float/critical");
}
void certify(const Project &p, const ScheduleResult &r) {
    MaterialFloatCheck stock(p, r);
    check(stock.fits(r.tasks), "current inventory");
    check(bool(r.levelingWitnesses), "missing material witnesses");
    for (const auto &proof : r.levelingWitnesses->tasks) {
        auto witness = r;
        for (const auto &d : proof.dates) {
            auto it = std::find_if(witness.tasks.begin(), witness.tasks.end(),
                                   [&](const auto &t) { return t.taskId == d.taskId; });
            check(it != witness.tasks.end(), "unknown witness ID");
            it->start = d.start;
            it->finish = d.finish;
            it->remainingStart = d.remainingStart;
        }
        check(stock.fits(witness.tasks), "witness inventory failed");
        check(resourceLoad(p, witness).status == CalculationStatus::Valid, "witness resource failed");
    }
}
QJsonObject record(const Project &p, const ScheduleResult &r) {
    QJsonArray rows, links;
    for (const auto &t : r.tasks)
        rows.append(QJsonObject{{"id", QString::fromStdString(t.taskId)},
                                {"start", QString::fromStdString(t.start)},
                                {"finish", QString::fromStdString(t.finish)},
                                {"tf", double(t.totalFloatMinutes)},
                                {"ff", double(t.freeFloatMinutes)},
                                {"critical", t.critical}});
    for (const auto &l : r.resourceOrderLinks)
        links.append(
            QJsonObject{{"from", QString::fromStdString(l.predecessorId)}, {"to", QString::fromStdString(l.taskId)}});
    return {{"input", json::encodeProject(p)}, {"rows", rows}, {"links", links}};
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size(), "save artifact");
}
QString dir() {
    auto d = QDir::currentPath() + "/material-float-qa";
    QDir().mkpath(d);
    return d;
}
void contract() {
    QJsonArray cases;
    for (const auto *profile : {"native-fs0-workminutes-v1", pdmProfile, constraintProfile, effortProfile}) {
        auto p = fixture(profile);
        const auto before = p;
        const auto r = levelResources(p, "task-order", 2).leveled;
        calculated(r);
        certify(p, r);
        check(row(r, "A").start == "2026-11-17 09:02" && row(r, "B").start == "2026-11-17 09:04",
              "arrival dates changed");
        check(row(r, "A").totalFloatMinutes == 4 && row(r, "A").freeFloatMinutes == 0 &&
                  row(r, "B").totalFloatMinutes == 4 && row(r, "B").freeFloatMinutes == 4 && row(r, "H").critical,
              "material critical chain floats");
        check(p == before, "float modified input");
        cases.append(record(p, r));
    }
    // Material-only tasks must not be skipped by the renewable-resource fast path.
    for (const auto *accrual : {"start", "finish", "uniform"}) {
        auto p = fixture();
        p.resources.clear();
        p.assignments.clear();
        p.tasks[1].predecessors.clear();
        p.resourcePlan.uses[0].accrual = accrual;
        p.resourcePlan.uses[1].accrual = accrual;
        p.resourcePlan.receipts = {{"r1", "M", "2026-11-17 09:02", 1000}, {"r2", "M", "2026-11-17 09:06", 1000}};
        auto r = levelResources(p, "task-order", 2).leveled;
        calculated(r);
        certify(p, r);
        cases.append(record(p, r));
    }
    for (int seed = 0; seed < 18; ++seed) {
        auto p = fixture();
        p.documentId = "oracle-" + std::to_string(seed);
        p.tasks[0].durationMinutes = 1 + seed % 3;
        p.tasks[1].durationMinutes = 1 + (seed / 3) % 3;
        if (seed % 2)
            p.tasks[1].predecessors.clear();
        if (seed % 3 == 0) {
            p.resources.clear();
            p.assignments.clear();
        }
        const std::vector<std::string> kinds = {"start", "finish", "uniform"};
        p.resourcePlan.uses[0].accrual = kinds[seed % 3];
        p.resourcePlan.uses[0].quantityMilli = 1 + seed;
        p.resourcePlan.uses[1].accrual = kinds[(seed + 1) % 3];
        p.resourcePlan.uses[1].quantityMilli = 2 + seed;
        p.resourcePlan.materials[0].stockMilli = seed % 4;
        p.resourcePlan.receipts = {{"r1", "M", "2026-11-17 09:02", 3 + seed}, {"r2", "M", "2026-11-17 09:06", 100}};
        auto r = levelResources(p, "task-order", 2).leveled;
        calculated(r);
        certify(p, r);
        cases.append(record(p, r));
    }
    // A later start finishes earlier: without inventory A has 3 minutes; arrival makes it critical.
    {
        auto p = fixture();
        p.calendars = {{"task", "8분", {1, 2, 3, 4, 5}, {{540, 548}}, {}},
                       {"res", "뒤 4분", {1, 2, 3, 4, 5}, {{544, 548}}, {}}};
        p.tasks = {{"A", "비단조 공수", 2, {}}, {"B", "고정 이정표", 0, {"A"}}, {"H", "완료일", 8, {}}};
        for (auto &t : p.tasks)
            t.calendarId = "task";
        p.tasks[0].taskType = TaskType::FixedWork;
        p.tasks[0].workDriven = true;
        p.tasks[0].workUnitMinutes = 3;
        p.tasks[1].dependencyRules = {{"A", DependencyType::SS, 0}};
        p.tasks[1].constraintType = ConstraintType::MSO;
        p.tasks[1].constraintAt = "2026-11-17 09:03";
        p.resources[0].calendarId = "res";
        p.resources[0].maxUnits = 300;
        p.assignments = {{"A", "R", 1, {{0, 2, 0}, {2, 4, 300}, {4, 100, 1}}}};
        p.resourcePlan.uses = {{"uA", "A", "M", 1000, "finish"}};
        p.resourcePlan.receipts = {{"r", "M", "2026-11-17 09:07", 1000}};
        auto without = p;
        without.resourcePlan = {};
        const auto resourceOnly = levelResources(without, "task-order", 2).leveled;
        check(resourceOnly.levelingFloatCalculated && row(resourceOnly, "A").totalFloatMinutes == 3,
              "nonmonotone resource-only float");
        auto r = levelResources(p, "task-order", 2).leveled;
        calculated(r);
        certify(p, r);
        check(row(r, "A").finish == "2026-11-17 09:07" && row(r, "A").critical && row(r, "A").totalFloatMinutes == 0,
              "arrival must restrict nonmonotone float");
        auto c = record(p, r);
        c["kind"] = "curved";
        cases.append(c);
        MaterialFloatCheck checkStock(p, r);
        auto bad = r.tasks;
        bad[0] = TaskExecution(p, p.tasks[0]).dates(1);
        auto badSchedule = r;
        badSchedule.tasks = bad;
        check(resourceLoad(p, badSchedule).status == CalculationStatus::Valid && !checkStock.fits(bad),
              "nonmonotone finish consumes before arrival");
    }
    // Inventory certificates reject a resource-feasible schedule that consumes before arrival.
    auto p = fixture();
    auto r = levelResources(p, "task-order", 2).leveled;
    auto reversed = r;
    std::reverse(reversed.tasks.begin(), reversed.tasks.end());
    calculateLevelingFloat(p, reversed);
    calculated(reversed);
    check(row(reversed, "A") == row(r, "A") && row(reversed, "H") == row(r, "H"),
          "material float must address task IDs");
    MaterialFloatCheck inventory(p, r);
    auto bad = r.tasks;
    bad[0].start = "2026-11-17 09:00";
    bad[0].finish = "2026-11-17 09:02";
    check(!inventory.fits(bad), "negative inventory certificate accepted");
    check(inventory.fits({row(r, "A")}), "partial necessary-condition certificate");
    {
        auto control = std::make_shared<CalculationControl>();
        control->cancel();
        bool cancelled = false;
        try {
            EvaluationScope scope(control);
            inventory.fits(r.tasks);
        } catch (const CalculationCancelled &) {
            cancelled = true;
        }
        check(cancelled, "material certificate cancellation");
    }
    auto old = calculatePlan(p); // Logical plan remains distinct from opted-in leveling.
    check(row(old, "A").start == "2026-11-17 09:00", "logical plan changed");
    // Adopted historical baselines replay their pre-material-float result.
    LevelingSelection selection;
    selection.policy = levelingPolicyFor(p);
    selection.reason = "자재 여유 승인";
    selection.adoptedAt = "2026-11-17T03:00:00Z";
    selection.sourceDocumentId = p.documentId;
    selection.sourceRevision = p.revision;
    selection.horizonWorkDays = 2;
    p.planLeveling = selection;
    auto historical = calculatePlan(p, "jobs-adopted-leveling-1.8");
    check(historical.status == CalculationStatus::Valid && !historical.levelingFloatCalculated,
          "historical material float changed");
    auto modern = calculatePlan(p);
    calculated(modern);
    check(modern.engineVersion == materialFloatEngine, "material engine identity");
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("baseline.ppm").toStdString();
    store.save(path, p, {});
    Document doc(store);
    doc.open(path);
    doc.captureBaseline("자재 여유 승인", "PM", "입고 고려", "2026-11-17T03:00:00Z");
    check(nativeSchemaFor(*doc.project()) == 84, "material baseline requires84");
    verifyBaseline(doc.project()->baselines.back());
    auto legacy = doc.project()->baselines.back();
    legacy.engineVersion = historical.engineVersion;
    legacy.schedule = historical.tasks;
    // Baselines do not persist the transient witness/policy; exact rows are the replay contract.
    verifyBaseline(legacy);
    for (bool work : {false, true}) {
        auto periods = p;
        periods.assignments[0].periods = {{0, 1, 50}};
        if (work) {
            periods.tasks[0].taskType = TaskType::FixedWork;
            periods.tasks[0].workDriven = true;
            periods.tasks[0].workUnitMinutes = 200;
        }
        auto oldPeriods = calculatePlan(periods, work ? "jobs-adopted-work-periods-1.0" : "jobs-adopted-periods-1.0");
        check(oldPeriods.status == CalculationStatus::Valid && !oldPeriods.levelingFloatCalculated,
              "old material period engine replay");
        auto frozen = legacy;
        frozen.input = periods;
        frozen.engineVersion = oldPeriods.engineVersion;
        frozen.schedule = oldPeriods.tasks;
        frozen.projectFinish = oldPeriods.projectFinish.value();
        verifyBaseline(frozen);
        calculated(calculatePlan(periods));
    }
    doc.save();
    check(store.load(path).project == *doc.project(), "schema84 roundtrip");
    bool refused = false;
    try {
        json::decodeProject(json::encodeProject(*doc.project()), 83);
    } catch (const AppError &) {
        refused = true;
    }
    check(refused, "schema83 accepted material-float baseline");
    auto historic = *doc.project();
    historic.baselines = {legacy};
    check(nativeSchemaFor(historic) == 82, "historical schema minimum changed");
    verifyBaseline(json::decodeProject(json::encodeProject(historic), 82).baselines.back());
    auto nested = fixture();
    nested.currencyConversions.push_back({});
    nested.currencyConversions.back().before = std::make_shared<const Project>(*doc.project());
    check(nativeSchemaFor(nested) == 84, "frozen material baseline minimum");
    // Shared capacity and per-document material stocks; a manual source keeps its forward dates.
    auto a = fixture(), b = fixture();
    a.documentId = "P";
    b.documentId = "Q";
    a.tasks[2].durationMinutes = 12;
    b.tasks[2].durationMinutes = 12;
    b.resourcePlan.autoDelayForReceipts = false;
    b.tasks[0].constraintType = ConstraintType::SNET;
    b.tasks[0].constraintAt = "2026-11-17 09:02";
    Workspace w;
    w.id = "material-float-pool";
    w.projects = {{"P", "", "P"}, {"Q", "", "Q"}};
    w.resources = {{"pool", "공유 시공조", "labor", 100}};
    w.bindings = {{"P", "R", "pool"}, {"Q", "R", "pool"}};
    w.integrated = {false, true, "task-order", 2};
    const auto beforeA = a, beforeB = b;
    const auto shared =
        calculateIntegratedWorkspace(w, {std::make_shared<const Project>(a), std::make_shared<const Project>(b)});
    check(shared.status == CalculationStatus::Valid && shared.floatPolicy.ends_with("/resource-material-float-v1"),
          "mixed manual/automatic shared float");
    for (const auto &t : shared.tasks)
        check(t.floatAvailable && t.schedule.totalFloatMinutes == (t.ref.taskId == "H" ? 0 : 2),
              "shared material total float");
    check(a == beforeA && b == beforeB, "shared float changed source");
    for (const auto &d : shared.diagnostics)
        check(d.code != "LEVELING_REPARTITION_WITNESS" && d.code != "LEVELING_REPARTITION_WITNESS_PREVIEW",
              "shared report must not expose private witness IDs");
    cases.append(QJsonObject{{"kind", "shared"},
                             {"projects", QJsonArray{json::encodeProject(a), json::encodeProject(b)}},
                             {"result", QJsonDocument::fromJson(integratedWorkspaceJson(w, shared, 1)).object()}});
    auto report = integratedWorkspaceReport(w, shared, 1, "2026-11-17T03:00:00Z");
    QFile::remove(dir() + "/shared.xlsx");
    QFile::remove(dir() + "/shared.pdf");
    exportXlsx(report, dir() + "/shared.xlsx");
    exportPdf(report, dir() + "/shared.pdf");
    save(dir() + "/shared.json", integratedWorkspaceJson(w, shared, 1));
    {
        auto largeA = a, largeB = b;
        for (auto *p : {&largeA, &largeB})
            for (int i = 0; i < 5000; ++i) {
                Material m;
                m.id = "unused-" + std::to_string(i);
                m.name = m.id;
                m.unit = "kg";
                m.stockMilli = 0;
                m.stockAt = "2026-11-17 09:00";
                p->resourcePlan.materials.push_back(m);
            }
        auto bounded = calculateIntegratedWorkspace(
            w, {std::make_shared<const Project>(largeA), std::make_shared<const Project>(largeB)});
        check(bounded.status == CalculationStatus::Valid && bounded.tasks.size() == 6 &&
                  bounded.finish == shared.finish && bounded.floatPolicy.empty(),
              "float-only projection limit must retain valid dates");
        check(std::all_of(bounded.tasks.begin(), bounded.tasks.end(),
                          [](const auto &t) { return !t.floatAvailable && !t.schedule.critical; }),
              "limited material float must not publish false critical tasks");
    }
    save(dir() + "/cases.json", QJsonDocument(cases).toJson());
    QFile::remove(dir() + "/baseline84.ppm");
    store.save((dir() + "/baseline84.ppm").toStdString(), *doc.project(), {});
    QFile::remove(dir() + "/historical82.ppm");
    store.save((dir() + "/historical82.ppm").toStdString(), historic, {});
    std::cout << "PASS material float: 27 exact fixtures, all profiles, no-resource groups, nonmonotone critical, "
                 "shared mixed opt-in, witnesses, replay82/84\n";
}
void ui() {
    auto p = fixture();
    const auto r = levelResources(p, "task-order", 2).leveled;
    calculated(r);
    LevelingWitnessDialog witness(p, r, "A", "자재 제약 배치");
    witness.show();
    QApplication::processEvents();
    witness.findChild<QCheckBox *>("witnessChangedOnly")->setChecked(false);
    auto *table = witness.findChild<QTableView *>("witnessDates");
    check(table && table->model()->rowCount() == 3, "material witness UI");
    check(witness.grab().save(dir() + "/witness.png"), "save witness screenshot");
    std::cout << "PASS material witness UI\n";
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
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
