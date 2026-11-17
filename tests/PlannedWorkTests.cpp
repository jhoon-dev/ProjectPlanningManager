#include "application/Document.h"
#include "calculation/PlannedWork.h"
#include "calculation/Effort.h"
#include "calculation/Usage.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
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
#include <random>
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
Project fixture() {
    Project p;
    p.documentId = "planned-work-contract";
    p.name = "계획 공수 변환 검증";
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "설계", 1440, {}}};
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    r.rates = {{"2026-11-02", 60}, {"2026-11-03", 120}};
    p.resources = {r};
    p.assignments = {{"T", "R", 100}};
    p.taskCosts = {{"T", 0}};
    return p;
}
// Independent civil-minute oracle. No TaskExecution/WorkCalendar/inverse arithmetic.
bool working(const Project &p, const std::string &id, QDateTime at) {
    TaskCalendar c;
    c.holidays = p.holidays;
    if (!id.empty())
        c = *std::find_if(p.calendars.begin(), p.calendars.end(), [&](const auto &x) { return x.id == id; });
    const auto date = at.date().toString("yyyy-MM-dd").toStdString();
    const int minute = at.time().hour() * 60 + at.time().minute();
    return std::find(c.weekdays.begin(), c.weekdays.end(), at.date().dayOfWeek()) != c.weekdays.end() &&
           std::find(c.holidays.begin(), c.holidays.end(), date) == c.holidays.end() &&
           std::any_of(c.periods.begin(), c.periods.end(),
                       [&](const auto &v) { return minute >= v.begin && minute < v.end; });
}
std::vector<std::int64_t> oracle(const Project &p, const TaskSchedule &dates,
                                 const std::vector<PlannedWorkAmount> &targets) {
    std::vector<std::int64_t> out(targets.size(), 0);
    auto at = QDateTime::fromString(QString::fromStdString(dates.start), "yyyy-MM-dd HH:mm");
    const auto finish = QDateTime::fromString(QString::fromStdString(dates.finish), "yyyy-MM-dd HH:mm");
    std::int64_t offset = 0;
    for (; at < finish; at = at.addSecs(60)) {
        const auto stamp = at.toString("yyyy-MM-dd HH:mm").toStdString();
        const auto &t = p.tasks[0];
        if (!working(p, t.calendarId, at) || std::any_of(t.breaks.begin(), t.breaks.end(), [&](const auto &b) {
                return stamp >= b.start && stamp < b.finish;
            }))
            continue;
        if (working(p, p.resources[0].calendarId, at)) {
            int units = p.assignments[0].units;
            for (const auto &v : p.assignments[0].periods)
                if (offset >= v.fromMinute && offset < v.toMinute) {
                    units = v.units;
                    break;
                }
            for (std::size_t i = 0; i < targets.size(); ++i)
                if (offset >= targets[i].fromMinute && offset < targets[i].toMinute)
                    out[i] += units;
        }
        ++offset;
    }
    check(offset == *p.tasks[0].durationMinutes, "oracle task minutes");
    return out;
}
PlannedWorkResult exact(const Project &p, const std::vector<PlannedWorkAmount> &amounts) {
    const auto before = p;
    auto r = convertPlannedWork(p, "T", "R", amounts);
    check(p == before, "conversion mutated source");
    auto converted = p;
    converted.assignments[0].periods = r.periods;
    const auto values = oracle(converted, r.dates, amounts);
    for (std::size_t i = 0; i < amounts.size(); ++i) {
        check(values[i] == amounts[i].workUnitMinutes, "independent minute oracle mismatch");
        check(r.buckets[i].amount == amounts[i], "certification mismatch");
    }
    check(calculatePlan(converted).tasks == calculatePlan(p).tasks, "fixed dates changed");
    return r;
}
void run() {
    for (std::int64_t i = 0; i < 20000; ++i)
        check(parsePlannedWorkHours(plannedWorkHours(i)) == i, "decimal roundtrip");
    for (auto i : {maxWork, maxWork - 1, 480000000000LL})
        check(parsePlannedWorkHours(plannedWorkHours(i)) == i, "large decimal roundtrip");
    for (const auto *s : {"", ".", "1.", ".1", "-1", "+1", "1e2", "NaN", "inf", "1,5", "1.00001", "1..0",
                          "99999999999999999999999", "83333333.3335"})
        rejects([&] { parsePlannedWorkHours(s); });
    check(parsePlannedWorkHours("0.0001") == 1 && parsePlannedWorkHours("0") == 0, "quantization");
    auto p = fixture();
    const auto described = describePlannedWork(p, "T", "R", PlannedWorkPeriod::Day);
    check(described.buckets.size() == 3 && described.buckets[0].availableMinutes == 480 &&
              described.buckets[2].amount.workUnitMinutes == 48000,
          "daily buckets");
    check(describePlannedWork(p, "T", "R", PlannedWorkPeriod::Week).buckets.size() == 1, "week bucket");
    check(describePlannedWork(p, "T", "R", PlannedWorkPeriod::Month).buckets.size() == 1, "month bucket");
    auto r = exact(p, {{0, 480, 12000}, {480, 960, 0}, {960, 1440, 60000}});
    check(r.periods == std::vector<AssignmentPeriod>{{0, 480, 25}, {480, 960, 0}, {960, 1440, 125}}, "exact rates");
    auto converted = p;
    converted.assignments[0].periods = r.periods;
    check(analyzeCosts(converted, "").plan == 1320, "converted cost");
    const auto usage = buildUsage(converted, {"2026-11-02", "2026-11-08", {}, {}, UsagePeriod::Day});
    check(usage.leaves.back().values[0][2].labor == 60000 && usage.leaves.back().values[0][2].cost == 1200,
          "converted usage work/cost");
    auto restricted = p;
    restricted.calendars = {{"am", "오전", {1, 2, 3, 4, 5}, {{540, 720}}, {}}};
    restricted.resources[0].calendarId = "am";
    auto rr = exact(restricted, {{0, 480, 48000}, {480, 960, 1}, {960, 1440, 0}});
    check(rr.periods.front().units == 267 && rr.periods.front().toMinute == 120, "exact remainder split");
    auto overloaded = restricted;
    overloaded.assignments[0].periods = rr.periods;
    check(resourceLoad(overloaded, calculatePlan(overloaded)).days[0].peakUnits == 267, "overload clipped");
    exact(p, {{0, 480, 1}});
    exact(p, {{0, 480, 48000000}});
    rejects([&] { convertPlannedWork(p, "T", "R", {{0, 480, 48000001}}); });
    auto sparse = p;
    sparse.assignments[0].periods = {{0, 100, 25}, {100, 480, 150}, {2000, 2200, 300}};
    const auto unchanged = exact(sparse, {{0, 480, 59500}});
    check(unchanged.periods == sparse.assignments[0].periods, "unchanged curve reshaped");
    const auto partial = exact(sparse, {{480, 960, 0}});
    check(partial.periods.front() == sparse.assignments[0].periods.front() &&
              partial.periods.back() == sparse.assignments[0].periods.back(),
          "unselected/dormant periods lost");
    auto multi = p;
    auto other = p.resources[0];
    other.id = "R2";
    multi.resources.push_back(other);
    multi.assignments.push_back({"T", "R2", 200, {{0, 100, 300}}});
    exact(multi, {{0, 480, 6001}});
    auto full = p;
    for (int i = 0; i < 1024; ++i)
        full.assignments[0].periods.push_back({i, i + 1, 25 + i % 2});
    rejects([&] { convertPlannedWork(full, "T", "R", {{1024, 1440, 1}}); });
    auto complex = restricted;
    complex.holidays = {"2026-11-03"};
    complex.calendars[0].holidays = {"2026-11-04"};
    complex.tasks[0].breaks = {{"2026-11-02 10:00", "2026-11-02 14:00"}};
    auto buckets = describePlannedWork(complex, "T", "R", PlannedWorkPeriod::Day);
    check(buckets.buckets[0].availableMinutes == 60, "break intersection");
    check(buckets.buckets[1].availableMinutes == 0, "resource holiday intersection");
    rejects([&] {
        convertPlannedWork(complex, "T", "R",
                           {{buckets.buckets[1].amount.fromMinute, buckets.buckets[1].amount.toMinute, 1}});
    });
    exact(complex, {{0, 300, 6001}});
    complex.tasks[0].calendarId = "am";
    exact(complex, {{0, 180, 24000}});
    std::mt19937 random(42);
    for (int n = 0; n < 24; ++n) {
        auto sample = restricted;
        sample.tasks[0].durationMinutes = 960;
        sample.calendars[0].periods = {{540 + int(random() % 100), 720}, {780, 800 + int(random() % 200)}};
        if (n % 2)
            sample.tasks[0].breaks = {{"2026-11-02 10:00", "2026-11-02 11:17"}};
        const auto days = describePlannedWork(sample, "T", "R", PlannedWorkPeriod::Day);
        std::vector<PlannedWorkAmount> targets;
        for (const auto &b : days.buckets)
            targets.push_back(
                {b.amount.fromMinute, b.amount.toMinute, b.availableMinutes ? std::int64_t(random() % 100001) : 0});
        exact(sample, targets);
    }
    auto boundary = p;
    boundary.startDate = "2026-11-27";
    boundary.tasks[0].durationMinutes = 1440;
    check(describePlannedWork(boundary, "T", "R", PlannedWorkPeriod::Week).buckets.size() == 2, "Monday boundary");
    check(describePlannedWork(boundary, "T", "R", PlannedWorkPeriod::Month).buckets.size() == 2, "month boundary");
    auto horizon = p;
    horizon.startDate = "9999-12-31";
    horizon.tasks[0].durationMinutes = 60;
    check(describePlannedWork(horizon, "T", "R", PlannedWorkPeriod::Month).buckets.size() == 1,
          "next bucket beyond horizon");
    auto steps = p;
    ActivityDefinition activity;
    activity.id = "steps";
    activity.name = "체크리스트";
    activity.taskId = "T";
    activity.steps = {{"s1", "검토", 1}};
    steps.activities = {activity};
    exact(steps, {{0, 480, 6001}});
    auto large = p;
    large.tasks[0].durationMinutes = 480 * 1025;
    rejects([&] { describePlannedWork(large, "T", "R", PlannedWorkPeriod::Day); });
    check(describePlannedWork(large, "T", "R", PlannedWorkPeriod::Week).buckets.size() < 1024, "weekly fallback");
    for (const auto &bad : std::vector<std::vector<PlannedWorkAmount>>{{},
                                                                       {{-1, 10, 0}},
                                                                       {{0, 0, 0}},
                                                                       {{0, 1441, 0}},
                                                                       {{0, 2, -1}},
                                                                       {{0, 10, 1}, {9, 11, 0}},
                                                                       {{20, 30, 0}, {0, 10, 0}},
                                                                       std::vector<PlannedWorkAmount>(1025, {0, 1, 0})})
        rejects([&] { convertPlannedWork(p, "T", "R", bad); });
    rejects([&] { convertPlannedWork(p, "missing", "R", {{0, 1, 0}}); });
    for (auto kind : {TaskType::FixedWork, TaskType::FixedUnits}) {
        auto bad = p;
        bad.tasks[0].taskType = kind;
        bad.tasks[0].workUnitMinutes = 144000;
        rejects([&] { convertPlannedWork(bad, "T", "R", {{0, 1, 0}}); });
    }
    auto bad = p;
    bad.tasks[0].durationMinutes = 0;
    rejects([&] { convertPlannedWork(bad, "T", "R", {{0, 1, 0}}); });
    bad = p;
    bad.tasks[0].durationMinutes.reset();
    rejects([&] { convertPlannedWork(bad, "T", "R", {{0, 1, 0}}); });
    bad = p;
    bad.tasks[0].workDriven = true;
    bad.tasks[0].workUnitMinutes = 144000;
    rejects([&] { convertPlannedWork(bad, "T", "R", {{0, 1, 0}}); });
    auto cancelled = std::make_shared<CalculationControl>();
    cancelled->cancel();
    bool stopped = false;
    try {
        convertPlannedWork(p, "T", "R", {{0, 480, 0}}, cancelled);
    } catch (const CalculationCancelled &) {
        stopped = true;
    }
    check(stopped, "cooperative cancellation missing");
    // Conversion is a draft; applying it uses the established atomic command and history.
    QTemporaryDir temp;
    JsonProjectStore store;
    auto path = temp.filePath("work.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    d.setStatusAt("2026-11-03 18:00");
    d.captureBaseline("승인", "검토자", "동결", "2026-11-05T01:00:00Z");
    const auto frozen = d.project()->baselines;
    PeriodActualSubmission actual;
    actual.reporter = "담당";
    actual.reason = "관측";
    actual.recordedAt = "2026-11-05T02:00:00Z";
    PeriodActualEntry entry;
    entry.taskId = "T";
    entry.resourceId = "R";
    entry.date = "2026-11-02";
    entry.workUnitMinutes = 6001;
    actual.entries = {entry};
    d.recordPeriodActuals(actual);
    const auto before = *d.project();
    auto prepared = std::async(std::launch::async, [request = d.makeEditRequest(
                                                        AssignmentPeriodsEdit{"T", "R", r.periods})]() mutable {
                        return Document::prepareEdit(std::move(request), std::make_shared<CalculationControl>());
                    }).get();
    check(d.applyPreparedEdit(*prepared) == EditState::Applied, "converted draft apply");
    check(d.project()->revision == before.revision + 1 && d.project()->baselines == frozen &&
              d.project()->periodActuals == before.periodActuals && d.project()->progress == before.progress,
          "facts/archive changed");
    d.undo();
    check(d.project()->assignments == before.assignments, "one-step undo");
    d.redo();
    check(d.project()->assignments[0].periods == r.periods, "redo");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-05T02:00:00Z"));
    session.reset();
    auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "recovery");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(store);
    restored.recover(claim->snapshot);
    check(restored.project()->assignments == d.project()->assignments &&
              restored.project()->baselines == d.project()->baselines &&
              restored.project()->periodActuals == d.project()->periodActuals,
          "recovered inputs/facts");
    d.save();
    Document reopened(store);
    reopened.open(path, true);
    check(reopened.project() == d.project() && nativeSchemaFor(*d.project()) == 17, "native persistence");
    const auto output = QDir::currentPath() + "/planned-work-qa";
    QDir().mkpath(output);
    for (const auto *name : {"work.ppm", "work.xlsx", "work.pdf", "usage.xlsx", "targets.json"})
        QFile::remove(output + "/" + name);
    d.saveAs((output + "/work.ppm").toStdString());
    const auto report = buildReport(
        *d.project(), {{}, {}, "budget-remaining-v1", {}, {}, "planned-work", "2026-11-05T02:00:00Z", false});
    exportXlsx(report, output + "/work.xlsx");
    check(exportPdf(report, output + "/work.pdf", ReportFamily::Resources) > 0, "PDF export");
    exportXlsx(usageReport(usage, UsageHierarchy::Resources, "planned-work", "2026-11-05T02:00:00Z", false),
               output + "/usage.xlsx");
    QJsonArray targets;
    for (const auto &b : r.buckets)
        targets.append(QJsonObject{{"from", double(b.amount.fromMinute)},
                                   {"to", double(b.amount.toMinute)},
                                   {"workUnitMinutes", double(b.amount.workUnitMinutes)}});
    QFile json(output + "/targets.json");
    check(json.open(QIODevice::WriteOnly), "target artifact");
    json.write(QJsonDocument(targets).toJson());
    std::cout << "PASS planned work: civil-minute oracle, rounding, calendars, breaks, exact inverse, preservation, "
                 "history and exports\n";
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto font =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        QGuiApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        run();
    } catch (const jobs::AppError &e) {
        std::cerr << "FAIL: " << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << " " << a;
        std::cerr << '\n';
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
