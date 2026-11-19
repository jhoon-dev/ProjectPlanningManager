#include "application/Document.h"
#include "calculation/Effort.h"
#include "persistence/JsonProjectStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <future>
#include <iostream>

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
auto prepare(ppm::Document &doc, ppm::EditCommand command,
             std::shared_ptr<ppm::CalculationControl> control = std::make_shared<ppm::CalculationControl>()) {
    return std::async(std::launch::async,
                      [request = doc.makeEditRequest(std::move(command)), control]() mutable {
                          return ppm::Document::prepareEdit(std::move(request), control);
                      })
        .get();
}
ppm::Project fixture(bool predecessorFirst, bool addEdge) {
    ppm::Project p;
    p.documentId = "batch-inverse";
    p.name = "Shared inverse proposal";
    p.startDate = "2026-10-26";
    p.calculationProfile = ppm::effortProfile;
    ppm::Task a{"A", "A", 480, {}},
        b{"B", "B", 480, addEdge ? std::vector<std::string>{} : std::vector<std::string>{"A"}};
    for (auto *task : {&a, &b}) {
        task->taskType = ppm::TaskType::FixedWork;
        task->workDriven = true;
        task->workUnitMinutes = 48000;
    }
    b.breaks = {{"2026-10-27 11:00", "2026-10-27 14:00"}};
    p.tasks = predecessorFirst ? std::vector{a, b} : std::vector{b, a};
    p.calendars = {{"short", "Four hours", {1, 2, 3, 4, 5}, {{540, 840}}, {}}};
    p.resources = {{"RA", "RA", "labor", 100, {}}, {"RB", "RB", "labor", 100, {}}};
    p.resources[1].calendarId = "short";
    p.assignments = {{"A", "RA", 100}, {"B", "RB", 100}};
    return p;
}
void inverseBatch(ppm::JsonProjectStore &store, const std::string &path, bool predecessorFirst, bool reverseCommands,
                  bool addEdge, bool worker) {
    store.save(path, fixture(predecessorFirst, addEdge), {});
    ppm::Document doc(store);
    doc.open(path);
    doc.captureBaseline("B", "PM", "Approved", "2026-10-30T00:00:00Z");
    doc.save();
    const auto source = *doc.project();
    ppm::TaskEdit a{"A", {}, {}, {}}, b{"B", {}, {}, {}};
    a.duration.emplace(960);
    b.duration.emplace(240);
    if (addEdge)
        b.edges = std::vector<ppm::Dependency>{{"A", ppm::DependencyType::FS, 0}};
    const auto changes = reverseCommands ? std::vector{b, a} : std::vector{a, b};
    if (worker) {
        auto prepared = prepare(doc, ppm::BatchTaskEdit{changes});
        check(prepared->state() == ppm::EditState::Ready && *doc.project() == source,
              "batch preparation failed or modified live input");
        check(doc.applyPreparedEdit(*prepared) == ppm::EditState::Applied, "batch proposal not applied");
    } else
        doc.editTasks(changes);
    const auto &p = *doc.project();
    const auto plan = ppm::calculatePlan(p);
    check(plan.status == ppm::CalculationStatus::Valid && p.revision == source.revision + 1 &&
              p.assignments[0].units == 50 && p.assignments[1].units == 200 && p.baselines == source.baselines,
          "row/command order changed inverse assignments or immutable baseline");
    for (const auto &task : p.tasks) {
        check(task.taskType == ppm::TaskType::FixedWork && task.workUnitMinutes == 48000 && task.workDriven,
              "temporary duration type escaped into the document");
        const auto row =
            std::find_if(plan.tasks.begin(), plan.tasks.end(), [&](const auto &r) { return r.taskId == task.id; });
        ppm::TaskExecution model(p, task);
        check(row != plan.tasks.end() && model.work(*row) == 48000 &&
                  model.duration(*row) == (task.id == "A" ? 960 : 240),
              "batch inverse failed to preserve effort or requested duration");
        if (task.id == "B")
            check(row->finish == "2026-10-28 14:00", "successor used the predecessor's old duration");
    }
    const auto edited = static_cast<const ppm::PlanInput &>(p);
    doc.undo();
    check(static_cast<const ppm::PlanInput &>(*doc.project()) == static_cast<const ppm::PlanInput &>(source) &&
              !doc.dirty(),
          "batch undo was not one atomic step");
    const auto undone = *doc.project();
    const auto redo = doc.redoLabel();
    auto invalid = changes;
    for (auto &change : invalid)
        if (change.taskId == "B")
            change.duration.emplace(0);
    auto failed = prepare(doc, ppm::BatchTaskEdit{invalid});
    check(failed->state() == ppm::EditState::Failed && failed->failure() &&
              failed->failure()->code == "TASK_EDIT_FAILED" &&
              doc.applyPreparedEdit(*failed) == ppm::EditState::Failed && *doc.project() == undone &&
              doc.redoLabel() == redo && !doc.dirty() && store.load(path).project == source,
          "invalid batch changed document, redo or saved bytes");
    auto control = std::make_shared<ppm::CalculationControl>();
    auto cancelled = prepare(doc, ppm::BatchTaskEdit{changes}, control);
    control->cancel();
    check(doc.applyPreparedEdit(*cancelled) == ppm::EditState::Cancelled && *doc.project() == undone &&
              doc.redoLabel() == redo,
          "cancelled batch consumed redo or modified input");
    doc.redo();
    check(static_cast<const ppm::PlanInput &>(*doc.project()) == edited, "batch redo lost common-plan assignments");
}
void notStarted(ppm::JsonProjectStore &store, const std::string &path, ppm::TaskType type) {
    ppm::Document doc(store);
    doc.create("Not started", "2026-10-26", {"A"});
    const auto id = doc.project()->tasks[0].id;
    const auto resource = doc.putResource({{}, "R", "labor", 100, {}});
    doc.setAssignments(id, {{id, resource, 100}});
    doc.setTaskEffort(0, type, true, 480, 48000, {});
    doc.setStatusAt("2026-10-27 09:00");
    ppm::ProgressEntry entry;
    entry.taskId = id;
    entry.remainingWorkUnitMinutes = 48000;
    entry.reporter = "PM";
    entry.reason = "All effort remains";
    entry.recordedAt = "2026-10-30T00:00:00Z";
    const auto source = *doc.project();
    auto prepared = prepare(doc, ppm::ProgressEdit{entry});
    check(prepared->state() == ppm::EditState::Ready && *doc.project() == source &&
              doc.applyPreparedEdit(*prepared) == ppm::EditState::Applied,
          "not-started work report requires fabricated actual dates or remaining duration");
    const auto first = doc.project()->progress.front();
    check(!first.actualStart && !first.actualFinish && first.remainingMinutes == 0 &&
              first.planDurationMinutes == 480 && first.remainingWorkUnitMinutes == 48000,
          "not-started report invented duration or actual dates");
    auto forecast = ppm::calculate(*doc.project());
    check(forecast.status == ppm::CalculationStatus::Valid && forecast.tasks[0].start == "2026-10-27 09:00" &&
              forecast.tasks[0].finish == "2026-10-27 18:00",
          "not-started effort forecast failed");
    doc.saveAs(path);
    const auto reported = *doc.project();
    for (const auto remaining :
         {std::optional<std::int64_t>{0}, std::optional<std::int64_t>{-1}, std::optional<std::int64_t>{}}) {
        entry.remainingWorkUnitMinutes = remaining;
        auto invalid = prepare(doc, ppm::ProgressEdit{entry});
        check(invalid->state() == ppm::EditState::Failed && *doc.project() == reported && !doc.dirty(),
              "missing/negative/zero remaining work bypassed completion evidence");
    }
    doc.open(path);
    check(*doc.project() == reported, "not-started report did not roundtrip");
    doc.setStatusAt("2026-10-28 09:00");
    entry.remainingWorkUnitMinutes = 36000;
    doc.recordProgress(entry);
    check(doc.project()->progress.front() == first && doc.project()->progress.back().previousEntryId == first.id,
          "not-started correction changed immutable history");
    forecast = ppm::calculate(*doc.project());
    check(forecast.status == ppm::CalculationStatus::Valid && forecast.tasks[0].finish == "2026-10-28 16:00",
          "corrected remaining work was not used");
    doc.setTaskEffort(0, ppm::TaskType::FixedDuration, false, 480, {}, {});
    check(doc.project()->progress.front() == first &&
              ppm::calculate(*doc.project()).status == ppm::CalculationStatus::Incomplete,
          "type conversion invalidated historical work reports or invented remaining duration");
    entry.remainingWorkUnitMinutes.reset();
    auto invalidDuration = prepare(doc, ppm::ProgressEdit{entry});
    check(invalidDuration->state() == ppm::EditState::Failed, "duration-based completion evidence check weakened");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        ppm::JsonProjectStore store;
        int n = 0;
        for (bool predecessorFirst : {false, true})
            for (bool reverseCommands : {false, true})
                for (bool addEdge : {false, true})
                    for (bool worker : {false, true})
                        inverseBatch(store, directory.filePath(QString::number(n++) + ".ppm").toStdString(),
                                     predecessorFirst, reverseCommands, addEdge, worker);
        for (const auto type : {ppm::TaskType::FixedWork, ppm::TaskType::FixedUnits})
            notStarted(store, directory.filePath(QString::number(n++) + ".ppm").toStdString(), type);
        std::cout
            << "PASS: common batch inverse plan across row/command/edge permutations, atomic undo/failure/cancel, "
               "not-started work reports, persistence and unchanged completion evidence\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
