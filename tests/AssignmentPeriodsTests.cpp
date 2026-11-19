#include "application/Document.h"
#include "calculation/AssignmentPeriods.h"
#include "calculation/Effort.h"
#include "calculation/Usage.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProjectXml.h"
#include "persistence/PrimaveraXer.h"
#include "reporting/ReportExport.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTemporaryDir>
#include <future>
#include <iostream>
namespace {
using namespace ppm;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "expected rejection");
}
std::int64_t area(const std::vector<AssignmentPeriod> &periods) {
    std::int64_t total = 0;
    for (const auto &v : periods)
        total += (v.toMinute - v.fromMinute) * v.units;
    return total;
}
Project fixture() {
    Project p;
    p.documentId = "assignment-period-contract";
    p.name = "기간별 계획 배정 검증";
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    p.tasks = {{"T", "설계", 1440, {}}};
    Resource r;
    r.id = "R";
    r.name = "설계 인력";
    r.maxUnits = 200;
    r.rates = {{"2026-11-02", 60}, {"2026-11-03", 120}};
    p.resources = {r};
    p.assignments = {{"T", "R", 100, {{0, 480, 150}, {480, 960, 100}, {960, 1440, 50}}}};
    p.taskCosts = {{"T", 0}};
    return p;
}
auto prepare(Document &d, EditCommand command,
             std::shared_ptr<CalculationControl> control = std::make_shared<CalculationControl>()) {
    return std::async(std::launch::async,
                      [request = d.makeEditRequest(std::move(command)), control]() mutable {
                          return Document::prepareEdit(std::move(request), control);
                      })
        .get();
}
const ReportTable &table(const ProjectReport &r, const std::string &id) {
    for (const auto &t : r.tables)
        if (t.id == id)
            return t;
    throw std::runtime_error("missing report table");
}
void run() {
    for (auto duration : {1LL, 2LL, 7LL, 13LL, 1439LL, 1440LL, 5000000LL})
        for (auto curve : {AssignmentCurve::Front, AssignmentCurve::Back, AssignmentCurve::Bell})
            for (int units : {1, 100, 12345}) {
                const auto v = assignmentCurve(duration, units, curve);
                check(area(v) == duration * units, "curve lost integer work");
                check(v.front().fromMinute == 0 && v.back().toMinute == duration && v.size() <= 20, "curve coverage");
            }
    rejects([] { assignmentCurve(1440, 100000, AssignmentCurve::Front); });
    auto p = fixture();
    validate(p);
    const auto plan = calculatePlan(p);
    check(plan.status == CalculationStatus::Valid && plan.engineVersion == "ppm-effort-periods-1.0", "period engine");
    const auto load = resourceLoad(p, plan);
    check(load.status == CalculationStatus::Valid && load.days.size() == 3, "shaped load valid");
    check(load.days[0].demandUnitMinutes == 72000 && load.days[1].demandUnitMinutes == 48000 &&
              load.days[2].demandUnitMinutes == 24000,
          "day integral");
    check(load.days[0].peakUnits == 150 && load.days[2].peakUnits == 50, "instantaneous peaks");
    check(analyzeCosts(p, "").plan == 2160, "effective rate shaped cost");
    const auto usage = buildUsage(p, {"2026-11-02", "2026-11-08", {}, {}, UsagePeriod::Day});
    check(usage.leaves.size() == 2 && usage.leaves.back().values[0][0].labor == 72000 &&
              usage.leaves.back().values[0][1].cost == 960,
          "usage shaped work/cost");
    auto flat = p;
    flat.assignments[0].periods.clear();
    check(calculatePlan(flat).tasks == plan.tasks, "shape changed fixed duration dates");
    rejects([&] { calculatePlan(p, "ppm-effort-1.1"); });
    auto zero = p;
    zero.assignments[0].periods = {{0, 480, 0}};
    check(TaskExecution(zero, zero.tasks[0]).work(calculatePlan(zero).tasks[0]) == 96000, "zero override/default gaps");
    auto extended = p.tasks[0];
    extended.durationMinutes = 1920;
    TaskExecution extendedModel(p, extended);
    auto extendedRow = extendedModel.dates(0);
    extendedRow.remainingStart = extendedRow.start;
    check(extendedModel.work(extendedRow) == 192000, "remaining beyond plan default");
    extendedRow.completedActual = true;
    check(extendedModel.spans(extendedRow).empty(), "completed future demand");
    auto calendar = p;
    calendar.holidays = {"2026-11-03"};
    check(resourceLoad(calendar, calculatePlan(calendar)).days[1].date == "2026-11-04", "holiday anchored profile");
    calendar = p;
    TaskCalendar mornings;
    mornings.id = "am";
    mornings.name = "오전";
    mornings.periods = {{540, 720}};
    calendar.calendars = {mornings};
    calendar.resources[0].calendarId = "am";
    auto restricted = resourceLoad(calendar, calculatePlan(calendar));
    check(restricted.days[0].demandUnitMinutes == 27000 && restricted.days[1].demandUnitMinutes == 18000 &&
              restricted.days[2].demandUnitMinutes == 9000,
          "resource calendar intersection");
    calendar = p;
    calendar.tasks[0].breaks = {{"2026-11-02 13:00", "2026-11-02 18:00"}};
    const auto broken = calculatePlan(calendar);
    check(TaskExecution(calendar, calendar.tasks[0]).work(broken.tasks[0]) == 144000 &&
              broken.tasks[0].finish == "2026-11-05 15:00",
          "break consumed contour offsets");
    // Delaying the task translates the full demand shape; relative knots never become dates.
    calendar = p;
    calendar.tasks[0].constraintType = ConstraintType::SNET;
    calendar.tasks[0].constraintAt = "2026-11-04 09:00";
    check(resourceLoad(calendar, calculatePlan(calendar)).days[0].demandUnitMinutes == 72000, "moved contour");
    for (auto periods : {std::vector<AssignmentPeriod>{{1, 1, 100}},
                         {{0, 2, 100}, {1, 3, 100}},
                         {{0, 1, -1}},
                         {{0, 1, 100001}},
                         {{0, 5000001, 100}}}) {
        auto invalid = p;
        invalid.assignments[0].periods = periods;
        rejects([&] { validate(invalid); });
    }
    auto maximum = p;
    maximum.resources.clear();
    maximum.assignments.clear();
    for (int i = 0; i < 100; ++i) {
        auto r = p.resources[0];
        r.id = "R" + std::to_string(i);
        maximum.resources.push_back(r);
        Assignment a{"T", r.id, 100};
        for (int j = 0; j < 1000; ++j)
            a.periods.push_back({j, j + 1, 100});
        maximum.assignments.push_back(std::move(a));
    }
    validate(maximum);
    check(json::decodeProject(json::encodeProject(maximum), 17) == maximum, "maximum contour read");
    maximum.assignments[0].periods.push_back({1000, 1001, 100});
    rejects([&] { validate(maximum); });
    rejects([&] { json::decodeProject(json::encodeProject(maximum), 17); });
    auto invalid = p;
    invalid.tasks[0].taskType = TaskType::FixedUnits;
    validate(invalid);
    check(nativeSchemaFor(invalid) == 24, "fixed units contour schema");
    rejects([&] { json::decodeProject(json::encodeProject(invalid), 17); });
    invalid = p;
    invalid.tasks[0].workDriven = true;
    invalid.tasks[0].workUnitMinutes = 144000;
    rejects([&] { validate(invalid); });
    invalid = p;
    invalid.assignments[0].periods.assign(1025, {0, 1, 100});
    rejects([&] { validate(invalid); });
    for (const auto &value :
         {QJsonValue(QJsonValue::Null), QJsonValue("100"), QJsonValue(true), QJsonValue(-1), QJsonValue(1.5)}) {
        auto malformed = json::encodeProject(p);
        auto rows = malformed["assignments"].toArray();
        auto row = rows[0].toObject();
        auto values = row["periods"].toArray();
        auto period = values[0].toObject();
        period["units"] = value;
        values[0] = period;
        row["periods"] = values;
        rows[0] = row;
        malformed["assignments"] = rows;
        rejects([&] { json::decodeProject(malformed, 17); });
    }
    auto json = json::encodeProject(p);
    check(json::decodeProject(json, 17) == p && nativeSchemaFor(p) == 17, "schema17 round trip");
    rejects([&] { json::decodeProject(json, 16); });
    auto assignments = json["assignments"].toArray();
    auto a = assignments[0].toObject();
    auto periods = a["periods"].toArray();
    auto v = periods[0].toObject();
    v["units"] = 1.5;
    periods[0] = v;
    a["periods"] = periods;
    assignments[0] = a;
    json["assignments"] = assignments;
    rejects([&] { json::decodeProject(json, 17); });
    QTemporaryDir temp;
    JsonProjectStore store;
    const auto path = temp.filePath("periods.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    const auto baseline = d.captureBaseline("배정 승인", "검토자", "계획 확정", "2026-11-05T01:00:00Z");
    d.saveScenario("곡선 시나리오", "task-order", 30, "2026-11-05T01:00:00Z", d.project()->documentId,
                   d.project()->revision);
    const auto frozen = d.project()->baselines;
    d.setAssignmentPeriods("T", "R", {});
    check(nativeSchemaFor(*d.project()) == 17 && !hasAssignmentPeriods(*d.project()), "archive-only schema17");
    check(json::decodeProject(json::encodeProject(*d.project()), 17) == *d.project(), "archive-only round trip");
    d.undo();
    const auto copies = d.duplicateTasks({"T"});
    check(copies.size() == 1 && d.project()->assignments.back().taskId == copies[0] &&
              d.project()->assignments.back().periods == p.assignments[0].periods,
          "duplicate lost contour");
    d.removeTasks(copies);
    check(d.project()->assignments.size() == 1, "remove retained contour reference");
    d.setAssignmentPeriods("T", "R", {{0, 480, 50}});
    check(d.dirty() && d.project()->baselines == frozen, "edit mutated baseline");
    d.undo();
    check(d.project()->assignments == p.assignments, "undo contour");
    d.redo();
    check(d.project()->assignments[0].periods.size() == 1, "redo contour");
    d.undo();
    auto before = *d.project();
    rejects([&] { d.setDuration(0, 0); });
    rejects([&] { d.setDuration(0, {}); });
    check(*d.project() == before, "unsupported edit partially applied");
    d.setTaskEffort(0, TaskType::FixedWork, true, {}, 144000, {});
    check(d.project()->tasks[0].workUnitMinutes == 144000 && nativeSchemaFor(*d.project()) == 24,
          "fixed work contour edit");
    d.undo();
    d.setTaskEffort(0, TaskType::FixedDuration, true, 1440, 150000, {});
    const auto inversePlan = calculatePlan(*d.project());
    check(inversePlan.status == CalculationStatus::Valid &&
              TaskExecution(*d.project(), d.project()->tasks[0]).work(inversePlan.tasks[0]) == 150000,
          "fixed duration contour inverse");
    d.undo();
    check(d.project()->assignments == before.assignments && d.project()->tasks == before.tasks, "inverse history");
    d.setDuration(0, 480);
    check(d.project()->assignments == p.assignments &&
              TaskExecution(*d.project(), d.project()->tasks[0]).work(calculatePlan(*d.project()).tasks[0]) == 72000,
          "shortening lost dormant knots");
    d.setDuration(0, 1920);
    check(TaskExecution(*d.project(), d.project()->tasks[0]).work(calculatePlan(*d.project()).tasks[0]) == 192000,
          "extension default");
    d.setDuration(0, 1440);
    verifyBaseline(d.project()->baselines[0]);
    const auto output = QDir::currentPath() + "/assignment-periods-qa";
    QDir().mkpath(output);
    for (const auto *name : {"periods.ppm", "periods.xlsx", "periods.pdf", "usage.xlsx"})
        QFile::remove(output + "/" + name);
    d.saveAs((output + "/periods.ppm").toStdString());
    const auto report =
        buildReport(*d.project(),
                    {{}, baseline, "budget-remaining-v1", {}, {}, "assignment-periods", "2026-11-05T01:00:00Z", false});
    check(table(report, "assignmentPeriods").rows.size() == 3, "report contour definitions");
    exportXlsx(report, output + "/periods.xlsx");
    check(exportPdf(report, output + "/periods.pdf", ReportFamily::Resources) > 0, "pdf");
    exportXlsx(usageReport(usage, UsageHierarchy::Resources, "assignment-usage", "2026-11-05T01:00:00Z", false),
               output + "/usage.xlsx");
    const auto xml = exportProjectXml(*d.project());
    check(xml.xml.contains("<TimephasedData>") &&
              QJsonDocument(xml.report).toJson().contains(QString::fromUtf8("계획 TimephasedData").toUtf8()),
          "XML planned contour exchange");
    const auto xer = exportPrimaveraXer(*d.project());
    check(xer.xer.contains("RSRCCURVDATA") &&
              QJsonDocument(xer.report).toJson().contains(QString::fromUtf8("20개 구간/21지점").toUtf8()),
          "XER planned contour projection/loss");
    d.setStatusAt("2026-11-03 18:00");
    ProgressEntry entry;
    entry.taskId = "T";
    entry.statusAt = *d.project()->statusAt;
    entry.recordedAt = "2026-11-05T02:00:00Z";
    entry.reporter = "담당";
    entry.reason = "잔여 확인";
    entry.actualStart = "2026-11-02 09:00";
    entry.actualDurationMinutes = 960;
    entry.remainingMinutes = 480;
    d.recordProgress(entry);
    const auto future = calculate(*d.project());
    check(future.status == CalculationStatus::Valid, "forecast status");
    check(resourceLoad(*d.project(), future).days[0].demandUnitMinutes == 24000, "forecast must use tail");
    const auto remaining = buildUsage(*d.project(), {"2026-11-02", "2026-11-09", baseline, {}, UsagePeriod::Day});
    check(usageTotal(remaining.leaves.back().values[2]).labor == 24000 &&
              usageTotal(remaining.leaves.back().values[2]).cost == 480,
          "forecast usage/cost tail");
    PeriodActualSubmission actual;
    actual.reporter = "실적 담당";
    actual.reason = "관측 공수";
    actual.recordedAt = "2026-11-05T02:00:00Z";
    PeriodActualEntry observation;
    observation.taskId = "T";
    observation.resourceId = "R";
    observation.date = "2026-11-02";
    observation.workUnitMinutes = 6001;
    actual.entries = {observation};
    d.recordPeriodActuals(actual);
    const auto actualFacts = d.project()->periodActuals;
    const auto facts = d.project()->progress;
    d.setAssignmentPeriods("T", "R", {{0, 1440, 75}});
    check(d.project()->progress == facts && d.project()->periodActuals == actualFacts, "contour inferred actuals");
    auto prepared = prepare(d, AssignmentPeriodsEdit{"T", "R", {{0, 1440, 50}}});
    d.renameTask(0, "문서 변경");
    check(d.applyPreparedEdit(*prepared) == EditState::Stale, "stale worker applied");
    auto cancelled = std::make_shared<CalculationControl>();
    cancelled->cancel();
    prepared = prepare(d, AssignmentPeriodsEdit{"T", "R", {{0, 1440, 50}}}, cancelled);
    check(d.applyPreparedEdit(*prepared) == EditState::Cancelled, "cancel applied");
    prepared = prepare(d, AssignmentPeriodsEdit{"T", "R", {{0, 1440, 50}}});
    const auto savedPath = d.path();
    d.open(savedPath);
    check(d.applyPreparedEdit(*prepared) == EditState::Stale, "same document reopened accepted old worker");
    d.open(savedPath, true);
    rejects([&] { d.setAssignmentPeriods("T", "R", {}); });
    d.open(savedPath);
    d.setAssignmentPeriods("T", "R", {{0, 1440, 75}});
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-05T02:00:00Z"));
    session.reset();
    const auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema17 recovery scan");
    auto claim = recovery.claim(scan.candidates[0]);
    Document restored(store);
    restored.recover(claim->snapshot);
    check(restored.project()->assignments == d.project()->assignments &&
              restored.project()->baselines == d.project()->baselines,
          "recovery contours/archives");
    // A zero part allows two tasks to overlap, but the 150% peak must remain within 200%.
    auto level = fixture();
    level.tasks.push_back({"U", "후속 자원", 1440, {}});
    level.taskCosts.push_back({"U", 0});
    level.assignments = {{"T", "R", 100, {{0, 480, 150}, {480, 1440, 0}}}, {"U", "R", 100}};
    const auto leveled = levelResources(level, "task-order", 30);
    check(leveled.leveled.status == CalculationStatus::Valid && leveled.after.status == CalculationStatus::Valid,
          "level true curve demand");
    check(leveled.leveled.tasks[1].start == "2026-11-03 09:00", "level ignored zero/peak contour");
    check(!leveled.leveled.levelingFloatCalculated ||
              leveled.leveled.calculationPolicy.ends_with("resource-repartition-float-v3"),
          "constant-envelope float used");
    auto selected = fixture();
    selected.assignments[0].periods = {{0, 1440, 75}};
    const auto selectedPath = temp.filePath("selected.ppm").toStdString();
    store.save(selectedPath, selected, {});
    Document adopted(store);
    adopted.open(selectedPath);
    LevelingSelection selection;
    selection.priority = "task-order";
    selection.horizonWorkDays = 30;
    selection.policy = levelingPolicyFor(*adopted.project());
    selection.adoptedAt = "2026-11-05T02:00:00Z";
    selection.reason = "승인";
    selection.sourceDocumentId = adopted.project()->documentId;
    selection.sourceRevision = adopted.project()->revision;
    adopted.adoptLeveling(selection);
    const auto decisions = adopted.project()->levelingDecisions.size();
    check(calculatePlan(*adopted.project()).engineVersion == "ppm-adopted-periods-1.0", "adopted engine identity");
    const auto adoptedBaseline = adopted.captureBaseline("승인", "담당", "곡선", "2026-11-05T02:00:00Z");
    (void)adoptedBaseline;
    verifyBaseline(adopted.project()->baselines[0]);
    adopted.setAssignmentPeriods("T", "R", {{0, 1440, 50}});
    check(!adopted.project()->planLeveling && adopted.project()->levelingDecisions.size() == decisions + 1,
          "contour skipped decision invalidation");
    adopted.undo();
    check(adopted.project()->planLeveling.has_value(), "atomic leveling undo");
    check(json::decodeProject(json::encodeProject(*adopted.project()), 17) == *adopted.project(),
          "adopted archive round trip");
    adopted.setAssignmentPeriods("T", "R", {});
    check(nativeSchemaFor(*adopted.project()) == 17 && !hasAssignmentPeriods(*adopted.project()),
          "policy history writer downgrade");
    check(json::decodeProject(json::encodeProject(*adopted.project()), 17) == *adopted.project(),
          "removed contour history round trip");
    rejects([&] { json::decodeProject(json::encodeProject(*adopted.project()), 16); });
    auto historyOnly = *adopted.project();
    historyOnly.baselines.clear();
    historyOnly.scenarios.clear();
    check(nativeSchemaFor(historyOnly) == 17 &&
              json::decodeProject(json::encodeProject(historyOnly), 17) == historyOnly,
          "historical contour policy lost without current or archived contours");
    rejects([&] { json::decodeProject(json::encodeProject(historyOnly), 16); });

    auto old = fixture();
    old.assignments[0].periods.clear();
    check(nativeSchemaFor(old) == 9, "old writer schema drift");
    QJsonArray metrics;
    for (const int count : {1000, 10000}) {
        auto scale = fixture();
        scale.tasks.clear();
        scale.assignments.clear();
        scale.taskCosts.clear();
        scale.resources[0].maxUnits = 100000;
        for (int i = 0; i < count; ++i) {
            const auto id = "S" + std::to_string(i);
            scale.tasks.push_back({id, id, 60, {}});
            scale.assignments.push_back({id, "R", 1, {{0, 30, 1}, {30, 60, 0}}});
        }
        QElapsedTimer timer;
        timer.start();
        const auto scheduled = calculatePlan(scale);
        const auto planMs = timer.elapsed();
        timer.restart();
        const auto demand = resourceLoad(scale, scheduled);
        const auto loadMs = timer.elapsed();
        check(scheduled.status == CalculationStatus::Valid && demand.status == CalculationStatus::Valid &&
                  demand.days.size() == 1 && demand.days[0].peakUnits == count &&
                  demand.days[0].demandUnitMinutes == 30LL * count,
              "scale contour integral");
        metrics.append(QJsonObject{{"assignments", count},
                                   {"periods", count * 2},
                                   {"plan_ms", double(planMs)},
                                   {"load_ms", double(loadMs)},
                                   {"exact_work", true}});
    }
    QFile metricFile(output + "/scale.json");
    check(metricFile.open(QIODevice::WriteOnly), "scale evidence");
    metricFile.write(QJsonDocument(metrics).toJson());
    std::cout << "Assignment periods: exact curves, calendars, costs/Usage, forecast tail, leveling, schema17, "
                 "history/worker/recovery/export passed\n";
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
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
