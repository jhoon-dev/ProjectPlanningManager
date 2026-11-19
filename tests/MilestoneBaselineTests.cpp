#include "application/Document.h"
#include "calculation/Effort.h"
#include "calculation/Tracking.h"
#include "persistence/JsonProjectStore.h"
#include "ui/ComparisonDialog.h"
#include <QApplication>
#include <QFile>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <future>
#include <iostream>

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
ppm::Project sample(const std::string &profile, bool work = false) {
    ppm::Project p;
    p.documentId = "milestones";
    p.name = "Milestone boundaries";
    p.startDate = "2026-10-26";
    p.calculationProfile = profile;
    p.tasks = {{"M", "M", 0, {}}};
    if (work) {
        p.tasks[0].taskType = ppm::TaskType::FixedWork;
        p.tasks[0].workUnitMinutes = 0;
        p.tasks[0].workDriven = true;
        p.tasks[0].durationMinutes.reset();
    }
    return p;
}
void at(const ppm::ScheduleResult &result, std::size_t index, const std::string &stamp) {
    check(result.status == ppm::CalculationStatus::Valid && result.tasks.at(index).start == stamp &&
              result.tasks.at(index).finish == stamp,
          "milestone delayed, outside a boundary or in conflict");
}
QByteArray bytes(const std::string &path) {
    QFile f(QString::fromStdString(path));
    check(f.open(QIODevice::ReadOnly), "cannot read fixture");
    return f.readAll();
}
void boundaries(const std::string &profile, bool work) {
    for (const auto &stamp : {"2026-10-26 09:00", "2026-10-27 09:00", "2026-10-27 12:00", "2026-10-27 13:00",
                              "2026-10-27 18:00", "2026-11-02 09:00"})
        for (auto type : {ppm::ConstraintType::SNET, ppm::ConstraintType::FNET, ppm::ConstraintType::MSO,
                          ppm::ConstraintType::MFO}) {
            auto p = sample(profile, work);
            p.tasks[0].constraintType = type;
            p.tasks[0].constraintAt = stamp;
            at(ppm::calculatePlan(p), 0, stamp);
            at(ppm::levelResources(p, "task-order", 20).leveled, 0, stamp);
        }
    auto p = sample(profile, work);
    p.holidays = {"2026-10-27"};
    p.tasks[0].constraintType = ppm::ConstraintType::SNET;
    p.tasks[0].constraintAt = "2026-10-27 09:00";
    at(ppm::calculatePlan(p), 0, "2026-10-28 09:00");
    p.holidays.clear();
    p.tasks[0].constraintAt = "2026-10-27 12:30";
    at(ppm::calculatePlan(p), 0, "2026-10-27 13:00");
    for (auto type : {ppm::ConstraintType::MSO, ppm::ConstraintType::MFO}) {
        p.tasks[0].constraintType = type;
        check(ppm::calculatePlan(p).status == ppm::CalculationStatus::Conflict,
              "fixed milestone silently snapped across lunch");
    }
    p.tasks[0].constraintType = ppm::ConstraintType::ASAP;
    p.tasks[0].constraintAt.reset();
    p.statusAt = "2026-10-27 09:00";
    at(ppm::calculate(p), 0, "2026-10-27 09:00");
    p.statusAt = "2026-10-27 12:30";
    at(ppm::calculate(p), 0, "2026-10-27 13:00");
    p.statusAt.reset();
    p.tasks.push_back({"A", "Anchor", 960, {}});
    p.tasks[0].constraintType = ppm::ConstraintType::ALAP;
    at(ppm::calculatePlan(p), 0, "2026-10-27 18:00");
    p.tasks[0].constraintType = ppm::ConstraintType::SNLT;
    p.tasks[0].constraintAt = "2026-10-27 12:30";
    const auto upper = ppm::calculatePlan(p);
    check(upper.status == ppm::CalculationStatus::Valid && upper.tasks[0].totalFloatMinutes == 660,
          "milestone upper bound or float used civil elapsed time");
}
ppm::Project dependencySample(const std::string &profile) {
    auto p = sample(profile);
    p.tasks.push_back({"A", "A", 60, {}});
    p.calendars = {{"tue", "Tuesday", {2}, {{540, 720}, {780, 1080}}, {}}};
    p.tasks[1].calendarId = "tue";
    p.tasks[0].predecessors = {"A"};
    p.tasks[0].dependencyRules = {{"A", ppm::DependencyType::SS, 0}};
    return p;
}
void relations(const std::string &profile) {
    auto p = dependencySample(profile);
    at(ppm::calculatePlan(p), 0, "2026-10-27 09:00");
    p.tasks[0].dependencyRules = {{"A", ppm::DependencyType::SF, 0}};
    at(ppm::calculatePlan(p), 0, "2026-10-27 09:00");
    p.tasks[0].dependencyRules = {{"A", ppm::DependencyType::FF, 0}};
    at(ppm::calculatePlan(p), 0, "2026-10-27 10:00");
    p.tasks[0].dependencyRules.clear();
    at(ppm::calculatePlan(p), 0, "2026-10-27 10:00");
    p.tasks[0].dependencyRules = {{"A", ppm::DependencyType::SS, -480}};
    at(ppm::calculatePlan(p), 0, "2026-10-26 09:00");
    p.tasks[0].dependencyRules = {{"A", ppm::DependencyType::SS, 480}};
    at(ppm::calculatePlan(p), 0, "2026-10-27 18:00");
}
void historical(ppm::JsonProjectStore &store, const std::string &path, const std::string &profile) {
    auto p = profile == ppm::pdmProfile ? dependencySample(profile) : sample(profile);
    if (profile != ppm::pdmProfile) {
        p.tasks[0].constraintType = ppm::ConstraintType::SNET;
        p.tasks[0].constraintAt = "2026-10-27 09:00";
    }
    const auto old = ppm::calculatePlan(p, ppm::legacyEngineFor(p));
    at(old, 0, "2026-10-27 09:01");
    ppm::Baseline b;
    b.id = "B1";
    b.version = 1;
    b.name = "Original";
    b.approvedBy = "PM";
    b.reason = "Approved";
    b.createdAt = "2026-10-30T00:00:00Z";
    b.sourceDocumentId = p.documentId;
    b.inputRevision = 1;
    b.input = p;
    b.engineVersion = old.engineVersion;
    b.projectFinish = *old.projectFinish;
    b.schedule = old.tasks;
    p.baselines = {b};
    ppm::LevelingScenario s;
    s.id = "S1";
    s.name = "Original";
    s.createdAt = b.createdAt;
    s.sourceDocumentId = p.documentId;
    s.inputRevision = 1;
    s.input = p;
    s.policy = ppm::legacyLevelingPolicyFor(p);
    s.priority = "task-order";
    s.horizonWorkDays = 20;
    p.scenarios = {s};
    p.revision = 2;
    store.save(path, p, {});
    const auto disk = bytes(path);
    ppm::Document doc(store);
    doc.open(path);
    check(*doc.project() == p, "historical baseline or scenario was rewritten on open");
    ppm::verifyBaseline(doc.project()->baselines[0]);
    at(ppm::calculatePlan(*doc.project()), 0, "2026-10-27 09:00");
    const auto replay = ppm::levelResources(ppm::scenarioProject(s), s.priority, s.horizonWorkDays, s.policy);
    at(replay.leveled, 0, "2026-10-27 09:01");
    check(replay.leveled.engineVersion == old.engineVersion &&
              replay.leveled.calculationPolicy ==
                  s.policy + "/task-order" + (replay.leveled.levelingFloatCalculated ? "/resource-order-float-v1" : ""),
          "scenario replay silently selected a new engine/policy");
    // The legacy policy must remain isolated to its calling worker.
    auto legacy = std::async(std::launch::async, [&] { return ppm::calculatePlan(p, ppm::legacyEngineFor(p)); });
    at(ppm::calculatePlan(p), 0, "2026-10-27 09:00");
    at(legacy.get(), 0, "2026-10-27 09:01");
    auto control = std::make_shared<ppm::CalculationControl>();
    control->cancel();
    bool cancelled = false;
    {
        ppm::EvaluationScope evaluation(control);
        try {
            ppm::calculatePlan(p, ppm::legacyEngineFor(p));
        } catch (const ppm::CalculationCancelled &) {
            cancelled = true;
        }
    }
    check(cancelled, "historical calculation ignored cancellation");
    at(ppm::calculatePlan(p), 0, "2026-10-27 09:00");
    doc.captureBaseline("Current", "PM", "Approved", b.createdAt);
    check(doc.project()->baselines[0] == b && doc.project()->baselines[1].engineVersion == ppm::engineFor(p),
          "new approval changed an old baseline or used an old engine");
    doc.saveAs(path + ".copy.ppm");
    check(bytes(path) == disk && store.load(doc.path()).project == *doc.project(),
          "historical copy roundtrip changed original bytes or archive metadata");
}
void deleted(ppm::JsonProjectStore &store, ppm::TaskType type, bool nominal) {
    ppm::Document doc(store);
    doc.create("Deleted effort", "2026-10-26", {"Removed", "Keep"});
    doc.setDuration(1, 60);
    const auto id = doc.project()->tasks[0].id;
    const auto rid = doc.putResource({{}, "R", "labor", 100, {}});
    doc.setAssignments(id, {{id, rid, 50}});
    doc.setTaskEffort(0, type, true, nominal ? std::optional<std::int64_t>{480} : std::nullopt, 48000,
                      {{"2026-10-26 13:00", "2026-10-27 13:00"}});
    const auto bid = doc.captureBaseline("B", "PM", "Approved", "2026-10-30T00:00:00Z");
    const auto baseline = doc.project()->baselines[0];
    const auto before = ppm::compareToBaseline(*doc.project(), bid);
    check(before.tasks[0].baselineDuration == 960, "unexpected baseline effort duration");
    doc.removeTasks({id});
    const auto comparison = ppm::compareToBaseline(*doc.project(), bid);
    const auto row =
        std::find_if(comparison.tasks.begin(), comparison.tasks.end(), [&](const auto &r) { return r.taskId == id; });
    check(row != comparison.tasks.end() && row->baselineDuration == 960 &&
              row->baselineStart == before.tasks[0].baselineStart &&
              row->baselineFinish == before.tasks[0].baselineFinish && doc.project()->baselines[0] == baseline,
          "deleted effort duration differs from frozen active duration");
    ppm::ComparisonDialog dialog(doc, bid);
    dialog.show();
    auto *table = dialog.findChild<QTableWidget *>("comparisonTable");
    for (int i = 0; i < 500 && table->rowCount() != 2; ++i)
        QTest::qWait(10);
    check(table->rowCount() == 2, "comparison UI did not finish");
    for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i, 0)->text() == "Removed")
            table->setCurrentCell(i, 0);
    check(dialog.findChild<QPlainTextEdit *>("comparisonDetails")->toPlainText().contains("계획 기간: 2d → —"),
          "deleted effort UI shows nominal/unknown duration instead of sixteen hours");
    dialog.hide();
}
void gantt(ppm::JsonProjectStore &store, bool work) {
    ppm::Document doc(store);
    doc.create("Milestone drag", "2026-10-26", {"M"});
    doc.setDuration(0, 0);
    if (work)
        doc.setTaskEffort(0, ppm::TaskType::FixedWork, true, {}, 0, {});
    else
        doc.setTaskTiming(0, ppm::ConstraintType::ASAP, {}, {});
    doc.applyCalculation(ppm::calculate(*doc.project()));
    const auto before = static_cast<const ppm::PlanInput &>(*doc.project());
    doc.editGantt(doc.project()->documentId, doc.project()->revision, doc.project()->tasks[0].id, false,
                  "2026-10-27 09:00");
    at(ppm::calculatePlan(*doc.project()), 0, "2026-10-27 09:00");
    check(doc.project()->tasks[0].constraintAt == "2026-10-27 09:00", "milestone drag saved a shifted constraint");
    doc.undo();
    check(static_cast<const ppm::PlanInput &>(*doc.project()) == before, "milestone drag undo changed source input");
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        ppm::JsonProjectStore store;
        gantt(store, false);
        gantt(store, true);
        for (const auto profile : {ppm::constraintProfile, ppm::effortProfile}) {
            boundaries(profile, false);
            if (profile == ppm::effortProfile)
                boundaries(profile, true);
        }
        for (const auto profile : {ppm::pdmProfile, ppm::constraintProfile, ppm::effortProfile}) {
            relations(profile);
            historical(store, dir.filePath(QString::fromUtf8(profile) + ".ppm").toStdString(), profile);
        }
        for (auto type : {ppm::TaskType::FixedWork, ppm::TaskType::FixedUnits})
            for (bool nominal : {false, true})
                deleted(store, type, nominal);
        std::cout << "PASS: milestone endpoints/relations/constraints/forecast/leveling/float, historical engines and "
                     "scenario replay, thread isolation, archive bytes and deleted effort baseline UI\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
