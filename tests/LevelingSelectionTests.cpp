#include "application/Document.h"
#include "calculation/Resources.h"
#include "calculation/Costs.h"
#include "calculation/Tracking.h"
#include "calculation/Usage.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/LevelingJson.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <future>
#include <iostream>
namespace {
using namespace jobs;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught, "expected rejection");
}
LevelingSelection selection(const Document &doc) {
    LevelingSelection s;
    s.policy = levelingPolicyFor(*doc.project());
    s.adoptedAt = "2026-11-03T00:00:00Z";
    s.reason = "과부하 해소";
    s.sourceDocumentId = doc.project()->documentId;
    s.sourceRevision = doc.project()->revision;
    return s;
}
auto prepare(Document &doc, EditCommand command,
             std::shared_ptr<CalculationControl> control = std::make_shared<CalculationControl>()) {
    auto request = doc.makeEditRequest(std::move(command));
    return std::async(std::launch::async,
                      [request = std::move(request), control]() mutable {
                          return Document::prepareEdit(std::move(request), control);
                      })
        .get();
}
void contracts() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document doc(store);
    doc.create("평준화 채택", "2026-11-02", {"A", "B", "C"});
    for (int i = 0; i < 3; ++i) {
        doc.setDuration(i, 480);
        doc.setTaskCost({doc.project()->tasks[i].id, 0});
    }
    Resource resource;
    resource.name = "공유 인력";
    resource.rates = {{"2026-11-02", 1}, {"2026-11-03", 2}};
    const auto rid = doc.putResource(resource);
    const auto assignmentSource = doc.snapshot();
    for (const auto &t : assignmentSource->tasks)
        doc.setAssignments(t.id, {{t.id, rid, 100}});
    const auto raw = *doc.project();
    const auto oldBaseline = doc.captureBaseline("논리 계획", "PM", "최초", "2026-11-03T00:00:00Z");
    const auto logical = calculatePlan(*doc.project());
    check(*logical.projectFinish == "2026-11-02 18:00", "fixture logical finish wrong");
    auto prepared = prepare(doc, AdoptLevelingEdit{selection(doc)});
    check(prepared->state() == EditState::Ready && !doc.project()->planLeveling, "worker mutated source");
    check(doc.applyPreparedEdit(*prepared) == EditState::Applied, "adoption commit failed");
    check(doc.calculation() && doc.calculation()->resourceLeveled &&
              doc.calculation()->inputRevision == doc.project()->revision,
          "adoption did not apply worker schedule");
    const auto adopted = calculate(*doc.project());
    check(std::none_of(adopted.diagnostics.begin(), adopted.diagnostics.end(),
                       [](const auto &d) { return d.code == "RESOURCE_NOT_CHECKED"; }),
          "selected result retained unvalidated-resource advisory");
    rejects([&] {
        doc.editGantt(doc.project()->documentId, doc.project()->revision, doc.project()->tasks[1].id, false,
                      "2026-11-05 09:00");
    });
    check(adopted.projectFinish == "2026-11-04 18:00" && adopted.tasks[1].start == "2026-11-03 09:00",
          "serial dates not adopted");
    check(doc.project()->tasks == raw.tasks && doc.project()->assignments == raw.assignments &&
              doc.project()->calendars == raw.calendars,
          "adoption changed constraints/duration/assignment");
    check(resourceLoad(*doc.project(), adopted).status == CalculationStatus::Valid &&
              hasScheduleFloat(adopted, adopted.tasks[1]),
          "selected schedule has overload/fake float");
    TaskEdit batch;
    batch.taskId = doc.project()->tasks[1].id;
    batch.edges = std::vector<Dependency>{{doc.project()->tasks[0].id, DependencyType::SS, 60}};
    doc.editTasks({batch});
    check(!doc.project()->planLeveling && isPdm(*doc.project()), "batch profile change did not release selection");
    doc.undo();
    doc.setTaskEffort(1, TaskType::FixedDuration, true, 480, 48000, {});
    check(!doc.project()->planLeveling && isEffort(*doc.project()),
          "effort profile edit failed selection invalidation");
    doc.undo();
    check(analyzeCosts(*doc.project(), oldBaseline).plan == 40, "adopted cost did not follow effective rates");
    const auto b = doc.captureBaseline("채택 계획", "PM", "자원 실행", "2026-11-03T01:00:00Z");
    check(doc.project()->planLeveling && doc.project()->baselines.back().input.planLeveling,
          "capture lost selected policy");
    verifyBaseline(doc.project()->baselines.back());
    auto usage = buildUsage(*doc.project(), {"2026-11-02", "2026-11-08", b, {}, UsagePeriod::Day});
    check(usageTotal(usageRows(usage, UsageHierarchy::Tasks)[0].values[0]).cost == 40,
          "Usage cost disagrees with adopted plan");
    doc.clearLeveling();
    check(!doc.project()->planLeveling && doc.calculation()->projectFinish == logical.projectFinish,
          "clear did not restore logical dates");
    doc.undo();
    check(doc.project()->planLeveling && calculate(*doc.project()).tasks == adopted.tasks,
          "undo did not restore adoption");
    doc.redo();
    check(!doc.project()->planLeveling, "redo did not clear adoption");
    doc.undo();
    doc.setDuration(1, 240);
    check(!doc.project()->planLeveling, "duration edit kept stale adoption");
    doc.undo();
    check(doc.project()->planLeveling && doc.project()->tasks[1].durationMinutes == 480, "edit undo lost selection");
    doc.renameTask(1, "B 이름");
    check(!doc.project()->planLeveling, "fast rename bypassed invalidation");
    doc.undo();
    doc.saveAs((temp.path() + "/selected.ppm").toStdString());
    check(nativeSchemaFor(*doc.project()) == 15 && store.load(doc.path()).project == *doc.project(),
          "schema15 decision/source/baseline roundtrip failed");
    const auto saved = doc.path();
    auto stale = prepare(doc, ClearLevelingEdit{});
    doc.open(saved);
    check(doc.applyPreparedEdit(*stale) == EditState::Stale, "same-id session accepted stale selection");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    auto canceled = prepare(doc, ClearLevelingEdit{}, control);
    check(doc.applyPreparedEdit(*canceled) == EditState::Cancelled, "cancelled selection committed");
    rejects([&] {
        auto s = selection(doc);
        s.sourceRevision--;
        doc.adoptLeveling(s);
    });
    Document ro(store);
    ro.open(saved, true);
    rejects([&] { ro.clearLeveling(); });
    doc.setStatusAt("2026-11-02 18:00");
    check(!doc.project()->planLeveling, "status date kept plan adoption");
    ProgressEntry e;
    e.taskId = doc.project()->tasks[0].id;
    e.actualStart = "2026-11-02 09:00";
    e.actualFinish = "2026-11-02 18:00";
    e.actualDurationMinutes = 480;
    e.remainingMinutes = 0;
    e.reporter = "PM";
    e.reason = "완료";
    e.recordedAt = "2026-11-03T00:00:00Z";
    doc.recordProgress(e);
    const auto facts = doc.project()->progress;
    doc.adoptLeveling(selection(doc));
    const auto remaining = calculate(*doc.project());
    check(doc.project()->remainingLeveling && remaining.isForecast && remaining.resourceLeveled &&
              remaining.tasks[0].completedActual && remaining.tasks[0].start == *e.actualStart &&
              remaining.projectFinish == "2026-11-04 18:00",
          "remaining adoption moved actuals or failed serial dates");
    check(doc.project()->progress == facts && !doc.project()->planLeveling &&
              calculatePlan(*doc.project()).projectFinish == logical.projectFinish,
          "remaining selection changed plan/facts");
    const auto activeSelection = doc.project()->remainingLeveling;
    PeriodActualSubmission actual;
    PeriodActualEntry detail;
    detail.taskId = doc.project()->tasks[0].id;
    detail.resourceId = rid;
    detail.date = "2026-11-02";
    detail.workUnitMinutes = 48000;
    detail.actualMinor = 7;
    actual.entries = {detail};
    actual.totals = {{detail.taskId, 10}};
    actual.reporter = "PM";
    actual.reason = "실제 비용 배분";
    actual.recordedAt = "2026-11-03T01:30:00Z";
    doc.recordPeriodActuals(actual);
    check(doc.project()->remainingLeveling == activeSelection && calculate(*doc.project()).tasks == remaining.tasks &&
              doc.project()->periodActuals.size() == 1,
          "dated actual facts changed selected schedule");
    doc.renameTask(1, "비교 변경");
    check(!doc.project()->remainingLeveling, "remaining adoption survived input change");
    doc.undo();
    JsonRecoveryStore recovery(temp.path() + "/recover");
    auto session = recovery.newSession();
    session->checkpoint(doc.recoverySnapshot("2026-11-03T02:00:00Z"));
    session.reset();
    auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "selection recovery invalid");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(store);
    restored.recover(claim->snapshot);
    check(restored.project()->remainingLeveling == doc.project()->remainingLeveling &&
              calculate(*restored.project()).tasks == remaining.tasks,
          "recovery lost policy/dates");
    auto unknown = *doc.project();
    unknown.levelingDecisions.clear(); // Independent malformed-capacity fixture.
    unknown.remainingLeveling.reset();
    unknown.resources[0].maxUnits.reset();
    const auto unknownPath = (temp.path() + "/unknown.ppm").toStdString();
    store.save(unknownPath, unknown, {});
    Document invalid(store);
    invalid.open(unknownPath);
    const auto before = *invalid.project();
    rejects([&] { invalid.adoptLeveling(selection(invalid)); });
    check(*invalid.project() == before, "incomplete candidate partially applied");
    auto object = json::encodeLeveling(selection(doc));
    object["surprise"] = true;
    rejects([&] { json::decodeLeveling(object); });
    for (const auto *profile : {pdmProfile, constraintProfile, effortProfile}) {
        auto p = raw;
        p.calculationProfile = profile;
        auto s = selection(doc);
        s.policy = levelingPolicyFor(p);
        p.planLeveling = s;
        const auto selected = calculatePlan(p);
        check(std::none_of(selected.diagnostics.begin(), selected.diagnostics.end(),
                           [](const auto &d) { return d.code == "PDM_POLICY"; }),
              "selected PDM result retained logical float/resource policy advisory");
        check(selected.status == CalculationStatus::Valid && selected.resourceLeveled &&
                  selected.projectFinish == "2026-11-04 18:00" &&
                  resourceLoad(p, selected).status == CalculationStatus::Valid,
              "profile-specific selection replay failed");
        p.planLeveling->policy = legacyLevelingPolicyFor(p);
        check(calculatePlan(p).tasks == selected.tasks, "legacy selection policy replay failed");
    }
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        contracts();
        std::cout << "adoption: "
                     "source/serial-dates/cost/baseline/undo/invalidation/forecast/readonly/session/cancel/schema/"
                     "recovery passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
