#include "calculation/MaterialInventory.h"
#include "calculation/MaterialReservations.h"
#include "calculation/IntegratedWorkspace.h"
#include "calculation/Effort.h"
#include "calculation/Tracking.h"
#include "domain/Classification.h"
#include "domain/ResourcePlan.h"
#include "domain/NativeSchema.h"
#include "application/Document.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "reporting/ReportExport.h"
#include "ui/ResourcePlanDialog.h"
#include "ui/ResourceComparisonDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
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
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <tuple>
#include <iostream>
namespace {
using namespace jobs;
void check(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
bool containsCode(const Diagnostic &d, const std::string &code) {
    return d.code == code ||
           std::any_of(d.causes.begin(), d.causes.end(), [&](const auto &cause) { return containsCode(cause, code); });
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
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size(), "save artifact");
}
Project fixture(const std::string &profile = effortProfile) {
    Project p;
    p.documentId = "material-delay";
    p.name = "자재 입고 자동 대기";
    p.startDate = "2026-11-17";
    p.calculationProfile = profile;
    p.tasks = {{"A", "자재 작업", 60, {}}, {"B", "후속 작업", 60, {"A"}}};
    p.resources = {{"R", "시공조", "labor", 200, {}}};
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    p.resourcePlan.materials = {{"M", "철근", "kg", 0, 0}};
    p.resourcePlan.uses = {{"use-A", "A", "M", 1000, "start"}};
    p.resourcePlan.receipts = {{"receipt", "M", "2026-11-17 10:00", 1000}};
    p.resourcePlan.autoDelayForReceipts = true;
    for (const auto &t : p.tasks)
        p.taskCosts.push_back({t.id, 0});
    validate(p);
    return p;
}
const TaskSchedule &row(const ScheduleResult &s, const std::string &id) {
    auto it = std::find_if(s.tasks.begin(), s.tasks.end(), [&](const auto &r) { return r.taskId == id; });
    check(it != s.tasks.end(), "missing schedule row");
    return *it;
}
LevelingResult run(const Project &p, const std::string &priority = "task-order", int horizon = 2) {
    auto result = levelResources(p, priority, horizon);
    if (result.leveled.status != CalculationStatus::Valid) {
        for (const auto &d : result.diagnostics)
            std::cerr << d.code << ' ';
        std::cerr << '\n';
    }
    return result;
}
void valid(const Project &p, const LevelingResult &r) {
    check(r.leveled.status == CalculationStatus::Valid && r.after.status == CalculationStatus::Valid,
          "material/resource schedule valid");
    MaterialInventoryQuery query(p, r.leveled);
    for (const auto &m : p.resourcePlan.materials)
        check(!query.firstShortage(m.id), "final material inventory feasible");
    check(r.leveled.levelingFloatCalculated || !r.leveled.levelingFloatReason.empty(),
          "material float computed or explicitly unavailable");
}
void contract() {
    for (const auto *profile : {"native-fs0-workminutes-v1", pdmProfile, constraintProfile, effortProfile}) {
        auto p = fixture(profile);
        const auto before = p;
        auto r = run(p);
        valid(p, r);
        check(row(r.leveled, "A").start == "2026-11-17 10:00" && row(r.leveled, "B").start == "2026-11-17 11:00",
              "arrival delay and successor propagation in each scheduler");
        check(p == before, "automatic leveling changed inputs");
        check(r.leveled.calculationPolicy.starts_with(std::string(materialArrivalPolicy) + "/task-order"),
              "new replay policy");
        check(std::any_of(r.diagnostics.begin(), r.diagnostics.end(),
                          [](const auto &d) { return d.code == "MATERIAL_DELAY"; }),
              "material delay provenance");
        p.resourcePlan.autoDelayForReceipts = false;
        check(levelResources(p, "task-order", 2).leveled.status == CalculationStatus::Conflict,
              "schema81 validator behavior retained");
    }
    auto p = fixture();
    p.tasks[1].predecessors.clear();
    p.resourcePlan.uses.push_back({"use-B", "B", "M", 1000, "start"});
    p.resourcePlan.receipts.push_back({"second", "M", "2026-11-17 11:00", 1000});
    auto two = run(p);
    valid(p, two);
    check(row(two.leveled, "A").start == "2026-11-17 10:00" && row(two.leveled, "B").start == "2026-11-17 11:00",
          "one delivery cannot be consumed twice");
    p.tasks[0].durationMinutes = 120;
    p.tasks[1].durationMinutes = 30;
    p.resourcePlan.materials[0].stockMilli = 1000;
    p.resourcePlan.uses[0].accrual = "finish";
    p.resourcePlan.receipts = {{"later", "M", "2026-11-17 12:00", 1000}};
    auto protectedStock = run(p);
    valid(p, protectedStock);
    check(row(protectedStock.leveled, "A").start == "2026-11-17 09:00" &&
              row(protectedStock.leveled, "B").start == "2026-11-17 13:00",
          "earlier low priority demand protects future accepted consumption and calendar");
    for (const auto &[accrual, amount, start] :
         std::vector<std::tuple<std::string, int, std::string>>{{"finish", 1000, "2026-11-17 09:00"},
                                                                {"uniform", 60000, "2026-11-17 09:59"},
                                                                {"uniform", 1, "2026-11-17 09:30"}}) {
        auto q = fixture();
        q.tasks.resize(1);
        q.assignments.resize(1);
        q.taskCosts.resize(1);
        q.resourcePlan.uses[0].accrual = accrual;
        q.resourcePlan.uses[0].quantityMilli = amount;
        q.resourcePlan.receipts[0].quantityMilli = amount;
        auto r = run(q);
        valid(q, r);
        check(row(r.leveled, "A").start == start,
              "earliest legal finish/uniform rounded demand, not conservative whole-task delay");
    }
    auto calendar = fixture();
    calendar.resourcePlan.receipts[0].at = "2026-11-17 12:30";
    auto lunch = run(calendar);
    valid(calendar, lunch);
    check(row(lunch.leveled, "A").start == "2026-11-17 13:00", "lunch arrival respects task calendar");
    calendar.holidays = {"2026-11-17"};
    auto holiday = run(calendar);
    valid(calendar, holiday);
    check(row(holiday.leveled, "A").start == "2026-11-18 09:00", "holiday receipt is available on next work day");
    auto capacity = fixture();
    capacity.resources[0].availableFrom = "2026-11-17 11:00";
    auto limited = run(capacity);
    valid(capacity, limited);
    check(row(limited.leveled, "A").start == "2026-11-17 11:00", "resource capacity combined with arrivals");
    auto opening = fixture();
    opening.resourcePlan.receipts.clear();
    opening.resourcePlan.materials[0].stockMilli = 1000;
    opening.resourcePlan.materials[0].stockAt = "2026-11-17 10:00";
    auto afterOpening = run(opening);
    valid(opening, afterOpening);
    check(row(afterOpening.leveled, "A").start == "2026-11-17 10:00",
          "auto mode does not hide consumption before stock baseline");
    for (int variant = 0; variant < 4; ++variant) {
        auto bad = fixture();
        if (variant == 0) {
            bad.tasks[0].constraintType = ConstraintType::MSO;
            bad.tasks[0].constraintAt = "2026-11-17 09:00";
        }
        if (variant == 1)
            bad.resourcePlan.receipts[0].quantityMilli = 999;
        if (variant == 2)
            bad.resourcePlan.materials[0].stockMilli.reset();
        if (variant == 3)
            bad.resourcePlan.receipts[0].at = "2026-11-18 09:00";
        auto r = levelResources(bad, "task-order", 1);
        check(r.leveled.status != CalculationStatus::Valid && r.leveled.tasks.empty(),
              "hard date/insufficient/unknown/horizon failure returns no placements");
    }
    auto forecast = fixture();
    auto milestone = fixture();
    milestone.tasks[0].durationMinutes = 0;
    auto zero = run(milestone);
    valid(milestone, zero);
    check(row(zero.leveled, "A").start == "2026-11-17 10:00" &&
              row(zero.leveled, "A").finish == row(zero.leveled, "A").start,
          "zero duration material consumption waits for arrival");
    auto bounded = fixture();
    bounded.resourcePlan.uses[0].accrual = "uniform";
    bounded.resourcePlan.receipts[0].at = "2027-02-01 09:00";
    auto exhausted = levelResources(bounded, "task-order", 60);
    check(exhausted.leveled.status != CalculationStatus::Valid && exhausted.leveled.tasks.empty() &&
              std::any_of(exhausted.leveled.diagnostics.begin(), exhausted.leveled.diagnostics.end(),
                          [](const auto &d) { return containsCode(d, "WORKSPACE_LIMIT"); }),
          "bounded search returns a limit diagnosis and no candidate dates");
    forecast.statusAt = "2026-11-17 09:00";
    check(levelResources(forecast, "task-order", 2).leveled.status != CalculationStatus::Valid,
          "forecast actual stock is not invented");
    auto curved = fixture();
    curved.tasks.resize(1);
    curved.assignments.resize(1);
    curved.taskCosts.resize(1);
    curved.tasks[0].taskType = TaskType::FixedWork;
    curved.tasks[0].workDriven = true;
    curved.tasks[0].workUnitMinutes = 6000;
    curved.assignments[0].periods = {{0, 15, 50}, {15, 45, 150}};
    curved.resources[0].maxUnits = 200;
    curved.resourcePlan.uses[0].accrual = "finish";
    auto curve = run(curved);
    valid(curved, curve);
    TaskExecution model(curved, curved.tasks[0]);
    std::optional<TaskSchedule> oracle;
    for (int start = 0; start < 180; ++start) {
        auto candidate = model.dates(start);
        if (candidate.finish >= "2026-11-17 10:00") {
            oracle = candidate;
            break;
        }
    }
    check(oracle && row(curve.leveled, "A").start == oracle->start && row(curve.leveled, "A").finish == oracle->finish,
          "work contour search agrees with exhaustive execution candidates");
    auto pp = fixture(), qq = fixture();
    qq.documentId = "other-project";
    qq.name = "다른 프로젝트";
    qq.resourcePlan.receipts[0].at = "2026-11-17 11:00";
    Workspace w;
    w.id = "arrival-pool";
    w.projects = {{pp.documentId, "", pp.name}, {qq.documentId, "", qq.name}};
    w.resources = {{"pool", "공유조", "labor", 200}};
    w.bindings = {{pp.documentId, "R", "pool"}, {qq.documentId, "R", "pool"}};
    w.integrated.forecast = false;
    w.integrated.levelShared = true;
    w.integrated.horizonWorkDays = 3;
    auto integrated =
        calculateIntegratedWorkspace(w, {std::make_shared<const Project>(pp), std::make_shared<const Project>(qq)});
    check(integrated.status == CalculationStatus::Valid && integrated.tasks.size() == 4,
          "integrated automatic material delay");
    for (const auto &t : integrated.tasks)
        if (t.ref.taskId == "A")
            check(t.schedule.start == (t.ref.projectId == pp.documentId ? "2026-11-17 10:00" : "2026-11-17 11:00"),
                  "same material IDs across projects keep independent stock");
    check(nativeSchemaFor(pp) == 82 && json::decodeProject(json::encodeProject(pp), 82) == pp,
          "schema82 exact roundtrip");
    rejects([&] { json::decodeProject(json::encodeProject(pp), 81); }, "old schema rejects automatic policy");
    auto old = pp;
    old.resourcePlan.autoDelayForReceipts = false;
    check(nativeSchemaFor(old) == 81 && !supportsLevelingPolicy(old, materialArrivalPolicy),
          "old minimum writer/policy retained");
    for (int i = 0; i < 5; ++i) {
        Project archive;
        archive.startDate = pp.startDate;
        if (i == 0) {
            archive.baselines.push_back({});
            archive.baselines.back().input = pp;
        }
        if (i == 1) {
            archive.scenarios.push_back({});
            archive.scenarios.back().input = pp;
        }
        if (i == 2) {
            archive.xerScheduleAdoptions.push_back({});
            archive.xerScheduleAdoptions.back().before = pp;
        }
        if (i == 3) {
            archive.currencyConversions.push_back({});
            archive.currencyConversions.back().after = std::make_shared<const Project>(pp);
        }
        if (i == 4) {
            archive.ledgerIdMigrations.push_back({});
            archive.ledgerIdMigrations.back().before = std::make_shared<const Project>(pp);
        }
        check(nativeSchemaFor(archive) == 82, "schema82 scans all frozen copies");
    }
    auto body = json::encodeProject(pp);
    auto plan = body["resourcePlan"].toObject();
    plan["autoDelayForReceipts"] = 1;
    body["resourcePlan"] = plan;
    rejects([&] { json::decodeProject(body, 82); }, "automatic flag must be bool");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope evaluation(control);
        levelResources(pp, "task-order", 2);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "cancellation propagates");
    QTemporaryDir temp;
    JsonProjectStore files;
    auto path = temp.filePath("source.ppm").toStdString();
    files.save(path, pp, {});
    Document doc(files);
    doc.open(path);
    LevelingSelection selection;
    selection.policy = levelingPolicyFor(*doc.project());
    selection.adoptedAt = "2026-11-17T03:00:00Z";
    selection.reason = "입고 자동 대기";
    selection.sourceDocumentId = doc.project()->documentId;
    selection.sourceRevision = doc.project()->revision;
    selection.horizonWorkDays = 2;
    doc.adoptLeveling(selection);
    check(row(calculatePlan(*doc.project()), "A").start == "2026-11-17 10:00", "adopt/replay automatic dates");
    doc.undo();
    check(!doc.project()->planLeveling && row(calculatePlan(*doc.project()), "A").start == "2026-11-17 09:00",
          "adoption undo restores logical dates");
    doc.redo();
    auto bid = doc.captureBaseline("입고 승인", "PM", "입고 대기", "2026-11-17T03:00:00Z");
    verifyBaseline(doc.project()->baselines.back());
    check(doc.project()->baselines.back().input.resourcePlan.autoDelayForReceipts && !bid.empty(),
          "approved input preserves opt-in policy");
    doc.saveScenario("입고 시나리오", "task-order", 2, "2026-11-17T03:00:00Z", doc.project()->documentId,
                     doc.project()->revision);
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-17T03:00:00Z"));
    session.reset();
    check(recovery.scan().candidates.front().valid, "auto policy recovery");
    doc.save();
    check(files.load(path).project == *doc.project(), "frozen schema82 file roundtrip");
    auto historyPath = temp.filePath("history-only.ppm").toStdString();
    files.save(historyPath, pp, {});
    Document history(files);
    history.open(historyPath);
    auto hs = selection;
    hs.sourceDocumentId = history.project()->documentId;
    hs.sourceRevision = history.project()->revision;
    history.adoptLeveling(hs);
    history.clearLeveling();
    history.setResourcePlan({});
    check(nativeSchemaFor(*history.project()) == 82,
          "new policy in decision history still requires82 after disabling/removing inputs");
    history.save();
    check(files.load(historyPath).project == *history.project(), "history-only policy exact replay");
    rejects([&] { json::decodeProject(json::encodeProject(*history.project()), 81); },
            "old schema rejects history-only automatic policy");
    auto dir = QDir::currentPath() + "/material-delay-qa";
    QDir().mkpath(dir);
    QFile::remove(dir + "/source.ppm");
    files.save((dir + "/source.ppm").toStdString(), *doc.project(), {});
    QJsonArray cases;
    for (int seed = 0; seed < 24; ++seed) {
        auto v = fixture();
        v.documentId = "case-" + std::to_string(seed);
        v.tasks[0].durationMinutes = 15 + (seed * 7) % 45;
        v.tasks[1].durationMinutes = 15 + (seed * 11) % 45;
        if (seed % 3)
            v.tasks[1].predecessors.clear();
        v.resources[0].maxUnits = seed % 2 ? 100 : 200;
        v.resourcePlan.materials[0].stockMilli = (seed % 3) * 1000;
        v.resourcePlan.uses = {{"use-A", "A", "M", 1000 + (seed % 3) * 1000,
                                seed % 3 == 0   ? "start"
                                : seed % 3 == 1 ? "finish"
                                                : "uniform"},
                               {"use-B", "B", "M", 1000 + ((seed + 1) % 3) * 1000, seed % 2 ? "start" : "uniform"}};
        char receipt[24];
        int n = 540 + (seed * 17) % 300;
        std::snprintf(receipt, sizeof(receipt), "2026-11-17 %02d:%02d", n / 60, n % 60);
        v.resourcePlan.receipts = {{"r1", "M", receipt, 2000}, {"r2", "M", "2026-11-17 14:00", 10000}};
        auto priority = seed % 3 == 0 ? "task-order" : seed % 3 == 1 ? "shortest-first" : "longest-first";
        auto r = run(v, priority, 2);
        valid(v, r);
        QJsonArray rows;
        for (const auto &t : r.leveled.tasks)
            rows.append(QJsonObject{{"taskId", QString::fromStdString(t.taskId)},
                                    {"start", QString::fromStdString(t.start)},
                                    {"finish", QString::fromStdString(t.finish)}});
        cases.append(QJsonObject{{"input", json::encodeProject(v)}, {"priority", priority}, {"rows", rows}});
    }
    save(dir + "/cases.json", QJsonDocument(cases).toJson());
    ReportOptions options;
    options.reportId = "arrival-policy";
    options.generatedAt = "2026-11-17T03:00:00Z";
    auto report = buildReport(*doc.project(), options);
    QFile::remove(dir + "/report.xlsx");
    QFile::remove(dir + "/report.pdf");
    exportXlsx(report, dir + "/report.xlsx");
    exportPdf(report, dir + "/report.pdf");
    std::cout << "PASS material automatic delay/all engines/reservations/future "
                 "protection/finish/uniform/constraints/calendar/curves/integrated/schema82/adoption/archives/"
                 "recovery; 24 oracle fixtures\n";
}
void wait(const std::function<bool()> &ready) {
    for (int i = 0; i < 3000 && !ready(); ++i)
        QTest::qWait(10);
    check(ready(), "UI calculation completed");
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore files;
    auto p = fixture();
    p.resourcePlan.autoDelayForReceipts = false;
    auto path = temp.filePath("source.ppm").toStdString();
    files.save(path, p, {});
    Document doc(files);
    doc.open(path);
    ResourcePlanDialog editor(doc);
    auto *automatic = editor.findChild<QCheckBox *>("autoDelayForReceipts");
    check(automatic && !automatic->isChecked(), "opt-in checkbox defaults off");
    automatic->setChecked(true);
    editor.findChild<QPushButton *>("saveResourcePlan")->click();
    check(doc.project()->resourcePlan.autoDelayForReceipts && nativeSchemaFor(*doc.project()) == 82,
          "opt-in editor saved");
    doc.undo();
    check(!doc.project()->resourcePlan.autoDelayForReceipts, "opt-in undo");
    doc.redo();
    ResourceComparisonDialog comparison(doc);
    comparison.show();
    auto *adopt = comparison.findChild<QPushButton *>("adoptLeveling");
    wait([&] { return adopt->isEnabled(); });
    auto *table = comparison.findChild<QTableWidget *>("levelingTasks");
    check(table->item(0, 3)->text() == "2026-11-17 10:00" &&
              comparison.findChild<QPlainTextEdit *>("levelingDiagnostics")->toPlainText().contains("MATERIAL_DELAY"),
          "arrival date and explanation shown");
    auto dir = QDir::currentPath() + "/material-delay-qa";
    QDir().mkpath(dir);
    comparison.resize(1000, 700);
    check(comparison.grab().save(dir + "/review.png"), "UI screenshot");
    comparison.findChild<QLineEdit *>("levelingReason")->setText("입고 시각 자동 대기");
    adopt->click();
    wait([&] { return doc.project()->planLeveling.has_value(); });
    check(row(calculatePlan(*doc.project()), "A").start == "2026-11-17 10:00", "UI adopted material wait");
    comparison.reject();
    doc.saveAs(temp.filePath("readonly.ppm").toStdString());
    Document ro(files);
    ro.open(doc.path(), true);
    ResourcePlanDialog readonly(ro);
    check(!readonly.findChild<QCheckBox *>("autoDelayForReceipts")->isEnabled(), "read-only option locked");
    std::cout << "PASS automatic option/undo/replay/comparison/material explanation/adoption/read-only\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    auto f = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (f >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(f).front(), 9));
#endif
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "material delay timeout\n";
        std::_Exit(2);
    });
    watchdog.start(150000);
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
