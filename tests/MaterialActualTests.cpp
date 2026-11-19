#include "domain/LedgerIdentity.h"
#include "domain/ProjectCurrency.h"
#include "application/Document.h"
#include "calculation/MaterialInventory.h"
#include "calculation/MaterialFloatCheck.h"
#include "calculation/IntegratedWorkspace.h"
#include "calculation/Tracking.h"
#include "calculation/Effort.h"
#include "domain/MaterialActuals.h"
#include "domain/ResourcePlan.h"
#include "domain/Classification.h"
#include "domain/NativeSchema.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "reporting/ReportExport.h"
#include "ui/MaterialActualDialog.h"
#include "ui/ResourceComparisonDialog.h"
#include "ui/ResourcePlanDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <iostream>
#include <cstdio>
namespace {
using namespace ppm;
void check(bool value, const char *why) {
    if (!value)
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
MaterialActualEntry entry(const std::string &kind, std::int64_t qty, const std::string &at = "2026-11-17 09:30",
                          const std::string &use = {}) {
    MaterialActualEntry e;
    e.kind = kind;
    e.quantityMilli = qty;
    e.materialId = "M";
    e.at = at;
    e.useId = use;
    if (!use.empty())
        e.taskId = use == "uA" ? "A" : "B";
    return e;
}
MaterialActualSubmission submission(std::vector<MaterialActualEntry> entries) {
    return {std::move(entries), "자재 담당", "실제 입고·소모 확인", "2026-11-17T03:00:00Z"};
}
void record(Project &p, std::vector<MaterialActualEntry> entries) {
    static int seq = 0;
    appendMaterialActuals(p, submission(std::move(entries)), [] { return "mat-" + std::to_string(++seq); });
    ++p.revision;
    validate(p);
}
Project fixture(const std::string &profile = effortProfile) {
    Project p;
    p.documentId = "material-actuals";
    p.name = "실제 자재 잔여 자동 대기";
    p.startDate = "2026-11-17";
    p.statusAt = "2026-11-17 09:30";
    p.calculationProfile = profile;
    p.tasks = {{"A", "진행 작업", 60, {}}, {"B", "후속 작업", 60, {"A"}}};
    p.resources = {{"R", "시공조", "labor", 200, {}}};
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    p.taskCosts = {{"A", 0}, {"B", 0}};
    p.resourcePlan.materials = {{"M", "철근", "kg", 0, 2000}};
    p.resourcePlan.uses = {{"uA", "A", "M", 3000, "start"}, {"uB", "B", "M", 1000, "finish"}};
    p.resourcePlan.receipts = {{"r1", "M", "2026-11-17 11:00", 3000}};
    p.resourcePlan.autoDelayRemainingForReceipts = true;
    ProgressEntry a;
    a.id = "progress-A";
    a.taskId = "A";
    a.statusAt = *p.statusAt;
    a.actualStart = "2026-11-17 09:00";
    a.actualDurationMinutes = 30;
    a.remainingMinutes = 60;
    a.planDurationMinutes = 60;
    a.reporter = "현장";
    a.reason = "진행 확인";
    a.recordedAt = "2026-11-17T03:00:00Z";
    a.sourceDocumentId = p.documentId;
    a.recordedRevision = 1;
    p.progress.push_back(a);
    validate(p);
    return p;
}
void movements(Project &p) {
    auto r = entry("receipt", 1000, "2026-11-17 09:15");
    r.receiptId = "r1";
    auto c = entry("consume", 3000, "2026-11-17 09:20");
    c.taskId = "A";
    record(p, {r, c});
}
void confirm(Project &p, std::int64_t onHand = 0, std::int64_t a = 1000, std::int64_t b = 1000) {
    record(p, {entry("confirm", onHand, *p.statusAt), entry("remaining", a, *p.statusAt, "uA"),
               entry("remaining", b, *p.statusAt, "uB")});
}
const TaskSchedule &row(const ScheduleResult &s, const std::string &id) {
    const auto it = std::find_if(s.tasks.begin(), s.tasks.end(), [&](const auto &v) { return v.taskId == id; });
    check(it != s.tasks.end(), "missing row");
    return *it;
}
void valid(const Project &p, const LevelingResult &r) {
    if (r.leveled.status != CalculationStatus::Valid)
        for (const auto &d : r.diagnostics)
            std::cerr << d.code << ' ';
    check(r.leveled.status == CalculationStatus::Valid && r.after.status == CalculationStatus::Valid,
          "remaining material/resource schedule valid");
    MaterialInventoryQuery q(p, r.leveled);
    check(!q.firstShortage("M"), "no negative remaining inventory");
    check(r.leveled.levelingFloatCalculated || !r.leveled.levelingFloatReason.empty(),
          "material float computed or explicitly unavailable");
}
void contract() {
    for (const auto *profile : {"native-fs0-workminutes-v1", pdmProfile, constraintProfile, effortProfile}) {
        auto p = fixture(profile);
        movements(p);
        auto stock = actualMaterialStock(p, *p.statusAt).front();
        check(stock.opening == 2000 && stock.received == 1000 && stock.consumed == 3000 && stock.remaining == 0 &&
                  !stock.confirmed,
              "actual opening plus receipts minus consumption");
        check(levelResources(p, "task-order", 2).leveled.status != CalculationStatus::Valid,
              "unconfirmed inventory is not assumed complete");
        confirm(p);
        const auto before = p;
        auto result = levelResources(p, "task-order", 2);
        valid(p, result);
        check(result.leveled.levelingFloatCalculated, "remaining material float in all engines");
        MaterialFloatCheck materialFloat(p, result.leveled);
        check(materialFloat.fits(result.leveled.tasks), "remaining material float certificate");
        check(row(result.leveled, "A").start == "2026-11-17 09:00" &&
                  row(result.leveled, "A").remainingStart == "2026-11-17 11:00" &&
                  row(result.leveled, "A").finish == "2026-11-17 12:00",
              "keep actual start and delay remaining segment in all engines");
        check(row(result.leveled, "B").start == "2026-11-17 13:00", "successor propagated across lunch");
        check(p == before &&
                  result.leveled.calculationPolicy.starts_with(std::string(remainingMaterialPolicy) + "/task-order"),
              "immutable input and separate replay policy");
        auto input = remainingMaterialInventoryInput(p);
        check(input.resourcePlan.materials[0].stockMilli == 0 &&
                  input.resourcePlan.materials[0].stockAt == p.statusAt &&
                  input.resourcePlan.receipts[0].quantityMilli == 2000 &&
                  input.resourcePlan.uses[0].quantityMilli == 1000,
              "future receipts only retain undelivered quantity; explicit remaining consumption");
    }
    auto p = fixture();
    movements(p);
    confirm(p);
    auto remapped = migrateLedgerIdentities(p, {{"task", "A", "A2"}});
    validate(remapped);
    check(remapped.materialActuals[1].taskId == "A2" && remapped.materialActuals[3].taskId == "A2" &&
              actualMaterialStock(remapped, *remapped.statusAt).front().remaining == 0,
          "identity migration preserves actual quantities and use links");
    CurrencyRate rate;
    rate.fromCurrency = p.currency;
    rate.targetCurrency = "USD";
    rate.effectiveDate = "2026-11-17";
    rate.reference = "material quantity invariant";
    rate.denominator = 1000;
    auto converted = convertProjectCurrency(p, rate);
    validate(converted);
    check(converted.materialActuals == p.materialActuals &&
              actualMaterialStock(converted, *converted.statusAt).front().remaining == 0,
          "currency conversion does not convert physical stock");
    for (const auto *policy : {retainedProgressPolicy, overrideProgressPolicy, actualDatesProgressPolicy}) {
        auto selected = p;
        selected.progressPolicy = ProgressPolicySelection{
            policy, "PM", "2026-11-17T03:00:00Z", "material policy combination", p.documentId, p.revision};
        valid(selected, levelResources(selected, "task-order", 2));
    }
    Workspace w;
    w.id = "actual-pool";
    w.projects = {{p.documentId, "", p.name}};
    w.resources = {{"pool", "Pool", "labor", 200}};
    w.bindings = {{p.documentId, "R", "pool"}};
    w.integrated.forecast = true;
    w.integrated.levelShared = true;
    auto integrated = calculateIntegratedWorkspace(w, {std::make_shared<const Project>(p)});
    check(integrated.status == CalculationStatus::Valid && integrated.tasks.size() == 2 &&
              integrated.materials.size() == 1,
          "shared actual ledger projection preserves the single-project result");
    auto corrected = p;
    corrected.statusAt = "2026-11-17 10:00";
    auto r = corrected.materialActuals[0];
    r.quantityMilli = 1500;
    r.previousEntryId = r.id;
    record(corrected, {r});
    check(actualMaterialStock(corrected, "2026-11-17 09:30").front().remaining == 0 &&
              actualMaterialStock(corrected, *corrected.statusAt).front().remaining == 500,
          "later correction preserves earlier report cutoff");
    check(corrected.materialActuals.size() == p.materialActuals.size() + 1 &&
              corrected.materialActuals[0] == p.materialActuals[0],
          "append-only correction retains root");
    check(levelResources(corrected, "task-order", 2).leveled.status != CalculationStatus::Valid,
          "new status/correction requires new confirmations");
    confirm(corrected, 500);
    valid(corrected, levelResources(corrected, "task-order", 2));
    auto same = p;
    auto c = same.materialActuals[1];
    c.previousEntryId = c.id;
    c.quantityMilli = 2500;
    record(same, {c});
    check(!actualMaterialStock(same, *same.statusAt).front().confirmed,
          "same-status correction invalidates older confirmation");
    rejects([&] { record(same, {c}); }, "stale correction rejected");
    auto bad = p;
    const auto before = bad;
    auto malformed = entry("receipt", -1);
    rejects([&] { record(bad, {malformed}); }, "negative movement rejected");
    check(bad == before, "invalid batch atomic");
    rejects([&] { record(bad, {entry("confirm", 1)}); }, "wrong reconciliation cannot certify inventory");
    auto broken = p;
    broken.resourcePlan.materials[0].unit = "ton";
    rejects([&] { validate(broken); }, "observed unit frozen");
    broken = p;
    broken.resourcePlan.materials[0].stockMilli = 0;
    rejects([&] { validate(broken); }, "opening basis frozen");
    broken = p;
    broken.resourcePlan.receipts.clear();
    rejects([&] { validate(broken); }, "linked planned receipt protected");
    auto empty = fixture();
    movements(empty);
    record(empty, {entry("confirm", 0), entry("remaining", 1000, *empty.statusAt, "uA")});
    check(levelResources(empty, "task-order", 2).leveled.status != CalculationStatus::Valid,
          "missing remaining use is not inferred");
    auto spent = p;
    auto delivery = spent.materialActuals[0];
    delivery.previousEntryId = delivery.id;
    delivery.quantityMilli = 3000;
    record(spent, {delivery});
    confirm(spent, 2000, 3000, 0);
    check(remainingMaterialInventoryInput(spent).resourcePlan.receipts.empty() &&
              levelResources(spent, "task-order", 2).leveled.status != CalculationStatus::Valid,
          "actual receipt cannot be counted again as future supply");
    auto overdue = p;
    overdue.resourcePlan.receipts[0].at = "2026-11-17 09:25";
    validate(overdue);
    check(levelResources(overdue, "task-order", 2).leveled.status != CalculationStatus::Valid,
          "overdue plans do not invent future receipts");
    auto finish = p;
    finish.resourcePlan.uses[0].accrual = "finish";
    auto fin = levelResources(finish, "task-order", 2);
    valid(finish, fin);
    check(row(fin.leveled, "A").remainingStart == "2026-11-17 10:00", "finish consumption allows work before receipt");
    auto uniform = p;
    uniform.resourcePlan.uses[0].accrual = "uniform";
    auto uni = levelResources(uniform, "task-order", 2);
    valid(uniform, uni);
    check(row(uni.leveled, "A").remainingStart == "2026-11-17 10:59",
          "remaining uniform consumption respects minute rounding");
    auto curve = p;
    curve.tasks[0].taskType = TaskType::FixedWork;
    curve.tasks[0].workDriven = true;
    curve.tasks[0].workUnitMinutes = 6000;
    curve.progress[0].remainingWorkUnitMinutes = 3000;
    curve.assignments[0].periods = {{0, 15, 50}, {15, 45, 150}};
    validate(curve);
    valid(curve, levelResources(curve, "task-order", 2));
    auto bounded = p;
    bounded.tasks[0].constraintType = ConstraintType::FNLT;
    bounded.tasks[0].constraintAt = "2026-11-17 10:45";
    check(levelResources(bounded, "task-order", 2).leveled.status != CalculationStatus::Valid,
          "hard finish date is not relaxed for stock");
    auto dated = p;
    dated.remainingWork = {{"A", "R", *dated.statusAt, {{"2026-11-17", 6000}}}};
    validate(dated);
    valid(dated, levelResources(dated, "task-order", 2));
    auto completed = p;
    auto b = completed.progress[0];
    b.id = "progress-B";
    b.taskId = "B";
    b.actualFinish = "2026-11-17 09:30";
    b.remainingMinutes = 0;
    completed.progress.push_back(b);
    validate(completed);
    check(levelResources(completed, "task-order", 2).leveled.status != CalculationStatus::Valid,
          "completed task cannot retain positive material demand");
    confirm(completed, 0, 1000, 0);
    valid(completed, levelResources(completed, "task-order", 2));
    auto voided = p;
    auto movement = voided.materialActuals[1];
    movement.previousEntryId = movement.id;
    movement.quantityMilli = 0;
    record(voided, {movement});
    check(actualMaterialStock(voided, *voided.statusAt).front().remaining == 3000,
          "zero correction voids prior consumption without deleting history");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope scope(control);
        actualMaterialStock(p, *p.statusAt);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled, "actual stock cooperative cancellation");
    check(nativeSchemaFor(p) == 83 && json::decodeProject(json::encodeProject(p), 83) == p,
          "native83 exact ledger roundtrip");
    rejects([&] { json::decodeProject(json::encodeProject(p), 82); }, "82 rejects new ledger and option");
    QTemporaryDir temp;
    JsonProjectStore files;
    auto path = temp.filePath("actual.ppm").toStdString();
    files.save(path, p, {});
    Document doc(files);
    doc.open(path);
    LevelingSelection selection;
    selection.policy = remainingMaterialPolicy;
    selection.adoptedAt = "2026-11-17T03:00:00Z";
    selection.reason = "실제 재고 잔여 배치";
    selection.sourceDocumentId = doc.project()->documentId;
    selection.sourceRevision = doc.project()->revision;
    selection.horizonWorkDays = 2;
    doc.adoptLeveling(selection);
    check(doc.project()->remainingLeveling && row(calculate(*doc.project()), "A").remainingStart == "2026-11-17 11:00",
          "adopt/replay remaining material schedule");
    doc.saveScenario("실제 재고 시나리오", "task-order", 2, "2026-11-17T03:00:00Z", doc.project()->documentId,
                     doc.project()->revision);
    auto scenario = scenarioProject(doc.project()->scenarios.back());
    check(scenario.materialActuals == doc.project()->materialActuals, "scenario freezes ledger");
    valid(scenario, levelResources(scenario, "task-order", 2, remainingMaterialPolicy));
    auto root = doc.project()->materialActuals[0];
    root.previousEntryId = root.id;
    root.quantityMilli = 1500;
    doc.recordMaterialActuals(submission({root}));
    check(!doc.project()->remainingLeveling && !doc.canUndo(),
          "actual correction releases adoption and cannot undo submitted history");
    check(levelResources(*doc.project(), "task-order", 2).leveled.status != CalculationStatus::Valid,
          "changed actual ledger requires renewed stock confirmation");
    doc.recordMaterialActuals(
        submission({entry("confirm", 500), entry("remaining", 1000, *doc.project()->statusAt, "uA"),
                    entry("remaining", 1000, *doc.project()->statusAt, "uB")}));
    selection.sourceRevision = doc.project()->revision;
    doc.adoptLeveling(selection);
    check(row(calculate(*doc.project()), "A").remainingStart == "2026-11-17 11:00",
          "readopt after actual correction and reconciliation");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-17T03:00:00Z"));
    session.reset();
    check(recovery.scan().candidates.front().valid, "native83 recovery");
    doc.save();
    check(files.load(path).project == *doc.project(), "file roundtrip with correction/scenario/history");
    check(!doc.previewRemoveTasks({"A"}).protectedTaskIds.empty(), "actual-observed task protected");
    auto dir = QDir::currentPath() + "/material-actual-qa";
    QDir().mkpath(dir);
    QFile::remove(dir + "/source.ppm");
    files.save((dir + "/source.ppm").toStdString(), *doc.project(), {});
    QJsonArray cases;
    for (int seed = 0; seed < 24; ++seed) {
        auto v = fixture();
        v.documentId = "actual-case-" + std::to_string(seed);
        v.progress[0].sourceDocumentId = v.documentId;
        v.progress[0].remainingMinutes = 15 + (seed * 7) % 45;
        v.tasks[1].durationMinutes = 20 + (seed * 11) % 35;
        if (seed % 3)
            v.tasks[1].predecessors.clear();
        v.resources[0].maxUnits = seed % 2 ? 100 : 200;
        v.resourcePlan.receipts[0].quantityMilli = 10000;
        const int minute = 660 + (seed * 17) % 120;
        char stamp[24];
        std::snprintf(stamp, sizeof(stamp), "2026-11-17 %02d:%02d", minute / 60, minute % 60);
        v.resourcePlan.receipts[0].at = stamp;
        v.resourcePlan.uses[0].accrual = seed % 3 == 0 ? "start" : seed % 3 == 1 ? "finish" : "uniform";
        v.resourcePlan.uses[1].accrual = seed % 2 ? "start" : "uniform";
        auto delivery = entry("receipt", 1000, "2026-11-17 09:15");
        delivery.receiptId = "r1";
        record(v, {delivery, entry("consume", 3000 - (seed % 3) * 500, "2026-11-17 09:20")});
        confirm(v, (seed % 3) * 500, 1000 + (seed % 3) * 1000, 1000 + (seed % 2) * 1000);
        const auto priority = seed % 3 == 0 ? "task-order" : seed % 3 == 1 ? "shortest-first" : "longest-first";
        auto r = levelResources(v, priority, 2);
        valid(v, r);
        QJsonArray rows;
        for (const auto &t : r.leveled.tasks)
            rows.append(QJsonObject{{"taskId", QString::fromStdString(t.taskId)},
                                    {"start", QString::fromStdString(t.start)},
                                    {"remainingStart", QString::fromStdString(t.remainingStart.value_or(t.start))},
                                    {"finish", QString::fromStdString(t.finish)}});
        cases.append(QJsonObject{{"input", json::encodeProject(v)}, {"priority", priority}, {"rows", rows}});
    }
    QFile oracle(dir + "/cases.json");
    check(oracle.open(QIODevice::WriteOnly | QIODevice::Truncate) && oracle.write(QJsonDocument(cases).toJson()) > 0,
          "save independent fixtures");
    oracle.close();
    ReportOptions options;
    options.reportId = "actual-materials";
    options.generatedAt = "2026-11-17T03:00:00Z";
    auto report = buildReport(*doc.project(), options);
    QFile::remove(dir + "/report.xlsx");
    QFile::remove(dir + "/report.pdf");
    exportXlsx(report, dir + "/report.xlsx");
    exportPdf(report, dir + "/report.pdf");
    std::cout << "PASS actual material ledger/corrections/reconciliation/remaining stock/all engines/future receipt "
                 "residuals/remaining spans/curves/native83/adoption/scenario/recovery\n";
}
void wait(const std::function<bool()> &ready) {
    for (int i = 0; i < 3000 && !ready(); ++i)
        QTest::qWait(10);
    check(ready(), "UI completed");
}
void ui() {
    QTemporaryDir temp;
    JsonProjectStore files;
    auto p = fixture();
    auto path = temp.filePath("ui.ppm").toStdString();
    files.save(path, p, {});
    Document doc(files);
    doc.open(path);
    MaterialActualDialog dialog(doc);
    dialog.show();
    dialog.findChild<QLineEdit *>("materialActualReporter")->setText("담당자");
    dialog.findChild<QLineEdit *>("materialActualReason")->setText("실제 보고");
    auto *type = dialog.findChild<QComboBox *>("materialActualKind");
    auto *receipt = dialog.findChild<QComboBox *>("materialActualReceipt");
    auto *at = dialog.findChild<QLineEdit *>("materialActualAt");
    auto *amount = dialog.findChild<QLineEdit *>("materialActualQuantity");
    auto *submit = dialog.findChild<QPushButton *>("submitMaterialActual");
    receipt->setCurrentIndex(receipt->findData("r1"));
    at->setText("2026-11-17 09:15");
    amount->setText("1");
    submit->click();
    check(doc.project()->materialActuals.size() == 1, "UI actual receipt");
    type->setCurrentIndex(type->findData("consume"));
    at->setText("2026-11-17 09:20");
    amount->setText("3");
    submit->click();
    check(doc.project()->materialActuals.size() == 2, "UI consumption");
    auto *stocks = dialog.findChild<QTableWidget *>("materialActualStocks");
    auto *uses = dialog.findChild<QTableWidget *>("materialRemainingUses");
    stocks->item(0, 6)->setText("0");
    uses->item(0, 4)->setText("1");
    uses->item(1, 4)->setText("1");
    dialog.findChild<QPushButton *>("confirmMaterialStock")->click();
    check(actualMaterialStock(*doc.project(), *doc.project()->statusAt).front().confirmed &&
              doc.project()->materialActuals.size() == 5,
          "UI reconciliation batch");
    dialog.findChild<QTabWidget *>()->setCurrentIndex(1);
    dialog.resize(1120, 820);
    auto dir = QDir::currentPath() + "/material-actual-qa";
    QDir().mkpath(dir);
    check(dialog.grab().save(dir + "/review.png"), "UI screenshot");
    ResourceComparisonDialog comparison(doc);
    comparison.show();
    auto *adopt = comparison.findChild<QPushButton *>("adoptLeveling");
    wait([&] { return adopt->isEnabled(); });
    comparison.findChild<QLineEdit *>("levelingReason")->setText("상태일 실제 재고 대기");
    adopt->click();
    wait([&] { return doc.project()->remainingLeveling.has_value(); });
    check(row(calculate(*doc.project()), "A").remainingStart == "2026-11-17 11:00", "UI remaining adoption");
    comparison.reject();
    check(!submit->isEnabled(), "changed document invalidates ledger draft");
    dialog.findChild<QPushButton *>("reloadMaterialActual")->click();
    check(submit->isEnabled(), "explicit reload restores editing");
    dialog.reject();
    doc.save();
    Document ro(files);
    ro.open(path, true);
    MaterialActualDialog readonly(ro);
    check(!readonly.findChild<QPushButton *>("submitMaterialActual")->isEnabled() &&
              !readonly.findChild<QPushButton *>("confirmMaterialStock")->isEnabled(),
          "read-only actual ledger");
    ResourcePlanDialog editor(doc);
    auto *option = editor.findChild<QCheckBox *>("autoDelayRemainingForReceipts");
    check(option && option->isChecked(), "remaining option editor");
    std::cout << "PASS actual ledger UI/receipt/consumption/reconciliation/adoption/stale/reload/readonly\n";
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
    QObject::connect(&watchdog, &QTimer::timeout, [] { std::_Exit(2); });
    watchdog.start(150000);
    try {
        if (app.arguments().contains("--ui"))
            ui();
        else
            contract();
    } catch (const std::exception &e) {
        std::cerr << e.what();
        if (auto *a = dynamic_cast<const AppError *>(&e))
            for (const auto &s : a->diagnostic().arguments)
                std::cerr << ' ' << s;
        return 1;
    }
}
