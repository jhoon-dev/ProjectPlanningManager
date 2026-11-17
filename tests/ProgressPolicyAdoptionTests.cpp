#include "ProgressPolicyFixture.h"
#include "application/Document.h"
#include "calculation/ProgressPolicies.h"
#include "calculation/NetworkDiagram.h"
#include "calculation/Usage.h"
#include "domain/Classification.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/JsonRecoveryStore.h"
#include "persistence/ProjectJson.h"
#include "persistence/ProgressPolicySelectionJson.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QTemporaryDir>
#include <algorithm>
#include <future>
#include <iostream>
using namespace jobs;
namespace {
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
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
ProgressPolicySelection selection(const Document &d, const char *policy = overrideProgressPolicy) {
    return {policy, "PM", "2026-11-03T00:00:00Z", "잔여 예측 검토", d.project()->documentId, d.project()->revision};
}
auto prepare(Document &d, EditCommand c,
             std::shared_ptr<CalculationControl> control = std::make_shared<CalculationControl>()) {
    return std::async(std::launch::async, [r = d.makeEditRequest(std::move(c)),
                                           control]() mutable { return Document::prepareEdit(std::move(r), control); })
        .get();
}
void contracts() {
    QTemporaryDir temp;
    JsonProjectStore store;
    auto p = progress_fixture::small();
    Resource resource;
    resource.id = "R";
    resource.name = "인력";
    resource.rates = {{"2026-11-02", 60}, {"2026-11-04", 120}};
    p.resources = {resource};
    p.assignments = {{"B", "R", 100}};
    for (const auto &t : p.tasks)
        p.taskCosts.push_back({t.id, 0});
    const auto path = temp.filePath("input.ppm").toStdString();
    store.save(path, p, {});
    Document d(store);
    d.open(path);
    const auto baseline = d.captureBaseline("원본", "PM", "최초", "2026-11-03T00:00:00Z");
    const auto frozen = d.project()->baselines;
    const auto before = *d.project();
    const auto retained = calculate(before);
    auto prepared = prepare(d, AdoptProgressPolicyEdit{selection(d)});
    check(prepared->state() == EditState::Ready && *d.project() == before, "worker changed source or failed");
    check(d.applyPreparedEdit(*prepared) == EditState::Applied, "commit");
    const auto adopted = *d.project();
    const auto forecast = calculate(adopted);
    check(forecast.status == CalculationStatus::Valid && forecast.tasks[1].finish == "2026-11-03 14:00", "B dates");
    check(d.calculation()->tasks == forecast.tasks && d.calculation()->inputRevision == adopted.revision,
          "published calculation");
    check(adopted.tasks == before.tasks && adopted.progress == before.progress && adopted.baselines == frozen,
          "facts/relations/baselines changed");
    check(calculatePlan(adopted).tasks == calculatePlan(before).tasks, "plan changed");
    check(forecast.calculationPolicy.starts_with("adopted-progress-v1/native-progress-override-v1/"), "provenance");
    check(std::none_of(forecast.diagnostics.begin(), forecast.diagnostics.end(),
                       [](const auto &v) { return v.code == "PROGRESS_OVERRIDE_PREVIEW"; }),
          "preview label in adopted result");
    check(std::any_of(forecast.diagnostics.begin(), forecast.diagnostics.end(),
                      [](const auto &d) {
                          return d.code == "OUT_OF_SEQUENCE" && d.textKey == "PROGRESS_OVERRIDE_OUT_OF_SEQUENCE";
                      }),
          "B diagnostic claims retained logic");
    const auto comparison = compareProgressPolicies(adopted);
    check(comparison.retained.tasks == retained.tasks && comparison.overridden.tasks == forecast.tasks,
          "comparison recursed or A became B");
    const auto usage = buildUsage(adopted, {"2026-11-02", "2026-11-06", baseline, {}, UsagePeriod::Day});
    const auto rows = usageRows(usage, UsageHierarchy::Tasks);
    const auto b = std::find_if(rows.begin(), rows.end(), [](const auto &r) { return r.taskId == "B" && !r.leaf; });
    check(b != rows.end() && usageTotal(b->values[2]).cost == 240, "remaining cost did not follow B effective rate");
    check(analyzeCosts(adopted, baseline).plan == analyzeCosts(before, baseline).plan,
          "plan budget changed with forecast preference");
    const auto graph = buildNetworkGraph(adopted);
    check(graph.edges.size() == 2 && graph.edges[0].driving == NetworkDriving::Inactive,
          "original inactive relation missing");
    auto leveled = levelResources(adopted, "task-order", 260);
    check(leveled.original.status == CalculationStatus::Valid && leveled.before.status == CalculationStatus::Valid &&
              leveled.leveled.status == CalculationStatus::Valid,
          "combined leveling failed");
    LevelingSelection ls;
    ls.policy = levelingPolicyFor(adopted);
    ls.adoptedAt = "2026-11-03T00:00:00Z";
    ls.reason = "test";
    ls.sourceDocumentId = adopted.documentId;
    ls.sourceRevision = adopted.revision;
    auto mixed = adopted;
    mixed.remainingLeveling = ls;
    validate(mixed);
    check(calculate(mixed).resourceLeveled, "combined choice did not route to leveling");
    d.undo();
    check(!d.project()->progressPolicy && calculate(*d.project()).tasks == retained.tasks, "undo");
    d.redo();
    check(d.project()->progressPolicy == adopted.progressPolicy, "redo");
    d.renameTask(0, "새 이름");
    check(d.project()->progressPolicy == adopted.progressPolicy, "ordinary edit discarded choice");
    d.setStatusAt({});
    rejects([&] { d.adoptProgressPolicy(selection(d)); });
    check(d.project()->progressPolicy && !calculate(*d.project()).isForecast &&
              calculate(*d.project()).calculationPolicy.ends_with("/dormant"),
          "dormant choice");
    d.setStatusAt(p.statusAt);
    check(calculate(*d.project()).tasks[1].finish == "2026-11-03 14:00", "reactivation");
    d.save();
    QFile file(QString::fromStdString(path));
    check(file.open(QIODevice::ReadOnly), "read file");
    check(QJsonDocument::fromJson(file.readAll()).object()["schemaVersion"] == 13, "schema13 absent");
    file.close();
    Document reopened(store);
    reopened.open(path, true);
    check(reopened.project()->progressPolicy == d.project()->progressPolicy &&
              calculate(*reopened.project()).tasks == calculate(*d.project()).tasks,
          "reopen");
    rejects([&] { reopened.clearProgressPolicy(); });
    const auto provenance = d.project()->progressPolicy;
    d.saveAs(temp.filePath("copy.ppm").toStdString());
    check(d.project()->progressPolicy == provenance, "SaveAs rewrote evidence");
    d.renameTask(0, "복구할 미저장 이름");
    JsonRecoveryStore recovery(temp.path() + "/recovery");
    auto session = recovery.newSession();
    session->checkpoint(d.recoverySnapshot("2026-11-03T02:00:00Z"));
    session.reset();
    const auto scan = recovery.scan();
    check(scan.candidates.size() == 1 && scan.candidates[0].valid, "schema13 recovery");
    auto claim = recovery.claim(scan.candidates[0]);
    Document recovered(store);
    recovered.recover(claim->snapshot);
    check(recovered.project()->progressPolicy == provenance, "recovery choice");
    const auto current = *d.project();
    auto cancelled = std::make_shared<CalculationControl>();
    cancelled->cancel();
    auto stop = prepare(d, ClearProgressPolicyEdit{}, cancelled);
    check(stop->state() == EditState::Cancelled && *d.project() == current, "cancel mutated source");
    auto control = std::make_shared<CalculationControl>();
    auto ready = prepare(d, ClearProgressPolicyEdit{}, control);
    check(ready->state() == EditState::Ready, "prepare before cancel");
    control->cancel();
    check(d.applyPreparedEdit(*ready) == EditState::Cancelled && *d.project() == current, "cancel before commit");
    auto stale = prepare(d, AdoptProgressPolicyEdit{selection(d, retainedProgressPolicy)});
    d.renameTask(0, "stale");
    check(d.applyPreparedEdit(*stale) == EditState::Stale, "stale commit accepted");
    auto sameSession = prepare(d, ClearProgressPolicyEdit{});
    d.save();
    const auto reopen = d.path();
    d.open(reopen);
    check(d.applyPreparedEdit(*sameSession) == EditState::Stale, "reopen session accepted");
    d.adoptProgressPolicy(selection(d, retainedProgressPolicy));
    check(calculate(*d.project()).tasks[1].finish == "2026-11-04 14:00", "A replacement");
    d.clearProgressPolicy();
    check(!d.project()->progressPolicy && nativeSchemaFor(*d.project()) < 13, "release");
    auto bad = adopted;
    bad.tasks[0].durationMinutes.reset();
    bad.revision += 2;
    const auto badPath = temp.filePath("incomplete.ppm").toStdString();
    store.save(badPath, bad, {});
    Document incomplete(store);
    incomplete.open(badPath);
    incomplete.clearProgressPolicy();
    check(!incomplete.project()->progressPolicy, "cannot release invalid forecast");
    const auto body = json::encodeProject(adopted);
    check(json::decodeProject(body, 13) == adopted, "JSON roundtrip");
    rejects([&] { json::decodeProject(body, 12); });
    for (const auto &key : {"policy", "adoptedBy", "adoptedAt", "reason", "sourceDocumentId"}) {
        auto value = json::encodeProgressPolicySelection(*adopted.progressPolicy);
        value[key] = " ";
        rejects([&] { json::decodeProgressPolicySelection(value); });
    }
    for (const auto &value : {QJsonValue(), QJsonValue(1), QJsonValue(QJsonObject{})})
        rejects([&] { json::decodeProgressPolicySelection(value); });
    auto value = json::encodeProgressPolicySelection(*adopted.progressPolicy);
    value["unknown"] = true;
    rejects([&] { json::decodeProgressPolicySelection(value); });
    value = json::encodeProgressPolicySelection(*adopted.progressPolicy);
    value["sourceRevision"] = 1.5;
    rejects([&] { json::decodeProgressPolicySelection(value); });
    for (const auto *profile : {pdmProfile, constraintProfile, effortProfile}) {
        auto lane = progress_fixture::small(profile);
        lane.progressPolicy = ProgressPolicySelection{
            overrideProgressPolicy, "PM", "2026-11-03T00:00:00Z", "test", lane.documentId, lane.revision};
        const auto result = calculate(lane);
        check(result.status == CalculationStatus::Valid && result.tasks[1].finish == "2026-11-03 14:00", "profile B");
    }
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        contracts();
        QJsonArray metrics;
        for (int count : {1000, 10000}) {
            auto p = progress_fixture::large(count);
            p.progressPolicy = ProgressPolicySelection{
                overrideProgressPolicy, "PM", "2026-11-03T00:00:00Z", "scale", p.documentId, p.revision};
            QElapsedTimer timer;
            timer.start();
            const auto result = calculate(p);
            check(result.status == CalculationStatus::Valid && result.tasks.size() == std::size_t(count),
                  "adopted scale");
            metrics.append(QJsonObject{{"tasks", count}, {"calculation_ms", double(timer.nsecsElapsed()) / 1000000}});
        }
        QDir().mkpath("progress-policy-adoption-qa");
        QFile metricsFile("progress-policy-adoption-qa/core-metrics.json");
        check(metricsFile.open(QIODevice::WriteOnly), "metrics file");
        metricsFile.write(QJsonDocument(metrics).toJson());
        std::cout << "progress policy adoption persistence/history/worker/forecast/integration passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
