#include "persistence/PrimaveraXer.h"
#include "persistence/ProjectXml.h"
#include <algorithm>
#include <tuple>
#include "calculation/EnginePolicy.h"
#include "application/Document.h"
#include "domain/Classification.h"
#include "domain/ResourcePlan.h"
#include "calculation/Costs.h"
#include "calculation/Resources.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectFile.h"
#include "reporting/ReportExport.h"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
using namespace ppm;
namespace {
void check(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
void rejects(const std::function<void()> &f) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
Project fixture() {
    Project p;
    p.documentId = "resource-fixture";
    p.name = "자재·비용·역할 검증";
    p.startDate = "2026-11-02";
    p.tasks = {{"A", "시공", 960, {}}, {"B", "검사", 480, {}}};
    p.taskCosts = {{"A", 0, "uniform", "physical"}, {"B", 0, "uniform", "physical"}};
    p.resourcePlan.materials = {{"M", "볼트", "개", 199, 2000}};
    p.resourcePlan.uses = {{"U", "A", "M", 1500, "start"}};
    p.resourcePlan.expenses = {{"E", "A", "운송", "운송비", 101, "uniform"}};
    return p;
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write");
}
void costs() {
    auto p = fixture();
    check(formatMaterialQuantity(100000000000000001LL) == "100000000000000.001", "large quantity formatting overflow");
    const auto old = p;
    validate(p);
    check(materialCost(p.resourcePlan.materials[0], 1500) == 299, "half-up material cost");
    check(materialCost(p.resourcePlan.materials[0], 1) == 0, "fraction rounding");
    auto overflow = p.resourcePlan.materials[0];
    overflow.unitPriceMinor = maxMoney;
    check(!materialCost(overflow, 1001), "overflow not unknown");
    const auto a = analyzeCosts(p, {});
    check(a.plan == 400, "charges not included in plan once");
    const auto days = plannedCostDays(p, calculatePlan(p), "2026-11-02", "2026-11-03");
    std::int64_t total = 0;
    for (const auto &d : days) {
        check(d.amount.has_value(), "known daily charge");
        total += *d.amount;
    }
    check(total == 400, "daily total disagrees");
    auto first =
        std::find_if(days.begin(), days.end(), [](const auto &d) { return d.taskId == "A" && d.date == "2026-11-02"; });
    check(first != days.end() && first->amount == 350, "start material + uniform day1");
    check(p == old, "cost mutated source");
    p.resourcePlan.materials[0].unitPriceMinor.reset();
    check(!analyzeCosts(p, {}).plan, "unknown price became zero");
    auto unknown = plannedCostDays(p, calculatePlan(p), "2026-11-02", "2026-11-03");
    for (const auto &d : unknown)
        if (d.taskId == "A" && d.date == "2026-11-03")
            check(d.amount == 50, "unknown start charge poisoned later day");
    p = old;
    p.resourcePlan.materials[0].stockMilli = 1000;
    check(calculatePlan(p).status == CalculationStatus::Valid, "stock changed logical dates");
    check(levelResources(p, "task-order", 260).leveled.status == CalculationStatus::Conflict,
          "insufficient material leveled");
    p.resourcePlan.materials[0].stockMilli.reset();
    check(!materialReadiness(p).empty(), "unknown inventory ignored");
    p = old;
    p.resourcePlan.uses[0].quantityMilli = 0;
    rejects([&] { validate(p); });
    p = old;
    p.resourcePlan.expenses[0].taskId = "missing";
    rejects([&] { validate(p); });
    const auto xer = exportPrimaveraXer(old);
    const auto xml = exportProjectXml(old);
    check(xer.project == old && xml.project == old, "exchange changed native resource source");
    bool xerNotice = false, xmlNotice = false;
    for (const auto &d : xer.report["diagnostics"].toArray())
        if (d.toObject()["path"] == "PROJECT/resourcePlan")
            xerNotice = true;
    for (const auto &d : xml.report["diagnostics"].toArray())
        if (d.toObject()["path"] == "/Project/ResourcePlan")
            xmlNotice = true;
    check(xerNotice && xmlNotice, "exchange omitted resource preservation notice");
    auto body = json::encodeProject(old);
    check(json::decodeProject(body, 20) == old, "schema20 roundtrip");
    rejects([&] { json::decodeProject(body, 19); });
    auto bad = body;
    auto rp = bad["resourcePlan"].toObject();
    auto ms = rp["materials"].toArray();
    auto m = ms[0].toObject();
    m["stockMilli"] = 1.5;
    ms[0] = m;
    rp["materials"] = ms;
    bad["resourcePlan"] = rp;
    rejects([&] { json::decodeProject(bad, 20); });
}
void document() {
    QTemporaryDir tmp;
    JsonProjectStore files;
    auto p = fixture();
    p.resources = {{"R", "시공조", "labor", 100, {}}};
    p.resources[0].rates = {{"2026-01-01", 0}};
    const auto path = tmp.filePath("source.ppm").toStdString();
    files.save(path, p, {});
    Document d(files);
    d.open(path);
    const auto before = *d.project();
    auto plan = p.resourcePlan;
    plan.roles = {{"ROLE", "설치조", "labor"}};
    plan.demands = {{"D", "A", "ROLE", "R", 100}};
    d.setResourcePlan(plan);
    check(d.project()->assignments.size() == 1 && isEffort(*d.project()), "binding did not manage native assignment");
    d.undo();
    check(d.project()->resourcePlan == before.resourcePlan && d.project()->assignments.empty(), "undo resource plan");
    d.redo();
    check(d.project()->resourcePlan == plan, "redo resource plan");
    d.setAssignmentPeriods("A", "R", {{0, 240, 50}});
    const auto noOpRevision = d.project()->revision;
    d.setResourcePlan(plan);
    check(d.project()->assignments[0].periods == std::vector<AssignmentPeriod>{{0, 240, 50}}, "role save lost contour");
    check(d.project()->revision == noOpRevision, "no-op role save changed revision");
    d.setAssignmentPeriods("A", "R", {});
    const auto copy = d.duplicateTasks({"A"});
    check(d.project()->resourcePlan.uses.size() == 2 && d.project()->resourcePlan.demands.size() == 2,
          "duplicate omitted plan refs");
    check(d.project()->resourcePlan.demands.back().taskId == copy[0] && d.project()->assignments.size() == 2,
          "duplicate role not remapped");
    d.removeTasks(copy);
    check(d.project()->resourcePlan.uses.size() == 1 && d.project()->resourcePlan.expenses.size() == 1 &&
              d.project()->assignments.size() == 1,
          "delete orphan refs");
    auto unresolved = d.project()->resourcePlan;
    unresolved.demands[0].resourceId.clear();
    d.setResourcePlan(unresolved);
    check(calculate(*d.project()).status == CalculationStatus::Incomplete, "unresolved role scheduled");
    d.undo();
    check(calculate(*d.project()).status == CalculationStatus::Valid, "bound role not scheduled");
    auto incompatible = d.project()->resourcePlan;
    incompatible.roles[0].type = "equipment";
    const auto unchanged = *d.project();
    rejects([&] { d.setResourcePlan(incompatible); });
    check(*d.project() == unchanged, "invalid edit partially applied");
    const auto baseline = d.captureBaseline("기준", "PM", "검증", "2026-11-05T00:00:00Z");
    check(d.project()->baselines.back().input.resourcePlan == plan, "baseline omitted resource plan");
    auto changed = plan;
    changed.materials[0].unitPriceMinor = 399;
    d.setResourcePlan(changed);
    check(analyzeCosts(*d.project(), baseline).evm.bac == 400, "baseline budget repriced");
    d.saveScenario("자재 시나리오", "deadline-first", 260, "2026-11-05T00:00:00Z", d.project()->documentId,
                   d.project()->revision);
    check(d.project()->scenarios.back().input.resourcePlan == changed, "scenario omitted resource plan");
    d.save();
    check(files.load(path).project == *d.project(), "native save lost resource plan");
    d.renameTask(1, "복구 검사");
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-05T01:00:00Z"));
    session.reset();
    auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema20 recovery invalid");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(files);
    restored.recover(claim->snapshot);
    check(restored.project()->resourcePlan == changed &&
              restored.project()->baselines.back().input.resourcePlan == plan,
          "recovery lost plan");
    Document ro(files);
    ro.open(path, true);
    rejects([&] { ro.setResourcePlan(plan); });
    d.saveAs("resource-plan.ppm");
    ReportOptions options;
    options.reportId = "resource-plan";
    options.generatedAt = "2026-11-05T01:00:00Z";
    auto report = buildReport(*d.project(), options);
    check(std::any_of(report.tables.begin(), report.tables.end(), [](const auto &t) { return t.id == "materialUses"; }),
          "missing material report");
    exportXlsx(report, "resource-plan.xlsx");
    exportPdf(report, "resource-plan.pdf");
    write("resource-plan-input.json", QJsonDocument(json::encodeProject(*d.project())).toJson());
}
void priorities() {
    auto p = fixture();
    p.resourcePlan = {};
    p.calculationProfile = effortProfile;
    p.tasks[0].durationMinutes = 480;
    p.tasks[1].deadlineAt = "2026-11-02 18:00";
    p.resources = {{"R", "crew", "labor", 100, {}}};
    p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
    const auto source = p;
    auto r = levelResources(p, "deadline-first", 260);
    check(r.leveled.status == CalculationStatus::Valid, "deadline leveling");
    check(r.leveled.tasks[1].start == "2026-11-02 09:00" && r.leveled.tasks[0].start == "2026-11-03 09:00",
          "deadline order golden");
    auto least = levelResources(p, "least-float-first", 260);
    check(least.leveled.status == CalculationStatus::Valid && p == source, "float leveling/source immutable");
    QTemporaryDir tmp;
    JsonProjectStore files;
    const auto path = tmp.filePath("p.ppm").toStdString();
    files.save(path, p, {});
    Document d(files);
    d.open(path);
    LevelingSelection selection;
    selection.priority = "deadline-first";
    selection.policy = levelingPolicyFor(*d.project());
    selection.adoptedAt = "2026-11-05T00:00:00Z";
    selection.reason = "마감 우선";
    selection.sourceDocumentId = p.documentId;
    selection.sourceRevision = p.revision;
    d.adoptLeveling(selection);
    check(d.project()->planLeveling && nativeSchemaFor(*d.project()) == 20, "advanced adoption schema");
    check(calculate(*d.project()).tasks[1].start == "2026-11-02 09:00", "adopt advanced order");
    check(json::decodeProject(json::encodeProject(*d.project()), 20) == *d.project(), "advanced adoption roundtrip");
    d.undo();
    check(!d.project()->planLeveling, "undo advanced adoption");
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const auto font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/malgun.ttf");
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 10));
    try {
        QDir().mkpath("resource-plan-qa");
        check(QDir::setCurrent("resource-plan-qa"), "output directory");
        QFile::remove("resource-plan.ppm");
        QFile::remove("resource-plan.xlsx");
        QFile::remove("resource-plan.pdf");

        costs();
        document();
        priorities();
        std::cout << "Resource plan: exact costs, unknowns, stock, roles, history, archives, recovery, schema20, "
                     "advanced priorities and reports passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
