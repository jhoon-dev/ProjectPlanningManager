#include "calculation/LevelingFloat.h"
#include "calculation/Resources.h"
#include "calculation/MandatoryDemand.h"
#include "calculation/Tracking.h"
#include "ProgressPolicyFixture.h"
#include "RepartitionFloatFixture.h"
#include "NonmonotoneFixture.h"
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
#include <random>
using namespace ppm;
using namespace nonmonotone_fixture;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
QJsonArray counters(const ScheduleResult &r) {
    for (const auto &d : r.diagnostics)
        if (d.code == "LEVELING_NONMONOTONE_PROPAGATION_POLICY") {
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
    return {{"rows", rows}, {"proofs", proofs}, {"links", links}, {"counters", counters(r)}};
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
    LevelingFloatScope old(6);
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
            b = run(p, initial, 7);
            a = run(p, initial, 6);
        } else {
            a = run(p, initial, 6);
            b = run(p, initial, 7);
        }
        check(a.result.levelingFloatCalculated && b.result.levelingFloatCalculated, b.result.levelingFloatReason);
        check(a.result.tasks == b.result.tasks, "1.6/1.7 values changed: " + name);
        check(resourceLoad(p, b.result).status == CalculationStatus::Valid, "current load changed");
        before.append(a.ms);
        after.append(b.ms);
        beforeChecks.append(double(a.checkpoints));
        afterChecks.append(double(b.checkpoints));
        final = std::move(b.result);
    }
    auto result = encode(final);
    check(counters(final).size() == 6 && counters(final)[5].toDouble() <= 50000, "new counters/budget missing");
    if (name.starts_with("holes"))
        check(counters(final)[0].toDouble() > 0, "mandatory aggregate load did not prune candidates");
    if (name == "varying")
        check(counters(final)[1].toDouble() > 0 && counters(final)[2].toDouble() > 0,
              "conditional support/load exclusions not exercised");
    if (name == "fractional-long")
        check(counters(final)[1].toDouble() > 0, "fractional live supports not exercised");
    auto repeated = run(p, final, 7).result;
    check(repeated.tasks == final.tasks && counters(repeated) == counters(final), "repeat changed propagation state");
    result["name"] = QString::fromStdString(name);
    result["legacy_ms"] = before;
    result["optimized_ms"] = after;
    result["legacy_checkpoints"] = beforeChecks;
    result["optimized_checkpoints"] = afterChecks;
    std::cout << name << ": " << QJsonDocument(counters(final)).toJson(QJsonDocument::Compact).constData() << '\n';
    return result;
}
void primitive() {
    struct Span {
        std::size_t resource;
        std::int64_t start, finish;
        int units;
    };
    std::mt19937 random(7717);
    for (int test = 0; test < 200; ++test) {
        std::vector<std::vector<Span>> alternatives(3);
        int expected[3][3][16] = {};
        for (int c = 0; c < 3; ++c)
            for (int s = 0; s < 10; ++s) {
                const auto r = random() % 3;
                const auto start = std::int64_t(random() % 15);
                const auto end = start + 1 + std::int64_t(random() % (16 - start));
                const int units = 1 + random() % 100;
                alternatives[c].push_back({r, start, end, units});
                for (auto at = start; at < end; ++at)
                    expected[c][r][at] += units;
            }
        auto lower = normalizeDemand(alternatives[0], [] {});
        for (int c = 1; c < 3; ++c)
            lower = intersectDemand(lower, normalizeDemand(alternatives[c], [] {}), [] {});
        for (int r = 0; r < 3; ++r)
            for (int at = 0; at < 16; ++at) {
                int got = 0;
                for (const auto &s : lower)
                    if (s.resource == std::size_t(r) && s.start <= at && at < s.finish)
                        got += s.units;
                check(got == std::min({expected[0][r][at], expected[1][r][at], expected[2][r][at]}),
                      "pointwise minimum failed overlap/gap/units oracle");
            }
        check(intersectDemand(lower, std::vector<Span>{}, [] {}).empty(), "absent resource is not zero");
    }
}
QJsonArray archiveAndFailures() {
    auto p = holes(2, 8);
    LevelingSelection selection;
    selection.policy = levelingPolicyFor(p);
    selection.adoptedAt = "2026-11-04T00:00:00Z";
    selection.reason = "비단조 전파 검증";
    selection.sourceDocumentId = p.documentId;
    selection.sourceRevision = p.revision;
    p.planLeveling = selection;
    LevelingFloatScope old(6);
    auto a = prepareBaseline(p, "1.6 보존", "PM", "검토", "2026-11-04T00:00:00Z");
    a.id = "old-6";
    a.version = 1;
    p.baselines.push_back(a);
    ++p.revision;
    LevelingFloatScope current(7);
    auto b = prepareBaseline(p, "1.7 전파", "PM", "검토", "2026-11-04T00:00:00Z");
    b.id = "new-7";
    b.version = 2;
    p.baselines.push_back(b);
    ++p.revision;
    check(a.engineVersion == "ppm-adopted-leveling-1.6" && b.engineVersion == "ppm-adopted-leveling-1.7" &&
              a.schedule == b.schedule,
          "baseline replay changed");
    verifyBaseline(a);
    verifyBaseline(b);
    JsonProjectStore store;
    QFile::remove("nonmonotone-propagation-qa/selected.ppm");
    store.save("nonmonotone-propagation-qa/selected.ppm", p, {});
    check(store.load("nonmonotone-propagation-qa/selected.ppm").project.baselines == p.baselines, "native reopen");
    ReportOptions options;
    options.reportId = "nonmonotone";
    options.generatedAt = "2026-11-04T00:00:00Z";
    QFile::remove("nonmonotone-propagation-qa/report.xlsx");
    exportXlsx(buildReport(p, options), "nonmonotone-propagation-qa/report.xlsx");
    auto source = varying();
    auto out = levelResources(source, "task-order", 260).leveled;
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool cancelled = false;
    try {
        EvaluationScope evaluation(control);
        calculateLevelingFloat(source, out);
    } catch (const CalculationCancelled &) {
        cancelled = true;
    }
    check(cancelled && !out.levelingWitnesses, "cancel published stale/partial witness");
    source.resources[0].exceptions.push_back({"2026-11-12", {}});
    const auto unknown = levelResources(source, "task-order", 260).leveled;
    check(!unknown.levelingFloatCalculated && !unknown.levelingWitnesses, "unknown demand published witness");
    auto forecast = fractional(16);
    forecast.statusAt = "2026-11-02 09:01";
    auto fact = progress_fixture::entry(forecast, "A", 2, 1, 2, "2026-11-02 09:00");
    fact.remainingWorkUnitMinutes = 151;
    forecast.progress.push_back(fact);
    LevelingFloatScope historical(6);
    const auto prior = levelResources(forecast, "task-order", 260).leveled;
    LevelingFloatScope latest(7);
    const auto next = levelResources(forecast, "task-order", 260).leveled;
    check(prior.levelingFloatCalculated && next.levelingFloatCalculated && prior.tasks == next.tasks &&
              next.tasks[0].start == "2026-11-02 09:00" && next.tasks[0].remainingStart &&
              *next.tasks[0].remainingStart >= *forecast.statusAt,
          "remaining fractional work or actual start changed");
    const auto split = repartition_fixture::project();
    LevelingFloatScope beforeFallback(6);
    const auto oldSplit = levelResources(split, "task-order", 260).leveled;
    LevelingFloatScope afterFallback(7);
    const auto newSplit = levelResources(split, "task-order", 260).leveled;
    const auto fallback = counters(newSplit);
    check(oldSplit.levelingFloatCalculated && newSplit.levelingFloatCalculated && oldSplit.tasks == newSplit.tasks &&
              fallback.size() == 6 && fallback[4].toDouble() > 0 && fallback[5].toDouble() <= 50000,
          "soft propagation limit changed exact split float");
    return fallback;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    LevelingFloatScope historical(7);
    try {
        QDir().mkpath("nonmonotone-propagation-qa");
        primitive();
        QJsonArray cases;
        cases.append(paired("holes-small", holes(2, 8), 1));
        cases.append(paired("holes-32", holes(32), 3));
        cases.append(paired("holes-96", holes(96), 3));
        cases.append(paired("fractional", fractional(), 3));
        cases.append(paired("fractional-long", fractional(16), 3));
        cases.append(paired("varying", varying(), 3));
        const auto fallback = archiveAndFailures();
        QFile file("nonmonotone-propagation-qa/metrics.json");
        check(file.open(QIODevice::WriteOnly), "metrics write");
        file.write(QJsonDocument(QJsonObject{{"engine", "ppm-adopted-leveling-1.7"},
                                             {"cases", cases},
                                             {"primitive_trials", 200},
                                             {"fallback_counters", fallback}})
                       .toJson(QJsonDocument::Compact));
        std::cout << "Nonmonotone mandatory demand, live supports, restoration and legacy contracts passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
