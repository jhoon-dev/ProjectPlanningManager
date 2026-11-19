#include "calculation/IntegratedWorkspace.h"
#include "domain/Classification.h"
#include "domain/ResourcePlan.h"
#include "persistence/ProjectJson.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonWorkspaceStore.h"
#include "reporting/IntegratedExport.h"
#include "reporting/ReportExport.h"
#include "ui/DesktopWorkspace.h"
#include "ui/IntegratedWorkspaceDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <iostream>
namespace {
using namespace ppm;
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
MaterialActualEntry entry(const std::string &kind, std::int64_t qty, const std::string &use = {}) {
    MaterialActualEntry e;
    e.kind = kind;
    e.quantityMilli = qty;
    e.materialId = "M";
    e.at = "2026-11-17 09:30";
    e.useId = use;
    if (!use.empty())
        e.taskId = use == "uA" ? "A" : "B";
    return e;
}
void record(Project &p, std::vector<MaterialActualEntry> rows) {
    auto seq = p.materialActuals.size();
    appendMaterialActuals(p, {std::move(rows), "자재 담당", "상태일 원장", "2026-11-17T03:00:00Z"},
                          [&] { return "observation-" + std::to_string(++seq); });
    ++p.revision;
    validate(p);
}
void confirm(Project &p, std::int64_t stock = 0, std::int64_t a = 1000, std::int64_t b = 1000) {
    record(p, {entry("confirm", stock), entry("remaining", a, "uA"), entry("remaining", b, "uB")});
}
Project project(const std::string &id, const std::string &arrival = "2026-11-17 11:00",
                const std::string &profile = effortProfile, const std::string &start = "2026-11-17") {
    Project p;
    p.documentId = id;
    p.name = "자재 프로젝트 " + id;
    p.startDate = start;
    p.calculationProfile = profile;
    p.statusAt = "2026-11-17 09:30";
    p.tasks = {{"A", "진행 작업", 60, {}}, {"B", "후속 작업", 60, {"A"}}};
    p.resources = {{"R", "시공조", "labor", 200, {}}};
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    p.resourcePlan.materials = {{"M", "철근", "kg", 0, 2000}};
    p.resourcePlan.uses = {{"uA", "A", "M", 3000, "start"}, {"uB", "B", "M", 1000, "finish"}};
    p.resourcePlan.receipts = {{"r1", "M", arrival, 3000}};
    p.resourcePlan.autoDelayRemainingForReceipts = true;
    ProgressEntry a;
    a.id = "progress";
    a.taskId = "A";
    a.statusAt = *p.statusAt;
    a.actualStart = "2026-11-17 09:00";
    a.actualDurationMinutes = 30;
    a.remainingMinutes = 60;
    a.planDurationMinutes = 60;
    a.reporter = "PM";
    a.reason = "진행 확인";
    a.recordedAt = "2026-11-17T03:00:00Z";
    a.sourceDocumentId = id;
    a.recordedRevision = 1;
    p.progress = {a};
    auto r = entry("receipt", 1000);
    r.at = "2026-11-17 09:15";
    r.receiptId = "r1";
    auto c = entry("consume", 3000);
    c.at = "2026-11-17 09:20";
    c.taskId = "A";
    record(p, {r, c});
    confirm(p);
    return p;
}
Workspace workspace(int capacity = 100) {
    Workspace w;
    w.id = "shared-materials";
    w.projects = {{"P", "", "프로젝트 P"}, {"Q", "", "프로젝트 Q"}};
    w.resources = {{"pool", "공유 시공조", "labor", capacity}};
    w.bindings = {{"P", "R", "pool"}, {"Q", "R", "pool"}};
    w.integrated = {true, true, "task-order", 3};
    return w;
}
std::vector<std::shared_ptr<const Project>> sources(const Project &p, const Project &q) {
    return {std::make_shared<const Project>(p), std::make_shared<const Project>(q)};
}
IntegratedResult run(const Workspace &w, const Project &p, const Project &q) {
    return calculateIntegratedWorkspace(w, sources(p, q));
}
const IntegratedTask &row(const IntegratedResult &r, const std::string &pid, const std::string &task) {
    auto t = std::find_if(r.tasks.begin(), r.tasks.end(),
                          [&](const auto &v) { return v.ref.projectId == pid && v.ref.taskId == task; });
    check(t != r.tasks.end(), "missing qualified row");
    return *t;
}
void valid(const IntegratedResult &r) {
    if (r.status != CalculationStatus::Valid)
        for (const auto &d : r.diagnostics) {
            std::cerr << d.code << ' ';
            for (const auto &a : d.arguments)
                std::cerr << a << ' ';
        }
    check(r.status == CalculationStatus::Valid && r.tasks.size() == 4 && r.shared.status == CalculationStatus::Valid,
          "valid shared material/resources");
    check(!r.floatPolicy.empty() || std::any_of(r.diagnostics.begin(), r.diagnostics.end(),
                                                [](const auto &d) { return d.code == "LEVELING_FLOAT_UNAVAILABLE"; }),
          "float capability or explicit reason");
    for (const auto &day : r.shared.days)
        check(day.overloadMinutes == 0 && day.unknownCapacityMinutes == 0, "shared future load feasible");
}
void failed(const IntegratedResult &r) {
    check(r.status != CalculationStatus::Valid && r.tasks.empty() && r.materials.empty() && !r.finish,
          "failed result exposes no candidate dates/balances");
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size(), "save artifact");
}
void contract() {
    auto p = project("P"), q = project("Q");
    auto w = workspace();
    const auto beforeP = p, beforeQ = q;
    check(p.materialActuals[0].id == q.materialActuals[0].id && p.progress[0].id == q.progress[0].id,
          "fixture intentionally collides IDs");
    auto r = run(w, p, q);
    valid(r);
    check(r.floatPolicy.ends_with("/resource-material-float-v1") &&
              std::all_of(r.tasks.begin(), r.tasks.end(),
                          [](const auto &t) {
                              return t.floatAvailable && t.schedule.totalFloatMinutes == 0 &&
                                     t.schedule.freeFloatMinutes == 0 && t.schedule.critical;
                          }),
          "shared material critical chain");
    check(r.forecast && r.policy.ends_with("/material-remaining-v1") && r.materials.size() == 2,
          "shared actual policy and per-project balances");
    check(row(r, "P", "A").schedule.start == "2026-11-17 09:00" &&
              row(r, "P", "A").schedule.remainingStart == "2026-11-17 11:00" &&
              row(r, "P", "B").schedule.start == "2026-11-17 13:00",
          "preserve actual start and propagate P material wait");
    check(row(r, "Q", "A").schedule.start == "2026-11-17 09:00" &&
              row(r, "Q", "A").schedule.remainingStart == "2026-11-17 14:00" &&
              row(r, "Q", "B").schedule.finish == "2026-11-17 16:00",
          "shared capacity protects future P reservations");
    check(p == beforeP && q == beforeQ, "integrated calculation changed actual sources");
    for (const auto &m : r.materials)
        check(m.materialId == "M" && m.stock.confirmed && m.stock.remaining == 0 && m.incoming == 2000 &&
                  m.remainingUse == 2000,
              "partial receipts not counted twice");
    for (const auto &d : r.diagnostics)
        for (const auto &id : d.taskIds)
            check(!id.starts_with("task-"), "diagnostics expose qualified source tasks");
    auto parallel = workspace(200);
    valid(run(parallel, p, q));
    check(row(run(parallel, p, q), "Q", "A").schedule.remainingStart == "2026-11-17 11:00",
          "shared two-unit supply allows overlap");
    auto isolated = q;
    isolated.resourcePlan.receipts[0].at = "2026-11-17 15:00";
    auto own = run(parallel, p, isolated);
    valid(own);
    check(row(own, "Q", "A").schedule.remainingStart == "2026-11-17 15:00",
          "identical material IDs never share stock or receipt supply");
    auto link = parallel;
    link.links = {{"L", "P", "B", "Q", "A", DependencyType::FS, 0}};
    auto linked = run(link, p, q);
    valid(linked);
    check(row(linked, "Q", "A").schedule.remainingStart == "2026-11-17 14:00",
          "cross-project FS advances remaining start without rewriting actual start");
    auto override = q;
    override.progressPolicy = ProgressPolicySelection{overrideProgressPolicy,   "PM",         "2026-11-17T03:00:00Z",
                                                      "actual endpoint policy", q.documentId, q.revision};
    auto overridden = run(link, p, override);
    valid(overridden);
    check(row(overridden, "Q", "A").schedule.remainingStart == "2026-11-17 11:00",
          "source progress override also applies with actual materials");
    auto correction = p;
    auto e = correction.materialActuals[0];
    e.previousEntryId = e.id;
    e.quantityMilli = 1500;
    record(correction, {e});
    confirm(correction, 500);
    auto ordered = run(w, correction, q);
    valid(ordered);
    check(ordered.materials[0].stock.remaining == 500 && ordered.materials[0].incoming == 1500 &&
              correction.revision > q.revision,
          "heterogeneous audit revisions are globally ordered without mixing corrections");
    auto future = q;
    future.statusAt = "2026-11-17 10:00";
    auto f = future.materialActuals[0];
    f.previousEntryId = f.id;
    f.quantityMilli = 1600;
    record(future, {f});
    future.statusAt = q.statusAt;
    validate(future);
    auto historical = run(w, p, future);
    valid(historical);
    check(historical.materials[1].stock.remaining == 0 && historical.materials[1].incoming == 2000,
          "future-status correction is not applied at the common earlier cutoff");
    auto basis = project("P", "2026-11-17 11:00", effortProfile, "2026-11-16");
    valid(run(w, basis, q));
    for (const auto *profile : {"native-fs0-workminutes-v1", pdmProfile, constraintProfile})
        valid(run(w, project("P", "2026-11-17 11:00", profile), q));
    auto dated = p;
    dated.remainingWork = {{"A", "R", *dated.statusAt, {{"2026-11-17", 6000}}}};
    valid(run(w, dated, q));
    auto ambiguous = dated;
    ambiguous.resources.push_back({"R2", "다른 시공조", "labor", 100, {}});
    ambiguous.assignments.push_back({"A", "R2", 100});
    auto merged = parallel;
    merged.bindings.push_back({"P", "R2", "pool"});
    failed(run(merged, ambiguous, q));
    auto curved = p;
    curved.tasks[0].taskType = TaskType::FixedWork;
    curved.tasks[0].workDriven = true;
    curved.tasks[0].workUnitMinutes = 6000;
    curved.progress[0].remainingWorkUnitMinutes = 3000;
    curved.assignments[0].periods = {{0, 15, 50}, {15, 45, 150}};
    curved.resourcePlan.uses[0].accrual = "uniform";
    valid(run(parallel, curved, q));
    auto available = w;
    available.resources[0].availableFrom = "2026-11-17 11:30";
    auto opening = run(available, p, q);
    valid(opening);
    check(row(opening, "P", "A").schedule.remainingStart == "2026-11-17 11:30",
          "shared availability is combined with inventory");
    auto complete = q;
    auto b = complete.progress[0];
    b.id = "finished-B";
    b.taskId = "B";
    b.actualFinish = "2026-11-17 09:30";
    b.remainingMinutes = 0;
    complete.progress.push_back(b);
    confirm(complete, 0, 1000, 0);
    auto done = run(w, p, complete);
    valid(done);
    check(row(done, "Q", "B").schedule.completedActual && row(done, "Q", "B").schedule.finish == "2026-11-17 09:30",
          "completed actual rows have no future resource/material consumption");
    auto plain = q;
    plain.resourcePlan = {};
    plain.materialActuals.clear();
    valid(run(w, p, plain));
    auto unconfirmed = q;
    std::erase_if(unconfirmed.materialActuals, [](const auto &v) { return v.kind == "confirm"; });
    auto rejected = run(w, p, unconfirmed);
    failed(rejected);
    check(!rejected.diagnostics.empty() && rejected.diagnostics.front().projectId == "Q",
          "missing confirmation points to source Q");
    auto mismatch = q;
    mismatch.statusAt = "2026-11-17 09:31";
    failed(run(w, p, mismatch));
    auto shortSupply = q;
    shortSupply.resourcePlan.receipts[0].quantityMilli = 1999;
    failed(run(w, p, shortSupply));
    auto overdue = q;
    overdue.resourcePlan.receipts[0].at = "2026-11-17 09:25";
    failed(run(w, p, overdue));
    auto cycle = w;
    cycle.links = {{"L", "P", "B", "Q", "A", DependencyType::FS, 0},
                   {"back", "Q", "B", "P", "A", DependencyType::FS, 0}};
    failed(run(cycle, p, q));
    auto logical = w;
    logical.integrated.levelShared = false;
    auto original = run(logical, p, q);
    check(original.status == CalculationStatus::Valid && original.materials.empty() &&
              row(original, "P", "A").schedule.remainingStart == p.statusAt,
          "logical preview does not silently adopt material waiting");
    auto cancelled = std::make_shared<CalculationControl>();
    cancelled->cancel();
    bool caught = false;
    try {
        calculateIntegratedWorkspace(w, sources(p, q), cancelled);
    } catch (const CalculationCancelled &) {
        caught = true;
    }
    check(caught, "shared material cancellation propagated");
    {
        auto many = p;
        auto zero = many.materialActuals[0];
        zero.quantityMilli = 0;
        zero.receiptId.clear();
        zero.previousEntryId.clear();
        zero.recordedRevision = 4;
        many.revision = 4;
        while (many.materialActuals.size() < 50001) {
            zero.id = "extra-" + std::to_string(many.materialActuals.size());
            zero.transactionId = zero.id;
            many.materialActuals.push_back(zero);
        }
        confirm(many);
        auto other = many;
        other.documentId = "Q";
        auto limit = run(w, many, other);
        failed(limit);
        check(std::any_of(limit.diagnostics.begin(), limit.diagnostics.end(),
                          [](const auto &d) { return d.code == "WORKSPACE_LIMIT"; }),
              "combined actual ledger limit rejects without partial dates or balances");
    }
    auto json = QJsonDocument::fromJson(integratedWorkspaceJson(w, r, 42)).object();
    check(json["materialStocks"].toArray().size() == 2 &&
              json["materialStocks"].toArray()[0].toObject()["incomingMilli"].toString() == "2000",
          "qualified balance JSON with exact quantity strings");
    auto large = r;
    large.materials[0].incoming = 9007199254740993LL;
    auto exact = QJsonDocument::fromJson(integratedWorkspaceJson(w, large, 42)).object();
    check(exact["materialStocks"].toArray()[0].toObject()["incomingMilli"].toString() == "9007199254740993",
          "aggregated quantity beyond binary64 integer range remains exact");
    auto dir = QDir::currentPath() + "/shared-material-qa";
    QDir().mkpath(dir);
    JsonProjectStore files;
    for (const auto *v : {&p, &q}) {
        auto path = dir + "/" + QString::fromStdString(v->documentId) + ".ppm";
        QFile::remove(path);
        files.save(path.toStdString(), *v, {});
    }
    auto saved = w;
    for (auto &ref : saved.projects)
        ref.path = (dir + "/" + QString::fromStdString(ref.documentId) + ".ppm").toStdString();
    JsonWorkspaceStore store(files);
    auto path = dir + "/workspace.ppmworkspace";
    QFile::remove(path);
    store.save(path.toStdString(), saved, {});
    auto loaded = store.load(path.toStdString()).workspace;
    valid(run(loaded, files.load(saved.projects[0].path).project, files.load(saved.projects[1].path).project));
    check(nativeSchemaFor(p) == 83 && nativeSchemaFor(q) == 83, "native schema unchanged");
    save(dir + "/result.json", integratedWorkspaceJson(w, r, 42));
    auto report = integratedWorkspaceReport(w, r, 42, "2026-11-17T03:00:00Z");
    QFile::remove(dir + "/report.xlsx");
    QFile::remove(dir + "/report.pdf");
    exportXlsx(report, dir + "/report.xlsx");
    exportPdf(report, dir + "/report.pdf");
    QJsonArray cases;
    for (int seed = 0; seed < 24; ++seed) {
        auto a = project("P"), b = project("Q");
        auto config = workspace(seed % 2 ? 100 : 200);
        config.integrated.priority = seed % 3 == 0 ? "task-order" : seed % 3 == 1 ? "shortest-first" : "longest-first";
        for (int index = 0; index < 2; ++index) {
            auto &v = index ? b : a;
            v.progress[0].remainingMinutes = 15 + (seed * 7 + index * 13) % 45;
            v.tasks[1].durationMinutes = 20 + (seed * 11 + index * 17) % 35;
            if (seed % 3)
                v.tasks[1].predecessors.clear();
            v.resourcePlan.uses[0].accrual = (seed + index) % 3 == 0   ? "start"
                                             : (seed + index) % 3 == 1 ? "finish"
                                                                       : "uniform";
            v.resourcePlan.uses[1].accrual = seed % 2 ? "start" : "uniform";
            int minute = 630 + (seed * 17 + index * 29) % 180;
            char stamp[24];
            std::snprintf(stamp, sizeof(stamp), "2026-11-17 %02d:%02d", minute / 60, minute % 60);
            v.resourcePlan.receipts[0].at = stamp;
            v.resourcePlan.receipts[0].quantityMilli = 10000;
            auto transaction = v.materialActuals[1];
            transaction.previousEntryId = transaction.id;
            transaction.quantityMilli = 3000 - ((seed + index) % 3) * 500;
            record(v, {transaction});
            confirm(v, ((seed + index) % 3) * 500, 1000 + ((seed + index) % 3) * 1000,
                    1000 + ((seed + index) % 2) * 1000);
        }
        if (seed % 4 == 0)
            config.links = {{"L", "P", "B", "Q", "A", DependencyType::FS, 15}};
        auto result = run(config, a, b);
        valid(result);
        cases.append(QJsonObject{
            {"P", json::encodeProject(a)},
            {"Q", json::encodeProject(b)},
            {"priority", QString::fromStdString(config.integrated.priority)},
            {"capacity", *config.resources[0].maxUnits},
            {"linked", !config.links.empty()},
            {"result", QJsonDocument::fromJson(integratedWorkspaceJson(config, result, 100 + seed)).object()}});
    }
    save(dir + "/cases.json", QJsonDocument(cases).toJson());
    check(p == beforeP && q == beforeQ, "sources unchanged after all derived exports");
    std::cout << "PASS shared actual ledger namespace/revisions/stock/future receipts/remaining spans/curves/dated "
                 "work/policies/resources/schema/export; 24 oracle fixtures\n";
}
void ui() {
    QTemporaryDir temp;
    auto files = std::make_shared<JsonProjectStore>();
    auto p = project("P"), q = project("Q");
    for (const auto *v : {&p, &q})
        files->save(temp.filePath(QString::fromStdString(v->documentId) + ".ppm").toStdString(), *v, {});
    DesktopWorkspace workspaceUi(files, {});
    workspaceUi.openProject(temp.filePath("P.ppm").toStdString());
    workspaceUi.openProject(temp.filePath("Q.ppm").toStdString());
    auto config = workspace();
    for (auto &ref : config.projects)
        ref.path = workspaceUi.document(ref.documentId)->path();
    workspaceUi.applyConfiguration(config);
    const auto a = *workspaceUi.document("P")->project(), b = *workspaceUi.document("Q")->project();
    IntegratedWorkspaceDialog dialog(workspaceUi);
    dialog.show();
    dialog.findChild<QPushButton *>("integratedCalculate")->click();
    auto *tasks = dialog.findChild<QTableWidget *>("integratedTasks");
    auto *stocks = dialog.findChild<QTableWidget *>("integratedMaterialStocks");
    auto *output = dialog.findChild<QPushButton *>("integratedJson");
    check(tasks->rowCount() == 4 && stocks->rowCount() == 2 && output->isEnabled(),
          "shared actual UI calculates balances and dates");
    check(tasks->item(0, 6)->text() == "2026-11-17 09:00" && tasks->item(0, 8)->text() == "2026-11-17 11:00",
          "UI separates actual and remaining start");
    check(tasks->item(0, 9)->text() == "0" && tasks->item(0, 10)->text() == "0" && tasks->item(0, 11)->text() == "임계",
          "UI material total/free float and critical");
    check(stocks->item(0, 4)->text() == "0" && stocks->item(0, 6)->text() == "2",
          "UI shows reconciled stock and residual supply");
    auto dir = QDir::currentPath() + "/shared-material-qa";
    QDir().mkpath(dir);
    dialog.resize(1240, 950);
    check(dialog.grab().save(dir + "/review.png"), "capture shared actual UI");
    check(*workspaceUi.document("P")->project() == a && *workspaceUi.document("Q")->project() == b &&
              !workspaceUi.document("P")->dirty() && !workspaceUi.document("Q")->dirty(),
          "preview neither mutates nor dirties source documents");
    auto correction = workspaceUi.document("Q")->project()->materialActuals[0];
    correction.previousEntryId = correction.id;
    correction.quantityMilli = 1500;
    workspaceUi.document("Q")->recordMaterialActuals({{correction}, "PM", "입고 정정", "2026-11-17T03:00:00Z"});
    QTest::qWait(300);
    check(tasks->rowCount() == 0 && stocks->rowCount() == 0 && !output->isEnabled(),
          "actual correction invalidates dates/balances/exports");
    dialog.findChild<QPushButton *>("integratedCalculate")->click();
    check(tasks->rowCount() == 0 && !output->isEnabled(), "unconfirmed correction cannot export new dates");
    auto *doc = workspaceUi.document("Q");
    doc->recordMaterialActuals({{entry("confirm", 500), entry("remaining", 1000, "uA"), entry("remaining", 1000, "uB")},
                                "PM",
                                "재고 재확인",
                                "2026-11-17T03:00:00Z"});
    dialog.findChild<QPushButton *>("integratedCalculate")->click();
    check(tasks->rowCount() == 4 && stocks->item(1, 4)->text() == "0.5" && output->isEnabled(),
          "UI recalculates after renewed actual confirmation");
    dialog.findChild<QCheckBox *>("integratedForecast")->setChecked(false);
    check(tasks->rowCount() == 0 && stocks->rowCount() == 0 && !output->isEnabled(),
          "mode edit invalidates balance provenance");
    dialog.reject();
    std::cout
        << "PASS shared material UI/remaining start/balances/actual correction/stale/reconfirm/source preservation\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef _WIN32
    auto font = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [] { std::_Exit(2); });
    timer.start(150000);
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
