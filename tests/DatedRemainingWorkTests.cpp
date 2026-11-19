#include "application/Document.h"
#include "calculation/DatedRemainingWork.h"
#include "calculation/PlannedWork.h"
#include "calculation/Usage.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "domain/Classification.h"
#include "reporting/ReportExport.h"
#include <QGuiApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <future>
#include <iostream>
namespace {
using namespace ppm;
void check(bool x, const char *s) {
    if (!x)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const AppError &) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
Project fixture() {
    Project p;
    p.documentId = "dated-remaining";
    p.name = "날짜별 잔여 검증";
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "설계", 1920, {}}};
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    r.rates = {{"2026-11-02", 60}, {"2026-11-03", 120}};
    p.resources = {r};
    p.assignments = {{"T", "R", 100}};
    p.taskCosts = {{"T", 0}};
    return p;
}
std::int64_t demand(const ResourceLoad &load, const std::string &day) {
    for (const auto &d : load.days)
        if (d.date == day && d.resourceId == "R")
            return d.demandUnitMinutes;
    return 0;
}
std::int64_t total(const Project &p) {
    auto r = calculate(p);
    check(r.status == CalculationStatus::Valid, "valid forecast");
    return TaskExecution(p, pendingTask(p, p.tasks[0])).work(r.tasks[0]);
}
// Independent minute ledger from returned spans, with a direct calendar/target check.
void minuteOracle(const Project &p, const ScheduleResult &r, const RemainingWorkPlan &goals) {
    const auto row =
        std::find_if(r.tasks.begin(), r.tasks.end(), [&](const auto &x) { return x.taskId == goals.taskId; });
    const auto t = std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &x) { return x.id == goals.taskId; });
    const auto spans = TaskExecution(p, pendingTask(p, *t)).spans(*row);
    const auto anchor = QDateTime::fromString(QString::fromStdString(p.startDate) + " 00:00", "yyyy-MM-dd HH:mm");
    for (const auto &goal : goals.days) {
        const auto day = QDate::fromString(QString::fromStdString(goal.date), "yyyy-MM-dd");
        std::int64_t work = 0;
        for (int m = 0; m < 1440; ++m) {
            const auto at = anchor.secsTo(QDateTime(day, QTime(m / 60, m % 60))) / 60;
            int rate = 0, count = 0;
            for (const auto &s : spans)
                if (s.resourceId == goals.resourceId && at >= s.start && at < s.finish) {
                    rate += s.units;
                    ++count;
                }
            check(count <= 1, "overlapping residual spans");
            const bool morning = m >= 540 && m < 720, afternoon = m >= 780 && m < 1080;
            if (!morning && !afternoon)
                check(rate == 0, "work outside default task hours");
            work += rate;
        }
        check(work == goal.workUnitMinutes, "independent minute sum");
    }
}
auto prepared(Document &d, EditCommand edit,
              std::shared_ptr<CalculationControl> c = std::make_shared<CalculationControl>()) {
    return std::async(std::launch::async, [request = d.makeEditRequest(std::move(edit)),
                                           c]() mutable { return Document::prepareEdit(std::move(request), c); })
        .get();
}
void datedFloatOracle() {
    QJsonArray observations;
    for (int goal : {100, 200, 300, 400}) {
        auto p = fixture();
        p.statusAt = "2026-11-02 08:59";
        p.calendars = {{"short", "4분 달력", {1, 2, 3, 4, 5}, {{540, 544}}, {}}};
        p.tasks = {{"A", "날짜 목표", 4, {}}, {"B", "후속 자원", 2, {}}, {"L", "프로젝트 끝", 12, {}}};
        for (auto &t : p.tasks)
            t.calendarId = "short";
        p.resources[0].calendarId = "short";
        p.assignments = {{"A", "R", 100}, {"B", "R", 100}};
        p.taskCosts = {{"A", 0}, {"B", 0}, {"L", 0}};
        p.remainingWork = {{"A", "R", *p.statusAt, {{"2026-11-02", goal}}}};
        const auto r = levelResources(p, "task-order", 30).leveled;
        check(r.status == CalculationStatus::Valid && r.levelingFloatCalculated, "small dated float unavailable");
        int totalA = 0, freeA = 0, totalB = 0, freeB = 0;
        // Independent finite working-minute ledger. No TaskExecution, inverse,
        // candidate search or capacity implementation is used to find these maxima.
        for (int a = 0; a <= 8; ++a)
            for (int b = 4; b <= 10; ++b) {
                const int count = std::max(0, 4 - a);
                if (!count)
                    continue;
                bool valid = a + 4 <= b;
                for (int m = 0; m < 12; ++m) {
                    int rate = 0;
                    if (m >= a && m < a + 4)
                        rate = m < 4 ? goal / count + (m - a < goal % count ? 1 : 0) : 100;
                    if (m >= b && m < b + 2)
                        rate += 100;
                    valid = valid && rate <= 100;
                }
                if (!valid)
                    continue;
                totalA = std::max(totalA, a);
                totalB = std::max(totalB, b - 4);
                if (b == 4)
                    freeA = std::max(freeA, a);
                if (a == 0)
                    freeB = std::max(freeB, b - 4);
            }
        check(totalA == 4 - goal / 100 && freeA == 0 && totalB == 6 && freeB == 6, "oracle fixture");
        check(r.tasks[0].totalFloatMinutes == totalA && r.tasks[0].freeFloatMinutes == freeA &&
                  r.tasks[1].totalFloatMinutes == totalB && r.tasks[1].freeFloatMinutes == freeB,
              "dated exhaustive maxima");
        check(bool(r.levelingWitnesses), "dated witnesses absent");
        for (const auto &w : r.levelingWitnesses->tasks) {
            auto trial = r;
            for (const auto &d : w.dates)
                for (auto &row : trial.tasks)
                    if (row.taskId == d.taskId) {
                        row.start = d.start;
                        row.finish = d.finish;
                        row.remainingStart = d.remainingStart;
                    }
            validateRemainingWorkExecution(p, trial);
            check(resourceLoad(p, trial).status == CalculationStatus::Valid, "dated witness overload");
            for (const auto &row : trial.tasks)
                check(row.finish <= *r.projectFinish, "dated witness horizon");
        }
        auto control = std::make_shared<CalculationControl>();
        control->cancel();
        bool cancelled = false;
        try {
            EvaluationScope scope(control);
            levelResources(p, "task-order", 30);
        } catch (const CalculationCancelled &) {
            cancelled = true;
        }
        check(cancelled, "dated float cancellation");
        observations.append(QJsonObject{{"workUnitMinutes", goal},
                                        {"totalA", totalA},
                                        {"freeA", freeA},
                                        {"totalB", totalB},
                                        {"freeB", freeB},
                                        {"witnesses", int(r.levelingWitnesses->tasks.size())},
                                        {"policy", QString::fromStdString(r.calculationPolicy)}});
    }
    QDir().mkpath(QDir::currentPath() + "/dated-udf-qa");
    QFile evidence(QDir::currentPath() + "/dated-udf-qa/dated-float-oracle.json");
    check(evidence.open(QIODevice::WriteOnly), "oracle evidence");
    evidence.write(QJsonDocument(observations).toJson());
}
void run() {
    datedFloatOracle();
    QTemporaryDir temp;
    JsonProjectStore store;
    auto p = fixture();
    const auto path = temp.filePath("dated.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    d.captureBaseline("승인", "PM", "원계획", "2026-11-06T01:00:00Z");
    d.setStatusAt("2026-11-02 18:00");
    ProgressEntry e;
    e.taskId = "T";
    e.statusAt = *d.project()->statusAt;
    e.recordedAt = "2026-11-06T01:00:00Z";
    e.reporter = "담당";
    e.reason = "관측";
    e.actualStart = "2026-11-02 09:00";
    e.actualDurationMinutes = 480;
    e.remainingMinutes = 1440;
    d.recordProgress(e);
    PeriodActualSubmission actual;
    actual.reporter = "담당";
    actual.reason = "실제";
    actual.recordedAt = "2026-11-06T01:00:00Z";
    PeriodActualEntry fact;
    fact.taskId = "T";
    fact.resourceId = "R";
    fact.date = "2026-11-02";
    fact.workUnitMinutes = 6001;
    actual.entries = {fact};
    d.recordPeriodActuals(actual);
    d.save();
    const auto before = *d.project();
    const auto plan = calculatePlan(before);
    const auto baseline = before.baselines;
    RemainingWorkPlan target{
        "T", "R", *before.statusAt, {{"2026-11-03", 12000}, {"2026-11-04", 0}, {"2026-11-05", 60000}}};
    auto result = prepared(d, DatedRemainingWorkEdit{target});
    check(result->state() == EditState::Ready && *d.project() == before, "preview changed live input");
    check(d.applyPreparedEdit(*result) == EditState::Applied, "apply");
    check(d.project()->revision == before.revision + 1 && nativeSchemaFor(*d.project()) == 22, "one revision/schema22");
    check(d.project()->assignments == before.assignments && d.project()->progress == before.progress &&
              d.project()->periodActuals == before.periodActuals && d.project()->baselines == baseline,
          "input/facts/archive preservation");
    check(calculatePlan(*d.project()).tasks == plan.tasks && analyzeCosts(*d.project(), "").plan == 3360,
          "plan dates/budget changed");
    const auto schedule = calculate(*d.project());
    check(schedule.status == CalculationStatus::Valid && schedule.isForecast &&
              schedule.engineVersion == "ppm-dated-remaining-1.0",
          "dated forecast engine");
    minuteOracle(*d.project(), schedule, target);
    const auto load = resourceLoad(*d.project(), schedule);
    check(demand(load, "2026-11-03") == 12000 && demand(load, "2026-11-04") == 0 && demand(load, "2026-11-05") == 60000,
          "daily remaining totals");
    check(total(*d.project()) == 72000, "remaining total");
    const auto usage = buildUsage(*d.project(), {"2026-11-02", "2026-11-09", {}, {}, UsagePeriod::Day});
    check(usage.available[0] && usage.available[2] && usageTotal(usage.leaves.back().values[0]).labor == 192000 &&
              usageTotal(usage.leaves.back().values[2]).labor == 72000 &&
              usageTotal(usage.leaves.back().values[2]).cost == 1440,
          "usage plan/remaining work/cost");
    d.undo();
    check(!d.dirty() && d.project()->remainingWork.empty(), "undo to savepoint");
    d.redo();
    auto partial = target;
    partial.days.resize(1);
    d.setDatedRemainingWork(partial);
    check(total(*d.project()) == 108000, "blank dates inherit tail");
    auto tiny = target;
    tiny.days = {{"2026-11-03", 1}};
    d.setDatedRemainingWork(tiny);
    check(demand(resourceLoad(*d.project(), calculate(*d.project())), "2026-11-03") == 1, "single raw unit");
    auto max = target;
    max.days = {{"2026-11-03", 48000000}};
    d.setDatedRemainingWork(max);
    max.days[0].workUnitMinutes++;
    const auto stable = *d.project();
    rejects([&] { d.setDatedRemainingWork(max); });
    check(*d.project() == stable, "failed inverse partially applied");
    d.setDatedRemainingWork(target);
    for (const auto &days :
         std::vector<std::vector<DatedWorkValue>>{{{"2026-11-03", -1}},
                                                  {{"2026-11-03", 1}, {"2026-11-03", 2}},
                                                  {{"2026-11-01", 0}},
                                                  {{"2026-11-09", 1}},
                                                  {{"2026-11-99", 0}},
                                                  std::vector<DatedWorkValue>(1025, {"2026-11-03", 0})}) {
        auto bad = target;
        bad.days = days;
        rejects([&] { d.setDatedRemainingWork(bad); });
    }
    auto restricted = *d.project();
    restricted.calendars = {{"am", "오전", {1, 2, 3, 4, 5}, {{540, 720}}, {}}};
    restricted.resources[0].calendarId = "am";
    auto morning = calculate(restricted);
    check(morning.status == CalculationStatus::Valid, "morning inverse");
    minuteOracle(restricted, morning, target);
    check(resourceLoad(restricted, morning).days.back().peakUnits == 334, "integer remainder/overload");
    restricted.calendars[0].holidays = {"2026-11-05"};
    check(calculate(restricted).status == CalculationStatus::Conflict, "unavailable positive silently clipped");
    auto broken = *d.project();
    broken.tasks[0].breaks = {{"2026-11-03 10:00", "2026-11-03 11:00"}};
    auto br = calculate(broken);
    check(br.status == CalculationStatus::Valid, "break model");
    minuteOracle(broken, br, target);
    auto completed = *d.project();
    completed.progress.back().actualFinish = "2026-11-02 18:00";
    completed.progress.back().remainingMinutes = 0;
    check(calculateEffort(completed, true, "task-order", 30).status == CalculationStatus::Conflict,
          "completed goal survived direct leveling execution");
    auto advanced = *d.project();
    advanced.statusAt = "2026-11-03 18:00";
    check(calculate(advanced).status == CalculationStatus::Conflict &&
              calculatePlan(advanced).status == CalculationStatus::Valid,
          "status mismatch fallback");
    check(!buildUsage(advanced, {"2026-11-02", "2026-11-09", {}, {}, UsagePeriod::Day}).available[2],
          "invalid residual shown as zero");
    auto moved = *d.project();
    moved.tasks[0].constraintType = ConstraintType::SNET;
    moved.tasks[0].constraintAt = "2026-11-06 09:00";
    check(calculate(moved).status == CalculationStatus::Conflict, "moved date goal silently moved");
    auto shortTask = *d.project();
    shortTask.tasks[0].durationMinutes = 480;
    shortTask.progress.back().remainingMinutes = 480;
    check(calculate(shortTask).status == CalculationStatus::Conflict, "out of window goal dropped");
    auto floatCase = *d.project();
    floatCase.tasks.push_back({"LONG", "긴 작업", 2880, {}});
    floatCase.taskCosts.push_back({"LONG", 0});
    auto floating = calculate(floatCase);
    check(floating.status == CalculationStatus::Valid && floating.forecastFloatCalculated, "dated logical float");
    const auto &fr = floating.tasks[0];
    check(fr.totalFloatMinutes == 479, "fixed date float bound");
    const WorkCalendar clock(floatCase);
    TaskExecution model(floatCase, pendingTask(floatCase, floatCase.tasks[0]));
    auto witness = model.dates(model.calendar().offset(clock.boundary(fr.lateStartMinutes, false)));
    witness.remainingStart = witness.start;
    auto witnessSchedule = floating;
    witnessSchedule.tasks[0] = witness;
    minuteOracle(floatCase, witnessSchedule, target);
    auto next = model.dates(model.calendar().offset(*witness.remainingStart) + 1);
    next.remainingStart = next.start;
    rejects([&] { model.spans(next); });
    check(levelResources(*d.project(), "task-order", 30).leveled.status == CalculationStatus::Conflict,
          "infeasible fixed-date overload leveled away");
    auto feasible = target;
    feasible.days.back().workUnitMinutes = 24000;
    d.setDatedRemainingWork(feasible);
    const auto leveled = levelResources(*d.project(), "task-order", 30);
    check(leveled.leveled.status == CalculationStatus::Valid && leveled.leveled.levelingFloatCalculated &&
              leveled.leveled.levelingFloatReason.empty() &&
              leveled.leveled.calculationPolicy.ends_with("/resource-dated-float-v1"),
          "dated resource float certificate");
    LevelingSelection selection;
    selection.policy = datedRemainingPolicy;
    selection.adoptedAt = "2026-11-06T01:00:00Z";
    selection.reason = "검토";
    selection.sourceDocumentId = d.project()->documentId;
    selection.sourceRevision = d.project()->revision;
    selection.horizonWorkDays = 30;
    d.adoptLeveling(selection);
    check(d.project()->remainingLeveling.has_value(), "dated adoption");
    d.setDatedRemainingWork(target);
    check(!d.project()->remainingLeveling && !d.project()->levelingDecisions.empty(),
          "dated edit did not invalidate adoption");
    d.undo();
    check(d.project()->remainingLeveling.has_value(), "undo adoption/settings");
    d.redo();
    d.setDatedRemainingWork(feasible);
    d.saveScenario("날짜별 잔여 시나리오", "task-order", 30, "2026-11-06T01:00:00Z", d.project()->documentId,
                   d.project()->revision);
    auto savedScenario = d.project()->scenarios.back();
    check(scenarioProject(savedScenario).remainingWork == d.project()->remainingWork &&
              levelResources(scenarioProject(savedScenario), "task-order", 30, savedScenario.policy).leveled.status ==
                  CalculationStatus::Valid,
          "scenario residual replay");
    d.setDatedRemainingWork(target);
    check(json::decodeProject(json::encodeProject(*d.project()), 22) == *d.project(), "schema22 replay");
    rejects([&] { json::decodeProject(json::encodeProject(*d.project()), 21); });
    auto malformed = json::encodeProject(*d.project());
    auto plans = malformed["remainingWork"].toArray();
    auto rp = plans[0].toObject();
    auto days = rp["days"].toArray();
    auto day = days[0].toObject();
    day["workUnitMinutes"] = .5;
    days[0] = day;
    rp["days"] = days;
    plans[0] = rp;
    malformed["remainingWork"] = plans;
    rejects([&] { json::decodeProject(malformed, 22); });
    auto old = prepared(d, DatedRemainingWorkEdit{feasible});
    d.renameTask(0, "새 이름");
    check(d.applyPreparedEdit(*old) == EditState::Stale, "stale apply");
    auto cancel = std::make_shared<CalculationControl>();
    cancel->cancel();
    auto stopped = prepared(d, DatedRemainingWorkEdit{feasible}, cancel);
    check(stopped->state() == EditState::Cancelled, "cooperative cancellation");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-06T01:00:00Z"));
    session.reset();
    auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "recovery scan");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(store);
    restored.recover(claim->snapshot);
    check(restored.project()->remainingWork == d.project()->remainingWork &&
              restored.project()->periodActuals == before.periodActuals,
          "recover goals/facts");
    const auto output = QDir::currentPath() + "/professional-fields-qa";
    QDir().mkpath(output);
    for (auto name : {"remaining.ppm", "remaining.xlsx", "remaining.pdf", "remaining-usage.xlsx"})
        QFile::remove(output + "/" + name);
    d.saveAs((output + "/remaining.ppm").toStdString());
    const auto report = buildReport(
        *d.project(), {{}, {}, "budget-remaining-v1", {}, {}, "dated-remaining", "2026-11-06T01:00:00Z", false});
    exportXlsx(report, output + "/remaining.xlsx");
    check(exportPdf(report, output + "/remaining.pdf", ReportFamily::Resources) > 0, "remaining PDF");
    exportXlsx(usageReport(usage, UsageHierarchy::Resources, "remaining-usage", "2026-11-06T01:00:00Z", false),
               output + "/remaining-usage.xlsx");
    const auto saved = d.path();
    old = prepared(d, DatedRemainingWorkEdit{feasible});
    d.open(saved);
    check(d.applyPreparedEdit(*old) == EditState::Stale, "reopened session apply");
    d.open(saved, true);
    rejects([&] { d.clearDatedRemainingWork(); });
    std::cout << "PASS dated remaining work: exact days, inherited/zero, calendars, status/window review, cost/usage, "
                 "float witness, leveling/adoption, immutable facts, history, schema/recovery, exports\n";
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    auto font = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        run();
    } catch (const ppm::AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << " " << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
