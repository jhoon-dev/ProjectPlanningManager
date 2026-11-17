#include "NonmonotoneFixture.h"
#include "RepartitionFloatFixture.h"
#include "calculation/LevelingFloat.h"
#include "calculation/Resources.h"
#include "calculation/PropagationCost.h"
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
#include <iostream>
using namespace jobs;
using namespace nonmonotone_fixture;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
QJsonArray counters(const ScheduleResult &r) {
    for (const auto &d : r.diagnostics)
        if (d.code == "LEVELING_PROPAGATION_COST_POLICY") {
            QJsonArray a;
            for (const auto &v : d.arguments)
                a.append(double(std::stoull(v)));
            return a;
        }
    return {};
}
QJsonObject encode(const ScheduleResult &r) {
    QJsonArray rows, proofs, links;
    for (const auto &x : r.tasks)
        rows.append(QJsonObject{{"id", QString::fromStdString(x.taskId)},
                                {"start", QString::fromStdString(x.start)},
                                {"finish", QString::fromStdString(x.finish)},
                                {"total", double(x.totalFloatMinutes)},
                                {"free", double(x.freeFloatMinutes)},
                                {"critical", x.critical}});
    check(bool(r.levelingWitnesses), "missing structured witnesses");
    for (const auto &w : r.levelingWitnesses->tasks) {
        QJsonArray dates;
        for (const auto &d : w.dates)
            dates.append(QJsonObject{{"id", QString::fromStdString(d.taskId)},
                                     {"start", QString::fromStdString(d.start)},
                                     {"finish", QString::fromStdString(d.finish)}});
        proofs.append(QJsonObject{{"id", QString::fromStdString(w.taskId)}, {"dates", dates}});
    }
    for (const auto &link : r.resourceOrderLinks)
        links.append(QJsonArray{QString::fromStdString(link.predecessorId), QString::fromStdString(link.taskId),
                                QString::fromStdString(link.resourceId)});
    QJsonArray pruning;
    for (const auto &d : r.diagnostics)
        if (d.code == "LEVELING_NONMONOTONE_PROPAGATION_POLICY")
            for (const auto &v : d.arguments)
                pruning.append(double(std::stoull(v)));
    return {{"pruning", pruning}, {"rows", rows}, {"proofs", proofs}, {"links", links}, {"counters", counters(r)}};
}
struct Trial {
    ScheduleResult result;
    double ms;
    std::uint64_t checkpoints;
};
Trial run(const Project &p, ScheduleResult initial, int version) {
    LevelingFloatScope policy(version);
    const auto control = std::make_shared<CalculationControl>();
    QElapsedTimer time;
    time.start();
    {
        EvaluationScope evaluation(control);
        calculateLevelingFloat(p, initial);
    }
    return {std::move(initial), double(time.nsecsElapsed()) / 1000000, control->checkpoints.load()};
}
QJsonObject paired(const std::string &name, const Project &p, int repeats) {
    LevelingFloatScope old(7);
    const auto initial = levelResources(p, "task-order", 260).leveled;
    if (initial.status != CalculationStatus::Valid || !initial.levelingFloatCalculated) {
        for (const auto &d : initial.diagnostics)
            std::cerr << name << ": " << d.code << '\n';
        check(false, name + ": initial schedule: " + initial.levelingFloatReason);
    }
    QJsonArray before, after, beforeChecks, afterChecks;
    ScheduleResult final;
    for (int i = 0; i < repeats; ++i) {
        Trial a, b;
        if (i % 2) {
            b = run(p, initial, 8);
            a = run(p, initial, 7);
        } else {
            a = run(p, initial, 7);
            b = run(p, initial, 8);
        }
        check(a.result.levelingFloatCalculated && b.result.levelingFloatCalculated, b.result.levelingFloatReason);
        check(a.result.tasks == b.result.tasks, "1.7/1.8 values changed: " + name);
        check(resourceLoad(p, b.result).status == CalculationStatus::Valid, "current load changed");
        before.append(a.ms);
        after.append(b.ms);
        beforeChecks.append(double(a.checkpoints));
        afterChecks.append(double(b.checkpoints));
        final = std::move(b.result);
    }
    auto result = encode(final);
    if (name == "holes-96")
        check(result["pruning"].toArray()[0].toDouble() == 11136, "large mandatory pruning regressed");
    if (name == "fractional" || name == "fractional-long" || name == "varying" || name == "holes-small" ||
        name == "late-tiny")
        check(result["pruning"].toArray()[5].toDouble() == 0, "small case spent optional profile budget");
    check(counters(final).size() == 7, "missing cost diagnostics");
    if (name == "fractional" || name == "fractional-long" || name == "varying" || name == "holes-small" ||
        name == "late-tiny")
        check(counters(final)[0].toDouble() > 0 && counters(final)[3].toDouble() == 0,
              "cheap exact search unnecessarily enabled profiles");
    if (name == "adaptive-observed")
        check(counters(final)[3].toDouble() > 0 && counters(final)[4].toDouble() > 0,
              "observed activation / low-yield backoff not exercised");
    auto repeated = run(p, final, 8).result;
    check(repeated.tasks == final.tasks && counters(repeated) == counters(final), "repeat changed propagation state");
    result["name"] = QString::fromStdString(name);
    result["legacy_ms"] = before;
    result["optimized_ms"] = after;
    result["legacy_checkpoints"] = beforeChecks;
    result["optimized_checkpoints"] = afterChecks;
    std::cout << name << ": " << QJsonDocument(counters(final)).toJson(QJsonDocument::Compact).constData() << '\n';
    return result;
}
void decisions() {
    check(!PropagationCost{5, 24, 0, 0}.worthwhile() && PropagationCost{5, 25, 0, 0}.worthwhile(),
          "cost crossover boundary");
    check(!PropagationCost{5, 25, 100, 0}.worthwhile() && !PropagationCost{5, 25, 0, 100}.worthwhile(),
          "span/edge work ignored");
    check(!PropagationCost{1, 10000, 0, 0}.worthwhile() && !PropagationCost{}.worthwhile(), "fixed/single unit gate");
    const PropagationCost large{10000, 250000, 2000000, 500000};
    check(large.sweep() == 2000000 && large.repeatSavings() == 2000000, "bounded estimate");
    PropagationDecision decision({2, 10, 0, 0});
    for (int i = 0; i < 63; ++i)
        decision.failedProbe();
    check(!decision.observe() && !decision.active(), "activation before evidence");
    decision.failedProbe();
    check(decision.observe() && decision.active(), "observed activation");
    check(!decision.feedback(100, 13) && decision.active(), "useful pruning disabled");
    check(decision.feedback(100, 0) && !decision.active(), "low-yield retry not delayed");
    for (int i = 0; i < 255; ++i)
        decision.failedProbe();
    check(!decision.observe(), "backoff boundary");
    decision.failedProbe();
    check(decision.observe(), "retry after fresh evidence");
    PropagationDecision late({6, 60, 48, 0});
    for (int goal = 0; goal < 4; ++goal) {
        for (int i = 0; i < 48; ++i)
            late.failedProbe();
        late.completedGoal();
    }
    for (int i = 0; i < 16; ++i)
        late.failedProbe();
    check(!late.observe() && !late.active(), "sunk work enabled a late sweep");
    PropagationDecision deferred({2, 10, 0, 0}, false);
    for (int i = 0; i < 64; ++i)
        deferred.failedProbe();
    check(deferred.needsEstimate() && !deferred.observe(), "unrefined bound activated propagation");
    deferred.refine({2, 10, 100, 0});
    check(!deferred.needsEstimate() && !deferred.observe(), "refined cost ignored");
    for (int i = 0; i < 246; ++i)
        deferred.failedProbe();
    check(deferred.observe(), "refined observed activation failed");
}
void archive() {
    auto p = holes(2, 8);
    LevelingSelection choice;
    choice.policy = levelingPolicyFor(p);
    choice.adoptedAt = "2026-11-04T00:00:00Z";
    choice.reason = "전파 비용 정책";
    choice.sourceDocumentId = p.documentId;
    choice.sourceRevision = p.revision;
    p.planLeveling = choice;
    LevelingFloatScope old(7);
    auto a = prepareBaseline(p, "1.7 보존", "PM", "검토", "2026-11-04T00:00:00Z");
    a.id = "old-7";
    a.version = 1;
    p.baselines.push_back(a);
    ++p.revision;
    LevelingFloatScope current(8);
    auto b = prepareBaseline(p, "1.8 비용", "PM", "검토", "2026-11-04T00:00:00Z");
    b.id = "new-8";
    b.version = 2;
    p.baselines.push_back(b);
    ++p.revision;
    check(a.engineVersion == "jobs-adopted-leveling-1.7" && b.engineVersion == "jobs-adopted-leveling-1.8" &&
              a.schedule == b.schedule,
          "baseline replay changed");
    verifyBaseline(a);
    verifyBaseline(b);
    JsonProjectStore store;
    QFile::remove("propagation-cost-qa/selected.ppm");
    store.save("propagation-cost-qa/selected.ppm", p, {});
    check(store.load("propagation-cost-qa/selected.ppm").project.baselines == p.baselines, "native reopen");
    ReportOptions options;
    options.reportId = "propagation-cost";
    options.generatedAt = "2026-11-04T00:00:00Z";
    QFile::remove("propagation-cost-qa/report.xlsx");
    exportXlsx(buildReport(p, options), "propagation-cost-qa/report.xlsx");
    auto r = calculatePlan(p);
    const auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope evaluation(control);
        calculateLevelingFloat(p, r);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled && !r.levelingWitnesses, "cancel published witness");
    auto unknown = varying();
    unknown.resources[0].exceptions.push_back({"2026-11-12", {}});
    const auto invalid = levelResources(unknown, "task-order", 260).leveled;
    check(!invalid.levelingFloatCalculated && !invalid.levelingWitnesses, "unknown capacity was estimated away");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QDir().mkpath("propagation-cost-qa");
        decisions();
        QJsonArray cases;
        cases.append(paired("holes-small", holes(2, 8), 5));
        cases.append(paired("fractional", fractional(), 5));
        cases.append(paired("fractional-long", fractional(16), 5));
        cases.append(paired("varying", varying(), 5));
        cases.append(paired("holes-96", holes(96), 3));
        cases.append(paired("late-tiny", holes(6, 12), 5));
        cases.append(paired("adaptive-observed", holes(15, 12), 3));
        cases.append(paired("split", repartition_fixture::project(), 3));
        archive();
        QFile file("propagation-cost-qa/metrics.json");
        check(file.open(QIODevice::WriteOnly), "metrics write");
        file.write(QJsonDocument(QJsonObject{{"engine", "jobs-adopted-leveling-1.8"}, {"cases", cases}})
                       .toJson(QJsonDocument::Compact));
        std::cout << "Cost estimates, observed activation, backoff, exact witnesses and archive passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
