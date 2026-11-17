#include "application/Document.h"
#include "calculation/DatedRemainingWork.h"
#include "calculation/Usage.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include <QGuiApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <random>
using namespace jobs;
namespace {
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
    throw std::runtime_error("expected dated effort rejection");
}
void write(const QString &name, const QByteArray &value) {
    QFile file("dated-effort-qa/" + name);
    check(file.open(QIODevice::WriteOnly) && file.write(value) == value.size(), "write dated effort evidence");
}
Project fixture(TaskType type = TaskType::FixedWork, bool work = true) {
    Project p;
    p.documentId = "dated-effort";
    p.name = "날짜 고정 잔여 역산";
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "설계", 960, {}}};
    p.tasks[0].taskType = type;
    p.tasks[0].workDriven = work;
    if (work)
        p.tasks[0].workUnitMinutes = 96000;
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    r.maxUnits = 200;
    r.rates = {{"2026-11-02", 60}, {"2026-11-03", 120}};
    p.resources = {r};
    p.assignments = {{"T", "R", 100}};
    p.taskCosts = {{"T", 0}};
    p.statusAt = "2026-11-02 09:00";
    return p;
}
auto prepare(Document &d, RemainingWorkPlan plan,
             std::shared_ptr<CalculationControl> c = std::make_shared<CalculationControl>()) {
    return Document::prepareEdit(d.makeEditRequest(DatedRemainingWorkEdit{std::move(plan)}), c);
}
std::map<std::string, std::int64_t> days(const Project &p, const ScheduleResult &r) {
    std::map<std::string, std::int64_t> out;
    WorkCalendar clock(p);
    for (const auto &s : TaskExecution(p, pendingTask(p, p.tasks[0])).spans(r.tasks[0]))
        for (auto at = s.start; at < s.finish; ++at)
            out[clock.boundary(at, false).substr(0, 10)] += s.units;
    return out;
}
// A separate civil-minute ledger builds quotas and consumes only the unreserved total.
void oracleCases() {
    QJsonArray evidence;
    std::mt19937 random(20261108);
    const auto anchor = QDateTime::fromString("2026-11-02 00:00", "yyyy-MM-dd HH:mm");
    for (int sample = 0; sample < 72; ++sample) {
        auto p = fixture(sample % 2 ? TaskType::FixedWork : TaskType::FixedUnits);
        p.calendars = {{"C", "8분", {1, 2, 3, 4, 5}, {{540, 548}}, {}},
                       {"A", "부분", {1, 2, 3, 4, 5}, {{541, 547}}, {sample % 3 == 0 ? "2026-11-04" : "2026-11-05"}}};
        p.tasks[0].calendarId = "C";
        p.tasks[0].breaks = {{"2026-11-02 09:03", "2026-11-02 09:04"}};
        p.resources[0].calendarId = "A";
        auto b = p.resources[0];
        b.id = "S";
        b.calendarId = "C";
        b.name = "지원";
        p.resources.push_back(b);
        const auto units = 1 + int(random() % 180);
        p.assignments = {{"T", "R", units, {{0, 2, 0}, {2, 9, units + 70}, {9, 14, units / 2}}}, {"T", "S", 33}};
        p.statusAt = sample % 2 ? "2026-11-02 09:01" : "2026-11-02 09:00";
        const std::int64_t quota = 1 + random() % 501, total = quota + 1 + random() % 1600;
        p.tasks[0].workUnitMinutes = total;
        p.remainingWork = {{"T", "R", *p.statusAt, {{"2026-11-02", 0}, {"2026-11-03", quota}}}};
        std::map<std::pair<std::int64_t, std::string>, int> expected;
        std::vector<std::int64_t> available;
        for (int m = 541; m < 547; ++m)
            available.push_back(1440 + m);
        for (std::size_t i = 0; i < available.size(); ++i)
            expected[{available[i], "R"}] = int(quota / 6 + (i < std::size_t(quota % 6)));
        auto remaining = total - quota;
        std::int64_t relative = 0;
        const auto begin = sample % 2 ? 541 : 540;
        for (std::int64_t at = begin; at < 1440 * 30 && remaining; ++at) {
            const auto stamp = anchor.addSecs(at * 60);
            const auto date = stamp.date().toString(Qt::ISODate).toStdString();
            const auto minute = stamp.time().hour() * 60 + stamp.time().minute();
            if (stamp.date().dayOfWeek() > 5 || minute < 540 || minute >= 548 ||
                (date == "2026-11-02" && minute == 543))
                continue;
            int rate = units;
            for (const auto &v : p.assignments[0].periods)
                if (relative >= v.fromMinute && relative < v.toMinute)
                    rate = v.units;
            ++relative;
            if (date == "2026-11-02" || date == "2026-11-03" || minute < 541 || minute >= 547 ||
                date == p.calendars[1].holidays[0])
                rate = 0;
            const int sum = rate + 33;
            const auto consume = std::min(remaining, std::int64_t(sum));
            int a = int(std::int64_t(rate) * consume / sum), bUnits = int(33 * consume / sum);
            auto spare = consume - a - bUnits;
            if (spare && rate) {
                ++a;
                --spare;
            }
            if (spare)
                ++bUnits;
            if (a)
                expected[{at, "R"}] += a;
            if (bUnits)
                expected[{at, "S"}] += bUnits;
            remaining -= consume;
        }
        check(!remaining, "oracle horizon");
        std::erase_if(expected, [](const auto &v) { return v.second == 0; });
        const auto result = calculate(p);
        check(result.status == CalculationStatus::Valid, "oracle forecast conflict");
        std::map<std::pair<std::int64_t, std::string>, int> actual;
        const auto spans = TaskExecution(p, pendingTask(p, p.tasks[0])).spans(result.tasks[0]);
        QJsonArray spanJson;
        for (const auto &s : spans) {
            spanJson.append(QJsonObject{{"resource", QString::fromStdString(s.resourceId)},
                                        {"start", double(s.start)},
                                        {"finish", double(s.finish)},
                                        {"units", s.units}});
            for (auto at = s.start; at < s.finish; ++at)
                actual[{at, s.resourceId}] += s.units;
        }
        check(actual == expected, "independent dated/untargeted minute demand mismatch");
        auto finish = expected.rbegin()->first.first + 1;
        check(result.tasks[0].finish == anchor.addSecs(finish * 60).toString("yyyy-MM-dd HH:mm").toStdString(),
              "earliest feasible finish");
        evidence.append(QJsonObject{{"input", json::encodeProject(p)},
                                    {"finish", QString::fromStdString(result.tasks[0].finish)},
                                    {"spans", spanJson}});
    }
    write("oracle.json", QJsonDocument(evidence).toJson());
}
void floatCases() {
    QJsonArray evidence;
    for (int quota : {200, 300, 400}) {
        auto p = fixture();
        p.calendars = {{"C", "4분", {1, 2, 3, 4, 5}, {{540, 544}}, {}}};
        p.tasks = {{"T", "날짜 목표", 4, {}}, {"B", "후속 자원", 2, {}}, {"L", "프로젝트 끝", 12, {}}};
        for (auto &t : p.tasks)
            t.calendarId = "C";
        p.tasks[0].taskType = TaskType::FixedWork;
        p.tasks[0].workDriven = true;
        p.tasks[0].workUnitMinutes = quota + 200;
        p.resources[0].calendarId = "C";
        p.resources[0].maxUnits = 100;
        p.assignments = {{"T", "R", 100}, {"B", "R", 100}};
        p.taskCosts = {{"T", 0}, {"B", 0}, {"L", 0}};
        p.remainingWork = {{"T", "R", *p.statusAt, {{"2026-11-02", quota}}}};
        const auto leveled = levelResources(p, "task-order", 30).leveled;
        check(leveled.status == CalculationStatus::Valid && leveled.levelingFloatCalculated,
              "small effort dated float");
        int totalA = 0, freeA = 0, totalB = 0, freeB = 0;
        // Enumerate every placement in twelve calendar working minutes. The
        // first day is reserved; the remaining 200 units finish at minute six.
        for (int a = 0; a < 4; ++a)
            for (int b = 6; b <= 10; ++b) {
                const int available = 4 - a;
                bool valid = true;
                for (int at = 0; at < 12; ++at) {
                    int rate = at >= a && at < 4 ? quota / available + (at - a < quota % available) : 0;
                    if (at >= 4 && at < 6)
                        rate += 100;
                    if (at >= b && at < b + 2)
                        rate += 100;
                    valid = valid && rate <= 100;
                }
                if (valid) {
                    totalA = std::max(totalA, a);
                    totalB = std::max(totalB, b - 6);
                    if (b == 6)
                        freeA = std::max(freeA, a);
                    if (a == 0)
                        freeB = std::max(freeB, b - 6);
                }
            }
        check(totalA == 4 - quota / 100 && freeA == totalA && totalB == 4 && freeB == 4, "independent float fixture");
        check(leveled.tasks[0].totalFloatMinutes == totalA && leveled.tasks[0].freeFloatMinutes == freeA &&
                  leveled.tasks[1].totalFloatMinutes == totalB && leveled.tasks[1].freeFloatMinutes == freeB,
              "independent dated effort float maxima");
        evidence.append(
            QJsonObject{{"quota", quota}, {"totalA", totalA}, {"freeA", freeA}, {"totalB", totalB}, {"freeB", freeB}});
    }
    write("float-oracle.json", QJsonDocument(evidence).toJson());
}
void repairAssignments(JsonProjectStore &store, const QString &folder) {
    for (auto type : {TaskType::FixedDuration, TaskType::FixedWork}) {
        auto p = fixture(type, type == TaskType::FixedWork);
        auto support = p.resources[0];
        support.id = "S";
        support.name = "지원";
        p.resources.push_back(support);
        p.assignments.push_back({"T", "S", 100});
        p.remainingWork = {{"T", "R", *p.statusAt, {{"2026-11-03", 20000}}},
                           {"T", "S", *p.statusAt, {{"2026-11-03", 20000}}}};
        const auto path = (folder + "/repair-" + QString::number(int(type)) + ".ppm").toStdString();
        store.save(path, p, {});
        Document d(store);
        d.open(path);
        d.setStatusAt("2026-11-02 10:00");
        auto first = d.project()->remainingWork[0];
        first.statusAt = *d.project()->statusAt;
        d.setDatedRemainingWork(first);
        check(d.project()->remainingWork[1].statusAt == *p.statusAt &&
                  calculate(*d.project()).status == CalculationStatus::Conflict,
              "first assignment repair silently adopted stale sibling");
        auto second = d.project()->remainingWork[1];
        second.statusAt = *d.project()->statusAt;
        if (type == TaskType::FixedWork) {
            auto bad = second;
            bad.days[0].workUnitMinutes = 80000;
            const auto unchanged = *d.project();
            rejects([&] { d.setDatedRemainingWork(bad); });
            check(*d.project() == unchanged, "shared total failure mutated partially repaired task");
        }
        d.setDatedRemainingWork(second);
        check(calculate(*d.project()).status == CalculationStatus::Valid, "same-task staged assignment repair");
    }
}
void run() {
    QDir().mkpath("dated-effort-qa");
    oracleCases();
    floatCases();
    QTemporaryDir temp;
    JsonProjectStore store;
    repairAssignments(store, temp.path());
    for (auto type : {TaskType::FixedWork, TaskType::FixedUnits}) {
        auto p = fixture(type);
        p.statusAt.reset();
        const auto path = temp.filePath("work.ppm").toStdString();
        if (QFile::exists(QString::fromStdString(path)))
            QFile::remove(QString::fromStdString(path));
        store.save(path, p, {});
        Document d(store);
        d.open(path);
        d.captureBaseline("승인", "검토자", "원계획", "2026-11-08T14:00:00Z");
        d.setStatusAt("2026-11-02 18:00");
        ProgressEntry progress;
        progress.taskId = "T";
        progress.statusAt = *d.project()->statusAt;
        progress.recordedAt = "2026-11-08T14:00:00Z";
        progress.reporter = "담당";
        progress.reason = "실측";
        progress.actualStart = "2026-11-02 09:00";
        progress.actualDurationMinutes = 480;
        progress.remainingMinutes = 480;
        progress.remainingWorkUnitMinutes = 48000;
        d.recordProgress(progress);
        PeriodActualSubmission actual;
        actual.reporter = "담당";
        actual.reason = "관측 보존";
        actual.recordedAt = "2026-11-08T14:00:00Z";
        PeriodActualEntry entry;
        entry.taskId = "T";
        entry.resourceId = "R";
        entry.date = "2026-11-02";
        entry.workUnitMinutes = 123;
        actual.entries = {entry};
        d.recordPeriodActuals(actual);
        d.save();
        const auto before = *d.project();
        const auto plan = calculatePlan(before);
        RemainingWorkPlan goal{"T", "R", *before.statusAt, {{"2026-11-03", 12000}, {"2026-11-04", 0}}};
        auto ready = prepare(d, goal);
        check(ready->state() == EditState::Ready, "prepared dated effort");
        check(*d.project() == before, "preview edited document");
        check(d.applyPreparedEdit(*ready) == EditState::Applied, "dated effort apply");
        const auto after = *d.project();
        const auto result = calculate(after);
        check(result.status == CalculationStatus::Valid && result.engineVersion == "jobs-dated-effort-1.0",
              "dated effort forecast engine");
        check(result.tasks[0].finish == "2026-11-05 16:00" && result.tasks[0].start == *progress.actualStart,
              "fixed work completion / actual start");
        check(days(after, result) == std::map<std::string, std::int64_t>{{"2026-11-03", 12000}, {"2026-11-05", 36000}},
              "targets and remaining total");
        check(after.tasks == before.tasks && after.assignments == before.assignments &&
                  after.progress == before.progress && after.periodActuals == before.periodActuals &&
                  after.baselines == before.baselines,
              "original plans/facts/baseline changed");
        check(calculatePlan(after).tasks == plan.tasks && nativeSchemaFor(after) == 35, "plan unchanged / schema35");
        d.undo();
        check(d.project()->remainingWork == before.remainingWork && d.project()->tasks == before.tasks &&
                  d.project()->progress == before.progress && !d.dirty(),
              "dated effort undo savepoint");
        d.redo();
        const auto usage =
            buildUsage(*d.project(), {"2026-11-02", "2026-11-10", before.baselines[0].id, {}, UsagePeriod::Day});
        check(usage.available[0] && usage.available[1] && usage.available[2], "usage forecast/baseline availability");
        check(usageTotal(usage.leaves.back().values[2]).labor == 48000 &&
                  usageTotal(usage.leaves.back().values[2]).cost == 960,
              "remaining work and effective rate cost");
        d.save();
        check(store.load(d.path()).project == *d.project(), "save/reopen dated effort");
        rejects([&] { json::decodeProject(json::encodeProject(*d.project()), 34); });
        auto bad = goal;
        bad.days[0].workUnitMinutes = 48001;
        const auto stable = *d.project();
        rejects([&] { d.setDatedRemainingWork(bad); });
        check(*d.project() == stable, "failed total edit mutated project");
        auto cancel = std::make_shared<CalculationControl>();
        cancel->cancel();
        check(prepare(d, goal, cancel)->state() == EditState::Cancelled, "dated effort cancellation");
        auto changed = goal;
        changed.days = {{"2026-11-06", 12000}};
        auto stale = prepare(d, changed);
        check(stale->state() == EditState::Ready, "future extension preparation");
        d.renameTask(0, "수정");
        check(d.applyPreparedEdit(*stale) == EditState::Stale, "stale dated effort commit");
        d.undo();
        const auto leveled = levelResources(*d.project(), "task-order", 30).leveled;
        check(leveled.status == CalculationStatus::Valid && leveled.levelingFloatCalculated,
              "dated effort leveling/witness");
        for (const auto &w : leveled.levelingWitnesses->tasks) {
            auto witness = leveled;
            for (const auto &v : w.dates)
                for (auto &r : witness.tasks)
                    if (r.taskId == v.taskId) {
                        r.start = v.start;
                        r.finish = v.finish;
                        r.remainingStart = v.remainingStart;
                    }
            validateRemainingWorkExecution(*d.project(), witness);
            check(resourceLoad(*d.project(), witness).status == CalculationStatus::Valid,
                  "dated effort witness overload");
        }
        LevelingSelection selection;
        selection.policy = datedEffortPolicy;
        selection.adoptedAt = "2026-11-08T14:00:00Z";
        selection.reason = "검토";
        selection.sourceDocumentId = d.project()->documentId;
        selection.sourceRevision = d.project()->revision;
        selection.horizonWorkDays = 30;
        d.adoptLeveling(selection);
        check(d.project()->remainingLeveling.has_value(), "dated effort adoption");
        d.saveScenario("잔여 목표 동결", "task-order", 30, "2026-11-08T14:00:00Z", d.project()->documentId,
                       d.project()->revision);
        const auto scenario = d.project()->scenarios.back();
        check(scenario.policy == datedEffortPolicy &&
                  levelResources(scenarioProject(scenario), "task-order", 30, scenario.policy).leveled.status ==
                      CalculationStatus::Valid,
              "scenario replay");
        d.setDatedRemainingWork(changed);
        check(!d.project()->remainingLeveling, "goal change invalidates selection");
        check(d.project()->scenarios.back() == scenario, "scenario mutated");
        d.undo();
        check(d.project()->remainingLeveling.has_value(), "undo selected leveling");
        d.clearDatedRemainingWork();
        check(nativeSchemaFor(*d.project()) == 35, "archived policy lost schema35");
        d.undo();
        d.save();
        d.renameTask(0, "복구 검증");
        JsonRecoveryStore recovery(temp.path() + "/recovery-" + QString::number(int(type)));
        auto session = recovery.newSession();
        session->checkpoint(d.recoverySnapshot("2026-11-08T14:00:00Z"));
        session.reset();
        auto scan = recovery.scan();
        check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema35 recovery scan");
        auto claim = recovery.claim(scan.candidates[0]);
        Document restored(store);
        restored.recover(claim->snapshot);
        auto recovered = *d.project();
        recovered.derivedFrom = recovered.documentId;
        recovered.documentId = restored.project()->documentId;
        check(*restored.project() == recovered, "schema35 recovery replay");
        if (type == TaskType::FixedWork) {
            write("before.json", QJsonDocument(json::encodeProject(before)).toJson());
            write("after.json", QJsonDocument(json::encodeProject(after)).toJson());
            const auto report = buildReport(
                after, {{}, {}, "budget-remaining-v1", {}, {}, "dated-effort", "2026-11-08T14:00:00Z", false});
            exportXlsx(report, temp.path() + "/report.xlsx");
            PdfOptions pdf;
            pdf.font = QGuiApplication::font();
            check(exportPdf(report, temp.path() + "/report.pdf", ReportFamily::All, pdf) > 0, "dated effort PDF");
            exportXlsx(usageReport(usage, UsageHierarchy::Resources, "dated-effort", "2026-11-08T14:00:00Z", false),
                       temp.path() + "/usage.xlsx");
            for (const auto &name : {"report.xlsx", "report.pdf", "usage.xlsx"}) {
                QFile f(temp.path() + "/" + name);
                check(f.open(QIODevice::ReadOnly), "read report");
                write(name, f.readAll());
            }
            write("export.xml", exportProjectXml(after).xml);
            write("export.xer", exportPrimaveraXer(after).xer);
        }
        const auto current = d.path();
        d.open(current, true);
        rejects([&] { d.setDatedRemainingWork(goal); });
    }
    auto duration = fixture(TaskType::FixedUnits, false);
    duration.remainingWork = {{"T", "R", *duration.statusAt, {{"2026-11-02", 12000}}}};
    const auto dr = calculate(duration);
    check(dr.status == CalculationStatus::Valid && dr.tasks[0].finish == "2026-11-03 18:00" &&
              days(duration, dr)["2026-11-02"] == 12000,
          "FixedUnits duration basis");
    auto p = fixture();
    p.remainingWork = {{"T", "R", *p.statusAt, {{"2026-11-02", 96000}}}};
    check(calculate(p).status == CalculationStatus::Valid, "logical overload remains queryable");
    p.resources[0].maxUnits = 100;
    check(levelResources(p, "task-order", 30).leveled.status == CalculationStatus::Conflict,
          "overload target moved away");
    p.resources[0].maxUnits = 200;
    p.statusAt = "2026-11-03 09:00";
    check(calculate(p).status == CalculationStatus::Conflict, "stale status silently adopted");
    p = fixture();
    p.remainingWork = {{"T", "R", *p.statusAt, {{"2026-11-07", 1}}}};
    check(calculate(p).status == CalculationStatus::Conflict, "weekend positive quota");
    p = fixture();
    p.remainingWork = {{"T", "R", *p.statusAt, {{"2026-11-10", 0}}}};
    check(calculate(p).status == CalculationStatus::Conflict, "zero goal prolonged completion");
    p = fixture();
    p.assignments[0].units = 100;
    p.remainingWork = {{"T", "R", *p.statusAt, {{"2026-11-03", 96000}}}};
    check(calculate(p).status == CalculationStatus::Valid && days(p, calculate(p))["2026-11-03"] == 96000,
          "fully specified total leaves no inherited work");
    p.remainingWork[0].days[0].workUnitMinutes = 96001;
    check(calculate(p).status == CalculationStatus::Conflict, "cross target total overflow");
    p = fixture();
    auto s = p.resources[0];
    s.id = "S";
    p.resources.push_back(s);
    p.assignments.push_back({"T", "S", 100});
    p.remainingWork = {{"T", "R", *p.statusAt, {{"2026-11-02", 60000}}},
                       {"T", "S", *p.statusAt, {{"2026-11-03", 60000}}}};
    check(calculate(p).status == CalculationStatus::Conflict, "multi-resource goals exceed task total");
    std::cout << "PASS dated effort: 72 independent civil-minute cases, exact work/targets, FixedWork/FixedUnits "
                 "duration/work basis, calendars/curves, original facts, float/leveling/witness/adoption/scenario, "
                 "schema35/recovery/undo, worker protections and exports\n";
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    // Use the installed face as the desktop does. Re-registering the same font
    // through the native platform can make Qt emit outlines instead of PDF text.
    app.setFont(QFont("Malgun Gothic", 9));
#endif
    try {
        run();
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
