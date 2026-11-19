#include "ExpandedRepartitionFixture.h"
#include "RepartitionFloatFixture.h"
#include "calculation/LevelingFloat.h"
#include "calculation/Resources.h"
#include "calculation/Tracking.h"
#include "persistence/JsonProjectStore.h"
#include "reporting/ReportExport.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <iostream>
#include <map>
#include <tuple>
using namespace ppm;
namespace {
void check(bool v, const std::string &m) {
    if (!v)
        throw std::runtime_error(m);
}
struct Trial {
    ScheduleResult result;
    double ms;
    std::uint64_t checkpoints;
};
Trial run(const Project &p, int version) {
    LevelingFloatScope policy(version);
    auto r = calculatePlan(p);
    check(r.status == CalculationStatus::Valid, "invalid fixture");
    r.resourceLeveled = true;
    auto control = std::make_shared<CalculationControl>();
    QElapsedTimer time;
    time.start();
    {
        EvaluationScope evaluation(control);
        calculateLevelingFloat(p, r);
    }
    return {std::move(r), double(time.nsecsElapsed()) / 1000000, control->checkpoints.load()};
}
Project resourceOnly(int count) {
    auto p = expanded_fixture::chain(count);
    WorkCalendar clock(p, Task{});
    for (int i = 0; i < count; ++i) {
        p.tasks[i].predecessors.clear();
        p.tasks[i].constraintType = ConstraintType::SNET;
        p.tasks[i].constraintAt = clock.boundary(i, false);
    }
    return p;
}
Project tiny() {
    Project p;
    p.documentId = "tiny-propagation";
    p.name = "비단조 용량과 분기 복구";
    p.startDate = "2026-11-02";
    p.calculationProfile = effortProfile;
    TaskCalendar cal;
    cal.id = "tiny";
    cal.name = "하루4분";
    cal.periods = {{540, 544}};
    p.calendars = {cal};
    p.tasks = {{"A", "A", 2, {}}, {"B", "B", 2, {}}, {"C", "C", 2, {}}, {"H", "완료일", 40, {}}};
    for (auto &t : p.tasks) {
        t.calendarId = "tiny";
        p.taskCosts.push_back({t.id, 0});
    }
    Resource r;
    r.id = "R";
    r.name = "자원";
    r.calendarId = "tiny";
    r.rates = {{p.startDate, 60}};
    r.exceptions = {{"2026-11-03", 50}, {"2026-11-04", 0}, {"2026-11-13", 50}};
    p.resources = {r};
    p.assignments = {{"A", "R", 50}, {"B", "R", 50}, {"C", "R", 50}};
    return p;
}
std::vector<std::uint64_t> stats(const ScheduleResult &r) {
    for (const auto &d : r.diagnostics)
        if (d.code == "LEVELING_REPARTITION_PROPAGATION_POLICY") {
            std::vector<std::uint64_t> out;
            for (const auto &a : d.arguments)
                out.push_back(std::stoull(a));
            return out;
        }
    return {};
}
QJsonObject encode(const ScheduleResult &r) {
    QJsonArray rows, records, proofs;
    std::map<std::tuple<std::string, std::string, std::string>, int> ids;
    for (const auto &x : r.tasks)
        rows.append(QJsonObject{{"id", QString::fromStdString(x.taskId)},
                                {"start", QString::fromStdString(x.start)},
                                {"finish", QString::fromStdString(x.finish)},
                                {"total", double(x.totalFloatMinutes)},
                                {"free", double(x.freeFloatMinutes)},
                                {"critical", x.critical}});
    if (r.levelingWitnesses)
        for (const auto &w : r.levelingWitnesses->tasks) {
            QJsonArray dates;
            for (const auto &d : w.dates) {
                const auto [it, added] = ids.emplace(std::tuple{d.taskId, d.start, d.finish}, records.size());
                if (added)
                    records.append(QJsonObject{{"id", QString::fromStdString(d.taskId)},
                                               {"start", QString::fromStdString(d.start)},
                                               {"finish", QString::fromStdString(d.finish)}});
                dates.append(it->second);
            }
            proofs.append(QJsonObject{{"id", QString::fromStdString(w.taskId)}, {"dates", dates}});
        }
    QJsonArray counters;
    for (const auto v : stats(r))
        counters.append(double(v));
    return {{"rows", rows}, {"date_records", records}, {"proofs", proofs}, {"counters", counters}};
}
QJsonObject paired(const std::string &name, const Project &p, int repeats) {
    QJsonArray oldTimes, newTimes, oldChecks, newChecks;
    ScheduleResult last;
    bool oldAvailable = true;
    for (int i = 0; i < repeats; ++i) {
        Trial a, b;
        if (i % 2) {
            b = run(p, 6);
            a = run(p, 5);
        } else {
            a = run(p, 5);
            b = run(p, 6);
        }
        check(b.result.levelingFloatCalculated, b.result.levelingFloatReason);
        oldAvailable = a.result.levelingFloatCalculated;
        if (oldAvailable)
            check(a.result.tasks == b.result.tasks, "1.5/1.6 numerical mismatch: " + name);
        else
            check(a.result.levelingFloatReason.find("한도") != std::string::npos, "unexpected old failure");
        for (std::size_t k = 0; k < a.result.tasks.size(); ++k)
            check(a.result.tasks[k].start == b.result.tasks[k].start &&
                      a.result.tasks[k].finish == b.result.tasks[k].finish,
                  "forward dates changed");
        oldTimes.append(a.ms);
        newTimes.append(b.ms);
        oldChecks.append(double(a.checkpoints));
        newChecks.append(double(b.checkpoints));
        last = std::move(b.result);
    }
    const auto counters = stats(last);
    check(counters.size() == 6, "missing propagation counters");
    if (name == "resource-60" || name == "resource-750")
        check(counters[0] > 0 && counters[1] == 3 * (p.tasks.size() - 1), "resource bounds did not shrink generation");
    if (name == "coupled-256" || name == "separate-1000")
        check(counters[3] == 1 && counters[4] > 0, "native certificates were not shared");
    if (name == "resource-750")
        check(!oldAvailable, "rescue fixture no longer exceeds legacy budget");
    auto result = encode(last);
    result["name"] = QString::fromStdString(name);
    result["legacy_available"] = oldAvailable;
    result["legacy_ms"] = oldTimes;
    result["optimized_ms"] = newTimes;
    result["legacy_checkpoints"] = oldChecks;
    result["optimized_checkpoints"] = newChecks;
    std::cout << name << " completed; legacy available=" << oldAvailable << ", certificates=" << counters[3] << '\n';
    return result;
}
QJsonObject nonmonotone() {
    const auto p = tiny();
    LevelingFloatScope old(5);
    const auto a = levelResources(p, "task-order", 260).leveled;
    LevelingFloatScope current(6);
    const auto b = levelResources(p, "task-order", 260).leveled;
    check(a.levelingFloatCalculated && b.levelingFloatCalculated && a.tasks == b.tasks,
          "nonmonotone capacity or restoration changed values");
    const auto counters = stats(b);
    check(counters.size() == 6 && counters[2] > 0, "branch exclusions not exercised");
    auto repeated = b;
    calculateLevelingFloat(p, repeated);
    check(repeated.tasks == b.tasks && stats(repeated) == counters, "repeated propagation changed state");
    auto mismatch = p;
    mismatch.resources[0].exceptions.push_back({"2026-11-12", {}});
    const auto unknown = levelResources(mismatch, "task-order", 260).leveled;
    check(!unknown.levelingFloatCalculated && !unknown.levelingWitnesses,
          "unknown capacity published partial certificates");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope evaluation(control);
        calculateRepartitionLevelingFloat(p, repeated);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled && repeated.tasks == b.tasks, "cancelled propagation changed results");
    auto selected = p;
    LevelingSelection choice;
    choice.policy = levelingPolicyFor(p);
    choice.adoptedAt = "2026-11-04T00:00:00Z";
    choice.reason = "전파 검증";
    choice.sourceDocumentId = p.documentId;
    choice.sourceRevision = p.revision;
    selected.planLeveling = choice;
    ReportOptions options;
    options.reportId = "propagation";
    options.generatedAt = "2026-11-04T00:00:00Z";
    QFile::remove("candidate-propagation-qa/report.xlsx");
    exportXlsx(buildReport(selected, options), "candidate-propagation-qa/report.xlsx");
    return encode(b);
}
void archive() {
    auto p = resourceOnly(60);
    LevelingSelection s;
    s.policy = levelingPolicyFor(p);
    s.adoptedAt = "2026-11-04T00:00:00Z";
    s.reason = "전파 채택";
    s.sourceDocumentId = p.documentId;
    s.sourceRevision = p.revision;
    p.planLeveling = s;
    LevelingFloatScope old(5);
    auto a = prepareBaseline(p, "1.5 보존", "PM", "검토", "2026-11-04T00:00:00Z");
    a.id = "old-5";
    a.version = 1;
    p.baselines.push_back(a);
    ++p.revision;
    LevelingFloatScope current(6);
    auto b = prepareBaseline(p, "1.6 인증", "PM", "검토", "2026-11-04T00:00:00Z");
    b.id = "new-6";
    b.version = 2;
    p.baselines.push_back(b);
    ++p.revision;
    check(a.engineVersion == "ppm-adopted-leveling-1.5" && b.engineVersion == "ppm-adopted-leveling-1.6" &&
              a.schedule == b.schedule,
          "archive engine replay changed");
    verifyBaseline(a);
    verifyBaseline(b);
    JsonProjectStore store;
    QFile::remove("candidate-propagation-qa/selected.ppm");
    store.save("candidate-propagation-qa/selected.ppm", p, {});
    const auto reopened = store.load("candidate-propagation-qa/selected.ppm");
    check(reopened.project.baselines == p.baselines, "archive reopen mismatch");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    LevelingFloatScope historical(6);
    try {
        QDir().mkpath("candidate-propagation-qa");
        QJsonArray cases;
        cases.append(paired("resource-60", resourceOnly(60), 3));
        cases.append(paired("coupled-256", expanded_fixture::chain(256), 3));
        cases.append(paired("separate-1000", expanded_fixture::chain(1000, 2, true), 3));
        cases.append(paired("resource-750", resourceOnly(750), 1));
        const auto small = nonmonotone();
        archive();
        QFile f("candidate-propagation-qa/metrics.json");
        check(f.open(QIODevice::WriteOnly), "metrics write");
        f.write(
            QJsonDocument(QJsonObject{{"engine", "ppm-adopted-leveling-1.6"}, {"cases", cases}, {"nonmonotone", small}})
                .toJson(QJsonDocument::Compact));
        std::cout << "Candidate bounds, reversible propagation, native certificates, legacy replay and paired "
                     "measurements passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
