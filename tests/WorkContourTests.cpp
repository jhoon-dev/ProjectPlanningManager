#include "application/Document.h"
#include "calculation/Effort.h"
#include "calculation/LevelingFloat.h"
#include "calculation/Usage.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "reporting/ReportExport.h"
#include <QGuiApplication>
#include <QFontDatabase>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <future>
#include <iostream>
#include <random>
namespace {
using namespace jobs;
void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected contour rejection");
}
Project fixture(TaskType type = TaskType::FixedWork) {
    Project p;
    p.documentId = "work-contour";
    p.name = "고정 공수·단위 곡선 검증";
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "설계", 480, {}}};
    auto &t = p.tasks[0];
    t.taskType = type;
    t.workDriven = true;
    t.workUnitMinutes = 48000;
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    r.maxUnits = 200;
    r.rates = {{"2026-11-02", 60}, {"2026-11-03", 120}};
    p.resources = {r};
    p.assignments = {{"T", "R", 100, {{0, 240, 50}, {240, 480, 150}, {1500, 1700, 7}}}};
    p.taskCosts = {{"T", 0}};
    return p;
}
auto prepared(Document &d, EditCommand edit,
              std::shared_ptr<CalculationControl> c = std::make_shared<CalculationControl>()) {
    return std::async(std::launch::async, [request = d.makeEditRequest(std::move(edit)),
                                           c]() mutable { return Document::prepareEdit(std::move(request), c); })
        .get();
}
void minuteCases() {
    std::mt19937 random(20261106);
    QJsonArray cases;
    const auto anchor = QDateTime::fromString("2026-11-02 00:00", "yyyy-MM-dd HH:mm");
    for (int sample = 0; sample < 80; ++sample) {
        auto p = fixture(sample % 2 ? TaskType::FixedWork : TaskType::FixedUnits);
        p.calendars = {
            {"task", "짧은 달력", {1, 2, 3, 4, 5}, {{540, 548}}, {}},
            {"res", "부분 달력", {1, 2, 3, 4, 5}, {{542, 547}}, {sample % 3 == 0 ? "2026-11-03" : "2026-11-05"}}};
        p.tasks[0].calendarId = "task";
        p.resources[0].calendarId = "res";
        p.tasks[0].breaks = {{"2026-11-02 09:03", "2026-11-02 09:04"}};
        auto &a = p.assignments[0];
        a.units = 1 + int(random() % 99);
        a.periods = {{0, 2, int(random() % 200)}, {2, 6, int(random() % 300)}, {6, 12, int(random() % 150)}};
        const int target = 1 + int(random() % 2000);
        p.tasks[0].workUnitMinutes = target;
        // Independent civil-minute execution, with explicit calendars/holiday/break tests.
        std::int64_t work = 0, offset = 0, finish = -1;
        std::map<std::int64_t, int> expected;
        for (std::int64_t at = 540; at < 1440 * 50; ++at) {
            const auto stamp = anchor.addSecs(at * 60);
            const auto date = stamp.date().toString(Qt::ISODate).toStdString();
            const int minute = stamp.time().hour() * 60 + stamp.time().minute();
            const bool task = stamp.date().dayOfWeek() <= 5 && minute >= 540 && minute < 548 &&
                              !(date == "2026-11-02" && minute == 543);
            if (!task)
                continue;
            int rate = a.units;
            for (const auto &v : a.periods)
                if (offset >= v.fromMinute && offset < v.toMinute)
                    rate = v.units;
            ++offset;
            if (minute < 542 || minute >= 547 || date == p.calendars[1].holidays[0])
                continue;
            const auto consumed = int(std::min(std::int64_t(rate), std::int64_t(target) - work));
            if (consumed)
                expected[at] = consumed;
            work += consumed;
            if (work == target) {
                finish = at + 1;
                break;
            }
        }
        check(finish > 0, "oracle horizon");
        TaskExecution model(p, p.tasks[0]);
        const auto row = model.dates(0);
        check(QString::fromStdString(row.finish) == anchor.addSecs(finish * 60).toString("yyyy-MM-dd HH:mm"),
              "civil oracle finish");
        std::map<std::int64_t, int> actual;
        for (const auto &s : model.spans(row))
            for (auto at = s.start; at < s.finish; ++at)
                actual[at] += s.units;
        check(actual == expected && model.work(row) == target, "civil oracle demand");
        cases.append(QJsonObject{{"sample", sample},
                                 {"workUnitMinutes", target},
                                 {"finish", QString::fromStdString(row.finish)},
                                 {"integerWork", double(model.work(row))},
                                 {"input", json::encodeProject(p)}});
    }
    QDir().mkpath(QDir::currentPath() + "/work-contour-qa");
    QFile file(QDir::currentPath() + "/work-contour-qa/minute-oracle.json");
    check(file.open(QIODevice::WriteOnly), "oracle output");
    file.write(QJsonDocument(cases).toJson());
}
void nonmonotone() {
    auto p = fixture();
    p.calendars = {{"task", "8분", {1, 2, 3, 4, 5}, {{540, 548}}, {}},
                   {"res", "뒤 4분", {1, 2, 3, 4, 5}, {{544, 548}}, {}}};
    p.tasks[0].calendarId = "task";
    p.tasks[0].workUnitMinutes = 3;
    p.resources[0].calendarId = "res";
    p.assignments[0].units = 1;
    p.assignments[0].periods = {{0, 2, 0}, {2, 4, 300}, {4, 100, 1}};
    TaskExecution model(p, p.tasks[0]);
    check(model.dates(0).finish == "2026-11-02 09:07" && model.dates(1).finish == "2026-11-02 09:05",
          "nonmonotone fixture");
    p.tasks.push_back({"L", "끝", 8, {}});
    p.tasks[1].calendarId = "task";
    p.taskCosts.push_back({"L", 0});
    const auto plan = calculatePlan(p);
    check(plan.status == CalculationStatus::Valid && plan.logicalFloatCalculated &&
              plan.tasks[0].totalFloatMinutes == 5 && plan.tasks[0].freeFloatMinutes == 5,
          "nonmonotone exact logical float");
    p.tasks[0].constraintType = ConstraintType::FNET;
    p.tasks[0].constraintAt = "2026-11-02 09:06";
    p.tasks.push_back({"P", "선행", 0, {}});
    p.tasks.back().calendarId = "task";
    p.taskCosts.push_back({"P", 0});
    p.tasks[0].predecessors = {"P"};
    p.tasks[0].dependencyRules = {{"P", DependencyType::SS, 1}};
    auto constrained = calculatePlan(p);
    check(constrained.status == CalculationStatus::Valid && constrained.tasks[0].start == "2026-11-02 09:03" &&
              constrained.tasks[0].finish == "2026-11-02 09:06",
          "nonmonotone lower-bound intersection");
    p.tasks[0].constraintType = ConstraintType::ASAP;
    p.tasks[0].constraintAt.reset();
    p.tasks[0].predecessors.clear();
    p.tasks[0].dependencyRules.clear();
    const auto level = levelResources(p, "task-order", 30);
    check(level.leveled.status == CalculationStatus::Valid && level.leveled.levelingFloatCalculated,
          "curved resource float");
}
void contracts() {
    QTemporaryDir tmp;
    JsonProjectStore store;
    {
        auto multi = fixture();
        auto resource = multi.resources[0];
        resource.id = "R2";
        multi.resources.push_back(resource);
        multi.assignments.push_back({"T", "R2", 100, {{0, 240, 25}, {240, 480, 75}}});
        multi.tasks[0].workUnitMinutes = 72000;
        const auto path = tmp.filePath("multi.ppm").toStdString();
        store.save(path, multi, {});
        Document edit(store);
        edit.open(path);
        edit.setDuration(0, 960);
        const auto plan = calculatePlan(*edit.project());
        check(plan.status == CalculationStatus::Valid &&
                  TaskExecution(*edit.project(), edit.project()->tasks[0]).work(plan.tasks[0]) == 72000,
              "multi-resource exact inverse");
        check(edit.project()->assignments[0].units == 100 && edit.project()->assignments[1].units == 100,
              "inverse changed outside base rates");
        edit.setTaskEffort(0, TaskType::FixedWork, true, 960, 1, {});
        const auto row = calculatePlan(*edit.project()).tasks[0];
        const auto spans = TaskExecution(*edit.project(), edit.project()->tasks[0]).spans(row);
        check(spans.size() == 1 && spans[0].resourceId == "R" && spans[0].units == 1,
              "last-minute stable resource allocation");
    }
    {
        const auto path = tmp.filePath("adoption.ppm").toStdString();
        store.save(path, fixture(), {});
        Document selected(store);
        selected.open(path);
        LevelingSelection choice;
        choice.policy = levelingPolicyFor(*selected.project());
        choice.priority = "task-order";
        choice.horizonWorkDays = 30;
        choice.reason = "곡선 검산";
        choice.adoptedAt = "2026-11-06T01:00:00Z";
        choice.sourceDocumentId = selected.project()->documentId;
        choice.sourceRevision = selected.project()->revision;
        selected.adoptLeveling(choice);
        check(calculatePlan(*selected.project()).engineVersion == "jobs-adopted-work-periods-1.0",
              "adopted work engine");
        selected.captureBaseline("채택 곡선", "검토자", "동결", "2026-11-06T01:00:00Z");
        verifyBaseline(selected.project()->baselines.back());
        const auto archive = selected.project()->baselines;
        selected.setDuration(0, 960);
        check(!selected.project()->planLeveling && selected.project()->baselines == archive &&
                  !selected.project()->levelingDecisions.empty(),
              "curve inverse adoption invalidation");
        selected.saveScenario("역산 시나리오", "task-order", 30, "2026-11-06T01:00:00Z", selected.project()->documentId,
                              selected.project()->revision);
        check(levelResources(scenarioProject(selected.project()->scenarios.back()), "task-order", 30,
                             selected.project()->scenarios.back().policy)
                      .leveled.status == CalculationStatus::Valid,
              "work contour scenario replay");
        check(json::decodeProject(json::encodeProject(*selected.project()), 24) == *selected.project(),
              "frozen/adopted schema24 replay");
    }
    for (bool reverseRows : {false, true})
        for (bool reverseChanges : {false, true}) {
            auto batch = fixture();
            batch.tasks[0].workUnitMinutes = 6000;
            auto predecessor = batch.tasks[0];
            predecessor.id = "B";
            predecessor.name = "아래쪽 선행";
            batch.tasks[0].predecessors = {"B"};
            batch.tasks.push_back(predecessor);
            if (reverseRows)
                std::reverse(batch.tasks.begin(), batch.tasks.end());
            auto resource = batch.resources[0];
            resource.id = "RB";
            batch.resources.push_back(resource);
            batch.assignments.push_back({"B", "RB", 100, batch.assignments[0].periods});
            batch.taskCosts.push_back({"B", 0});
            const auto batchPath =
                tmp.filePath(QString("batch-%1-%2.ppm").arg(reverseRows).arg(reverseChanges)).toStdString();
            store.save(batchPath, batch, {});
            Document edit(store);
            edit.open(batchPath);
            TaskEdit t{"T", {}, {}, {}};
            t.duration.emplace(70);
            TaskEdit b{"B", {}, {}, {}};
            b.duration.emplace(120);
            const auto proposal = prepared(edit, BatchTaskEdit{reverseChanges ? std::vector{b, t} : std::vector{t, b}});
            check(proposal->state() == EditState::Ready && edit.project()->assignments == batch.assignments,
                  "curved batch source");
            check(edit.applyPreparedEdit(*proposal) == EditState::Applied, "curved batch apply");
            const auto final = calculatePlan(*edit.project());
            check(final.status == CalculationStatus::Valid, "curved batch final plan");
            for (const auto &task : edit.project()->tasks) {
                auto row = std::find_if(final.tasks.begin(), final.tasks.end(),
                                        [&](const auto &v) { return v.taskId == task.id; });
                TaskExecution model(*edit.project(), task);
                check(model.work(*row) == 6000 && model.duration(*row) == (task.id == "T" ? 70 : 120),
                      "batch order changed exact inverse");
            }
            edit.undo();
            check(edit.project()->assignments == batch.assignments, "batch inverse undo");
        }
    auto p = fixture();
    const auto original = p;
    TaskSchedule completed;
    completed.taskId = "T";
    completed.start = "2026-10-01 09:00";
    completed.finish = "2026-10-02 18:00";
    completed.completedActual = true;
    check(TaskExecution(p, p.tasks[0]).spans(completed).empty(), "completed work produced future demand");
    auto row = calculatePlan(p);
    check(row.status == CalculationStatus::Valid && row.tasks[0].finish == "2026-11-02 18:00" &&
              row.engineVersion == "jobs-effort-work-periods-1.0",
          "work contour plan");
    check(TaskExecution(p, p.tasks[0]).work(row.tasks[0]) == 48000, "planned work");
    check(nativeSchemaFor(p) == 24 && json::decodeProject(json::encodeProject(p), 24) == p, "schema24");
    rejects([&] { json::decodeProject(json::encodeProject(p), 23); });
    const auto path = tmp.filePath("work.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    d.captureBaseline("역산 전", "검토자", "동결", "2026-11-06T01:00:00Z");
    const auto frozen = d.project()->baselines;
    d.setDuration(0, 960);
    const auto after = calculatePlan(*d.project());
    check(after.status == CalculationStatus::Valid && after.tasks[0].finish == "2026-11-03 18:00",
          "fixed work duration inverse");
    check(d.project()->tasks[0].workUnitMinutes == 48000 &&
              TaskExecution(*d.project(), d.project()->tasks[0]).work(after.tasks[0]) == 48000,
          "fixed work not conserved");
    check(d.project()->assignments[0].periods.back() == original.assignments[0].periods.back() &&
              d.project()->baselines == frozen,
          "outside contour/archive changed");
    d.undo();
    check(d.project()->assignments == original.assignments, "inverse undo");
    d.redo();
    d.setTaskEffort(0, TaskType::FixedWork, true, 960, 1, {});
    check(TaskExecution(*d.project(), d.project()->tasks[0]).work(calculatePlan(*d.project()).tasks[0]) == 1,
          "single quantum");
    d.setDuration(0, 480);
    auto tiny = calculatePlan(*d.project());
    check(tiny.tasks[0].finish == "2026-11-02 18:00", "tiny work duration inverse");
    d.setTaskEffort(0, TaskType::FixedWork, true, 480, 0, {});
    d.setDuration(0, 0);
    const auto zero = calculatePlan(*d.project());
    check(zero.status == CalculationStatus::Valid && zero.tasks[0].start == zero.tasks[0].finish,
          "zero-work contour milestone");
    auto fu = fixture(TaskType::FixedUnits);
    fu.tasks[0].workDriven = false;
    fu.tasks[0].workUnitMinutes.reset();
    const auto unitPath = tmp.filePath("units.ppm").toStdString();
    store.save(unitPath, fu, {});
    Document units(store);
    units.open(unitPath);
    const auto rates = units.project()->assignments;
    units.setDuration(0, 960);
    auto duration = calculatePlan(*units.project());
    check(units.project()->assignments == rates &&
              TaskExecution(*units.project(), units.project()->tasks[0]).work(duration.tasks[0]) == 96000,
          "fixed units duration changed rates");
    units.setTaskEffort(0, TaskType::FixedUnits, true, 960, 24000, {});
    auto work = calculatePlan(*units.project());
    check(work.tasks[0].finish == "2026-11-02 15:20" && units.project()->assignments == rates,
          "fixed units work inverse");
    units.setDuration(0, 480);
    const auto workBefore = 48000;
    units.setAssignmentPeriods("T", "R", {{0, 480, 50}});
    auto curved = calculatePlan(*units.project());
    check(units.project()->tasks[0].workUnitMinutes == workBefore &&
              TaskExecution(*units.project(), units.project()->tasks[0]).work(curved.tasks[0]) == workBefore,
          "fixed units curve edit lost prior work");
    const auto source = units.snapshot();
    auto ready = prepared(units, DurationEdit{"T", 240});
    check(ready->state() == EditState::Ready && units.snapshot() == source, "worker mutated source");
    units.renameTask(0, "새 이름");
    check(units.applyPreparedEdit(*ready) == EditState::Stale, "stale inverse applied");
    auto c = std::make_shared<CalculationControl>();
    c->cancel();
    check(prepared(units, DurationEdit{"T", 240}, c)->state() == EditState::Cancelled, "cancel");
    auto residual = fixture();
    residual.statusAt = "2026-11-02 18:00";
    ProgressEntry fact;
    fact.id = "P";
    fact.taskId = "T";
    fact.statusAt = *residual.statusAt;
    fact.recordedAt = "2026-11-02T10:00:00Z";
    fact.actualStart = "2026-11-02 09:00";
    fact.actualDurationMinutes = 480;
    fact.remainingMinutes = 480;
    fact.remainingWorkUnitMinutes = 24000;
    fact.reporter = "검토자";
    fact.reason = "보고";
    fact.recordedRevision = 1;
    fact.sourceDocumentId = residual.documentId;
    fact.planDurationMinutes = 480;
    residual.progress = {fact};
    auto forecast = calculate(residual);
    check(forecast.status == CalculationStatus::Valid && forecast.forecastFloatCalculated, "work contour forecast");
    check(TaskExecution(residual, pendingTask(residual, residual.tasks[0])).work(forecast.tasks[0]) == 24000 &&
              residual.progress == std::vector<ProgressEntry>{fact},
          "remaining work/actuals");
    auto bad = fixture();
    bad.resources[0].calendarId = "am";
    bad.calendars = {{"am", "오전", {1, 2, 3, 4, 5}, {{540, 720}}, {}}};
    store.save(tmp.filePath("bad.ppm").toStdString(), bad, {});
    Document impossible(store);
    impossible.open(tmp.filePath("bad.ppm").toStdString());
    const auto unchanged = impossible.snapshot();
    rejects([&] { impossible.setDuration(0, 480); });
    check(impossible.snapshot() == unchanged, "failed inverse partially applied");
    const auto out = QDir::currentPath() + "/work-contour-qa";
    QDir().mkpath(out);
    for (const auto &name : {"work.ppm", "work.xlsx", "work.pdf", "usage.xlsx"})
        QFile::remove(out + "/" + name);
    const auto artifactPath = tmp.filePath("artifact.ppm").toStdString();
    store.save(artifactPath, fixture(), {});
    Document artifact(store);
    artifact.open(artifactPath);
    artifact.setDuration(0, 960);
    artifact.saveAs((out + "/work.ppm").toStdString());
    auto report = buildReport(*artifact.project(),
                              {{}, {}, "budget-remaining-v1", {}, {}, "work-contour", "2026-11-06T01:00:00Z", false});
    exportXlsx(report, out + "/work.xlsx");
    check(exportPdf(report, out + "/work.pdf", ReportFamily::All) > 0, "PDF");
    exportXlsx(usageReport(buildUsage(*artifact.project(), {"2026-11-02", "2026-11-04", {}, {}, UsagePeriod::Day}),
                           UsageHierarchy::Resources, "work-contour", "2026-11-06T01:00:00Z", false),
               out + "/usage.xlsx");
    artifact.renameTask(0, "복구 검증");
    JsonRecoveryStore recovery(tmp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(artifact.recoverySnapshot("2026-11-06T01:00:00Z"));
    session.reset();
    auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema24 recovery");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(store);
    restored.recover(claim->snapshot);
    check(restored.project()->assignments == artifact.project()->assignments &&
              restored.project()->tasks == artifact.project()->tasks,
          "recover work contour");
    units.open(unitPath, true);
    rejects([&] { units.setAssignmentPeriods("T", "R", {{0, 480, 200}}); });
    std::cout << "PASS work contours: independent80 minute ledgers, nonmonotone bounds/float, FW/FU inverses, exact "
                 "tiny work, calendars, preserved tails/facts/archives, schema24/recovery, workers/history, reports\n";
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        minuteCases();
        nonmonotone();
        contracts();
    } catch (const AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << " " << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
