#include "application/Document.h"
#include "calculation/Effort.h"
#include "calculation/ForecastFloat.h"
#include "calculation/Reports.h"
#include "persistence/JsonProjectStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <future>
#include <iostream>
#include <random>
#include <thread>

namespace {
using namespace jobs;
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
Project fixture(const std::string &profile) {
    Project p;
    p.documentId = "forecast-float";
    p.name = "GAP-T01";
    p.startDate = "2026-11-02";
    p.calculationProfile = profile;
    p.tasks = {{"A", "A", 480, {}}, {"B", "B", 480, {"A"}}, {"C", "C", 480, {"B"}}, {"D", "D", 480, {}}};
    return p;
}
ProgressEntry report(const Project &p, const std::string &id, std::int64_t remaining,
                     std::optional<std::string> start = {}, std::optional<std::string> finish = {}) {
    ProgressEntry e;
    e.id = "entry-" + id;
    e.taskId = id;
    e.statusAt = *p.statusAt;
    e.sourceDocumentId = p.documentId;
    e.recordedRevision = p.revision;
    e.recordedAt = "2026-11-02T09:00:00Z";
    e.reporter = "scheduler";
    e.reason = "status report";
    e.actualStart = start;
    e.actualFinish = finish;
    e.actualDurationMinutes = start ? 60 : 0;
    e.remainingMinutes = remaining;
    return e;
}
void startFixture(Project &p) {
    p.statusAt = "2026-11-02 18:00";
    p.progress.push_back(report(p, "A", 0, "2026-11-02 09:00", "2026-11-02 18:00"));
}
void goldenCases() {
    for (const std::string profile : {"native-fs0-workminutes-v1", pdmProfile, constraintProfile, effortProfile}) {
        auto p = fixture(profile);
        startFixture(p);
        const auto before = p;
        const auto r = calculate(p);
        check(r.status == CalculationStatus::Valid && r.forecastFloatCalculated &&
                  r.projectFinish == "2026-11-04 18:00",
              "GAP-T01 forecast failed");
        check(r.tasks[1].totalFloatMinutes == 0 && r.tasks[2].totalFloatMinutes == 0 &&
                  r.tasks[3].totalFloatMinutes == 480 && r.tasks[3].freeFloatMinutes == 480,
              "GAP-T01 independent 0/0/8h expectation failed");
        check(!hasScheduleFloat(r, r.tasks[0]) && !r.tasks[0].critical && r.tasks[0].drivingPredecessors.empty() &&
                  r.tasks[2].drivingPredecessors == std::vector<std::string>{"B"},
              "completed work or future driving relation misclassified");
        check(p == before, "forecast modified input or ledger");
        const auto leveled = levelResources(p, "task-order", 260);
        check(leveled.leveled.status == CalculationStatus::Valid && !leveled.leveled.forecastFloatCalculated &&
                  hasScheduleFloat(leveled.leveled, leveled.leveled.tasks[1]),
              "logical float leaked into leveling");
        p.progress.push_back(report(p, "D", 960));
        auto changed = calculate(p);
        check(changed.tasks[3].critical && changed.tasks[3].totalFloatMinutes == 0 &&
                  changed.tasks[1].totalFloatMinutes == 0,
              "remaining correction did not change criticality");
        p.progress.pop_back();
        p.statusAt = "2026-11-03 18:00";
        changed = calculate(p);
        check(changed.projectFinish == "2026-11-05 18:00" && changed.tasks[3].totalFloatMinutes == 480 &&
                  changed.tasks[0].finish == "2026-11-02 18:00",
              "status move changed actual or float basis");
    }
}
// Independent minute enumeration on a continuous calendar. No PDM inverse,
// backward engine, binary search, or TaskExecution is used to obtain the oracle.
int minute(const std::string &at) {
    check(at.substr(0, 10) == "2026-11-02", "oracle exceeded its one-day fixture");
    return std::stoi(at.substr(11, 2)) * 60 + std::stoi(at.substr(14, 2));
}
void minuteOracle(const Project &p, const ScheduleResult &r) {
    const auto n = p.tasks.size();
    check(r.status == CalculationStatus::Valid && r.forecastFloatCalculated, "oracle forecast unavailable");
    std::vector<int> remaining(n), early(n), late(n), actualStart(n, -1), expectedStart(n), expectedFinish(n);
    std::vector<bool> complete(n);
    std::vector<std::vector<std::pair<std::size_t, Dependency>>> next(n);
    const auto index = [&](const std::string &id) {
        for (std::size_t i = 0; i < n; ++i)
            if (p.tasks[i].id == id)
                return i;
        throw std::runtime_error("missing oracle reference");
    };
    for (std::size_t i = 0; i < n; ++i) {
        const auto *e = progressAsOf(p, p.tasks[i].id);
        complete[i] = e && e->actualFinish;
        remaining[i] = int(e ? e->remainingMinutes : *p.tasks[i].durationMinutes);
        if (e && e->actualStart)
            actualStart[i] = minute(*e->actualStart);
        if (complete[i]) {
            expectedStart[i] = actualStart[i];
            expectedFinish[i] = minute(*e->actualFinish);
        } else {
            early[i] = minute(*p.statusAt);
            for (const auto &d : dependencies(p.tasks[i])) {
                const auto j = index(d.predecessorId);
                const auto bound = std::max(0, (dependencyFromFinish(d.type) ? expectedFinish[j] : expectedStart[j]) +
                                                   int(d.lagMinutes));
                early[i] = std::max(early[i], bound - (dependencyToFinish(d.type) ? remaining[i] : 0));
                next[j].push_back({i, d});
            }
            expectedStart[i] = actualStart[i] < 0 ? early[i] : actualStart[i];
            expectedFinish[i] = early[i] + remaining[i];
        }
        check(minute(r.tasks[i].start) == expectedStart[i] && minute(r.tasks[i].finish) == expectedFinish[i],
              "forward result differs from independent minute oracle");
    }
    const int finish = *std::max_element(expectedFinish.begin(), expectedFinish.end());
    late = early;
    for (std::size_t reverse = n; reverse > 0; --reverse) {
        const auto i = reverse - 1;
        if (complete[i])
            continue;
        auto latest = [&](bool total) {
            int best = early[i];
            for (int candidate = early[i]; candidate + remaining[i] <= finish; ++candidate) {
                bool fits = true;
                for (const auto &[j, d] : next[i]) {
                    const int source = dependencyFromFinish(d.type) ? candidate + remaining[i]
                                       : actualStart[i] >= 0        ? actualStart[i]
                                                                    : candidate;
                    const int target = total ? late[j] : early[j];
                    if (std::max(0, source + int(d.lagMinutes)) >
                        target + (dependencyToFinish(d.type) ? remaining[j] : 0))
                        fits = false;
                }
                if (fits)
                    best = candidate;
            }
            return best;
        };
        late[i] = latest(true);
        check(r.tasks[i].totalFloatMinutes == late[i] - early[i] &&
                  r.tasks[i].freeFloatMinutes == latest(false) - early[i],
              "float differs from exhaustive minute oracle");
        check(r.tasks[i].critical == (late[i] == early[i]), "oracle critical mismatch");
    }
}
void generatedCases() {
    std::mt19937 rng(1102);
    for (const auto profile : {pdmProfile, constraintProfile, effortProfile})
        for (int sample = 0; sample < 36; ++sample) {
            auto p = fixture(profile);
            p.tasks.clear();
            p.calendars = {{"continuous", "24h", {1, 2, 3, 4, 5, 6, 7}, {{0, 1440}}, {}}};
            p.statusAt = "2026-11-02 06:00";
            for (int i = 0; i < 6; ++i) {
                Task t{"T" + std::to_string(i), "Task", std::int64_t(rng() % 40), {}};
                t.calendarId = "continuous";
                for (int j = 0; j < i; ++j)
                    if (rng() % 2) {
                        t.predecessors.push_back(p.tasks[j].id);
                        const Dependency edge{p.tasks[j].id, DependencyType(rng() % 4), std::int64_t(rng() % 81) - 40};
                        if (edge.type != DependencyType::FS || edge.lagMinutes != 0)
                            t.dependencyRules.push_back(edge);
                    }
                p.tasks.push_back(t);
            }
            p.progress = {report(p, "T0", 35, "2026-11-02 01:00"),
                          report(p, "T1", 0, "2026-11-02 01:30", "2026-11-02 02:00")};
            p.progress.back().actualDurationMinutes = 30;
            validate(p);
            const auto result = calculate(p);
            if (result.status != CalculationStatus::Valid) {
                std::cerr << profile << " sample=" << sample << '\n';
                for (const auto &diagnostic : result.diagnostics)
                    std::cerr << diagnostic.code << '\n';
            }
            minuteOracle(p, result);
            std::reverse(p.tasks.begin(), p.tasks.end());
            const auto reversed = calculate(p);
            check(reversed.status == CalculationStatus::Valid, "reordered forecast failed");
            for (std::size_t i = 0; i < result.tasks.size(); ++i)
                check(result.tasks[i] == reversed.tasks[result.tasks.size() - i - 1],
                      "row order changed float or dates");
        }
}
void constraintsAndCalendars() {
    auto p = fixture(constraintProfile);
    startFixture(p);
    p.tasks[3].constraintType = ConstraintType::SNLT;
    p.tasks[3].constraintAt = "2026-11-03 13:00";
    auto r = calculate(p);
    check(r.status == CalculationStatus::Valid && r.tasks[3].totalFloatMinutes == 180, "upper start did not cap float");
    p.tasks[3].constraintType = ConstraintType::MSO;
    p.tasks[3].constraintAt = "2026-11-03 09:00";
    check(calculate(p).tasks[3].critical, "fixed start was allowed to move");
    p.progress.push_back(report(p, "D", 480, "2026-11-02 09:00"));
    p.tasks[3].constraintAt = "2026-11-02 09:00";
    r = calculate(p);
    check(r.status == CalculationStatus::Valid && r.tasks[3].totalFloatMinutes == 480 &&
              r.tasks[3].start == "2026-11-02 09:00",
          "started MSO incorrectly fixed remaining resume");
    p.progress.pop_back();
    p.tasks[3].constraintType = ConstraintType::ASAP;
    p.tasks[3].constraintAt.reset();
    p.tasks[3].deadlineAt = "2026-11-02 18:00";
    r = calculate(p);
    check(r.tasks[3].totalFloatMinutes == 480 && std::any_of(r.diagnostics.begin(), r.diagnostics.end(),
                                                             [](const auto &d) { return d.code == "DEADLINE_MISSED"; }),
          "native deadline changed CPM float or lost warning");
    p.tasks[3].constraintType = ConstraintType::FNLT;
    p.tasks[3].constraintAt = "2026-11-02 18:00";
    r = calculate(p);
    check(r.status == CalculationStatus::Conflict && !r.projectFinish && !r.forecastFloatCalculated,
          "conflict published future float");

    p = fixture(pdmProfile);
    startFixture(p);
    p.calendars = {{"pm", "Afternoon", {1, 2, 3, 4, 5}, {{780, 1080}}, {}}};
    p.tasks[3].calendarId = "pm";
    p.tasks[3].durationMinutes = 300;
    r = calculate(p);
    check(r.tasks[3].totalFloatMinutes == 300 && r.tasks[3].freeFloatMinutes == 300,
          "mixed-calendar float used project hours");
    p.holidays = {"2026-11-03"};
    p.calendars[0].holidays = p.holidays;
    r = calculate(p);
    check(r.projectFinish == "2026-11-05 18:00" && r.tasks[3].totalFloatMinutes == 300, "holiday float mismatch");

    p = fixture(effortProfile);
    startFixture(p);
    p.calendars = {{"pm", "Afternoon", {1, 2, 3, 4, 5}, {{780, 1080}}, {}}};
    Resource resource{"R", "Team", "labor", 100, {}};
    resource.calendarId = "pm";
    p.resources = {resource};
    p.assignments = {{"D", "R", 100}};
    p.tasks[3].taskType = TaskType::FixedWork;
    p.tasks[3].workDriven = true;
    p.tasks[3].workUnitMinutes = 30000;
    r = calculate(p);
    check(r.status == CalculationStatus::Valid && r.tasks[3].finish == "2026-11-03 18:00" &&
              r.tasks[3].totalFloatMinutes == 480 && TaskExecution(p, p.tasks[3]).work(r.tasks[3]) == 30000,
          "remaining work/resource calendar did not preserve amount or task-calendar float");
    auto actualWork = report(p, "D", 0, "2026-11-02 13:00");
    actualWork.remainingWorkUnitMinutes = 30000;
    p.progress.push_back(actualWork);
    r = calculate(p);
    check(r.status == CalculationStatus::Valid && r.tasks[3].start == "2026-11-02 13:00" &&
              r.tasks[3].totalFloatMinutes == 480,
          "actual work start was moved or used as remaining float origin");
    p.progress.pop_back();
    p.tasks[3].taskType = TaskType::FixedDuration;
    p.tasks[3].workDriven = false;
    p.tasks[3].workUnitMinutes.reset();
    p.tasks[3].breaks = {{"2026-11-03 12:00", "2026-11-04 13:00"}};
    r = calculate(p);
    check(r.status == CalculationStatus::Valid && r.tasks[3].finish == "2026-11-04 18:00" && r.tasks[3].critical,
          "manual break ignored in future criticality");
    for (const auto profile : {pdmProfile, constraintProfile, effortProfile}) {
        p = fixture(profile);
        p.statusAt = "2026-11-02 18:00";
        p.calendars = {{"pm", "Afternoon", {1, 2, 3, 4, 5}, {{780, 1080}}, {}}};
        p.tasks = {{"A", "A", 480, {}}, {"B", "B", 300, {"A"}}, {"C", "C", 960, {}}};
        p.tasks[1].calendarId = "pm";
        r = calculate(p);
        check(r.status == CalculationStatus::Valid && r.tasks[0].totalFloatMinutes == 180 &&
                  r.tasks[0].freeFloatMinutes == 180 && r.tasks[1].critical && r.tasks[2].critical,
              "mixed successor calendar did not preserve the lunch boundary");
        p.tasks[1].dependencyRules = {{"A", DependencyType::FS, -60}};
        r = calculate(p);
        check(r.status == CalculationStatus::Valid && r.tasks[0].totalFloatMinutes == 240 &&
                  r.tasks[0].freeFloatMinutes == 180 && r.tasks[1].totalFloatMinutes == 60,
              "mixed-calendar negative lag lost total/free float distinction");
    }
}
void lifecycle() {
    QTemporaryDir temporary;
    JsonProjectStore store;
    Document doc(store);
    doc.create("Float lifecycle", "2026-11-02", {"A", "B", "C", "D"});
    for (std::size_t i = 0; i < 4; ++i)
        doc.setDuration(i, 480);
    doc.setPredecessors(1, {doc.project()->tasks[0].id});
    doc.setPredecessors(2, {doc.project()->tasks[1].id});
    doc.captureBaseline("Approved", "PM", "initial", "2026-11-02T00:00:00Z");
    const auto baseline = doc.project()->baselines[0];
    doc.setStatusAt("2026-11-02 18:00");
    doc.recordProgress(report(*doc.project(), doc.project()->tasks[0].id, 0, "2026-11-02 09:00", "2026-11-02 18:00"));
    const auto ledger = doc.project()->progress;
    const auto before = calculate(*doc.project());
    check(before.forecastFloatCalculated, "lifecycle forecast unavailable");
    const auto checkpoint = doc.recoverySnapshot("2026-11-02T10:00:00Z");
    doc.saveAs((temporary.path() + "/forecast.ppm").toStdString());
    Document reopened(store);
    reopened.open(doc.path(), true);
    check(calculate(*reopened.project()).tasks == before.tasks && reopened.project()->baselines[0] == baseline &&
              reopened.project()->progress == ledger,
          "file roundtrip changed future float or frozen history");
    Document recovered(store);
    recovered.recover(checkpoint);
    check(calculate(*recovered.project()).tasks == before.tasks && recovered.project()->baselines[0] == baseline,
          "recovery changed future float or baseline");
    doc.setDuration(3, 960);
    check(calculate(*doc.project()).tasks[3].critical, "duration edit did not recompute future float");
    doc.undo();
    check(calculate(*doc.project()).tasks == before.tasks && doc.project()->progress == ledger &&
              doc.project()->baselines[0] == baseline,
          "undo changed ledger or did not restore float");
    doc.applyCalculation(before);
    check(doc.calculation()->status == CalculationStatus::Stale, "old forecast accepted after revision change");
    verifyBaseline(baseline);
    ReportOptions options;
    options.reportId = "forecast-lifecycle";
    options.generatedAt = "2026-11-02T10:00:00Z";
    const auto reportResult = buildReport(*doc.project(), options);
    const auto table = std::find_if(reportResult.tables.begin(), reportResult.tables.end(),
                                    [](const auto &t) { return t.id == "forecast"; });
    check(table != reportResult.tables.end() && std::holds_alternative<std::monostate>(table->rows[0][10].value) &&
              std::get<std::int64_t>(table->rows[3][10].value) == 480 &&
              std::get<std::int64_t>(table->rows[3][11].value) == 480,
          "report lost float or showed actual float as zero");
}
void cancellation() {
    auto p = fixture(pdmProfile);
    startFixture(p);
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    const auto cancelled = calculate(p, control);
    check(cancelled.status == CalculationStatus::Cancelled && cancelled.tasks.empty() && !cancelled.projectFinish &&
              !cancelled.forecastFloatCalculated,
          "cancelled forecast published dates/float");
    // Exercise cancellation inside the remaining backward pass without racing
    // against the initial plan's separate backward pass.
    p.tasks.clear();
    p.progress.clear();
    for (int i = 0; i < 5000; ++i)
        p.tasks.push_back({"T" + std::to_string(i), "Task", 60, {}});
    auto forward = calculate(p);
    forward.forecastFloatCalculated = false;
    WorkCalendar cal(p, p.tasks[0]);
    std::vector<RemainingExecution> models;
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < p.tasks.size(); ++i) {
        order.push_back(i);
        models.push_back({&cal,
                          [&](std::int64_t local) {
                              TaskSchedule r;
                              r.start = cal.boundary(local, false);
                              r.finish = cal.boundary(local + 60, true);
                              return r;
                          },
                          [&](const std::string &at) { return cal.offset(at); },
                          [&](const std::string &at, bool) { return cal.startAtLeast(at); }});
    }
    control = std::make_shared<CalculationControl>();
    auto worker = std::async(std::launch::async, [&] {
        EvaluationScope evaluation(control);
        try {
            calculateRemainingFloat(p, forward, order, models);
            return false;
        } catch (const CalculationCancelled &) {
            return true;
        }
    });
    while (control->stage.load() != CalculationStage::Backward &&
           worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        std::this_thread::yield();
    control->cancel();
    check(worker.get() && !forward.forecastFloatCalculated, "remaining backward pass ignored cancellation");
    check(calculate(fixture(pdmProfile)).status == CalculationStatus::Valid,
          "fresh calculation failed after cancellation");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        goldenCases();
        generatedCases();
        constraintsAndCalendars();
        lifecycle();
        cancellation();
        std::cout << "forecast float: golden/oracle/constraints/calendars/history/cancellation passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
