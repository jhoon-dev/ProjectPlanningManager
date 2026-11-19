#include "calculation/NetworkDiagram.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <chrono>
#include <iostream>
namespace {
using namespace ppm;
void check(bool value, const char *message) {
    if (!value)
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
Project fixture() {
    Project p;
    p.documentId = "network-contract";
    p.name = "네트워크 기대값";
    p.startDate = "2026-11-02";
    p.tasks = {
        {"A", "기초 A", 480, {}}, {"B", "후속 B", 480, {"A"}}, {"C", "후속 C", 480, {"B"}}, {"D", "독립 D", 480, {}}};
    return p;
}
const TaskSchedule &row(const NetworkGraph &g, const std::string &id) {
    const auto &n = g.nodes.at(g.indices.at(id));
    check(n.timing.has_value(), "schedule row missing");
    return g.calculation.tasks.at(*n.timing);
}
std::set<std::string> ids(const NetworkGraph &g, const NetworkWindow &w) {
    std::set<std::string> result;
    for (const auto &n : w.nodes)
        result.insert(g.nodes[n.node].id);
    return result;
}
void contracts() {
    auto p = fixture();
    const auto before = p;
    const auto g = buildNetworkGraph(p);
    const bool sourceUnchanged = p == before;
    check(p == before && g.nodes.size() == 4 && g.edges.size() == 2 && !g.cycleComponents,
          "graph changed source or lost input");
    check(row(g, "A").start == "2026-11-02 09:00" && row(g, "B").start == "2026-11-03 09:00" &&
              row(g, "C").finish == "2026-11-04 18:00" && row(g, "D").totalFloatMinutes == 960,
          "independent plan dates/float mismatch");
    NetworkQuery q;
    q.focus = "B";
    q.visible = {"A", "B", "C", "D"};
    auto w = networkWindow(g, q);
    check(ids(g, w) == std::set<std::string>{"A", "B", "C"} && w.edges.size() == 2, "neighbor graph wrong");
    q.mode = NetworkMode::Ancestors;
    q.focus = "C";
    check(ids(g, networkWindow(g, q)) == std::set<std::string>{"A", "B", "C"}, "transitive ancestors lost");
    q.mode = NetworkMode::DrivingAncestors;
    check(networkWindow(g, q).edges.size() == 2, "binding path lost");
    q.mode = NetworkMode::DrivingDescendants;
    q.focus = "A";
    check(networkWindow(g, q).edges.size() == 2, "binding descendants lost");
    q.mode = NetworkMode::Ancestors;
    q.focus = "C";
    q.visibleOnly = true;
    q.visible = {"A", "C"};
    w = networkWindow(g, q);
    check(ids(g, w) == std::set<std::string>{"A", "C"} && w.edges.empty() && w.scopeExcluded == 1 &&
              row(g, "D").totalFloatMinutes == 960,
          "filter changed calculation/fabricated edge across hidden node");
    q.visible.clear();
    w = networkWindow(g, q);
    check(w.nodes.size() == 1 && !w.nodes[0].inTable, "hidden focus not retained");
    q = {};
    q.focus = "C";
    q.mode = NetworkMode::All;
    q.maxNodes = 2;
    auto first = networkWindow(g, q);
    q.offset = first.nextOffset;
    auto next = networkWindow(g, q);
    check(first.omittedNodes == 2 && first.nodes.size() == 2 && next.nodes.size() == 2 && ids(g, first) != ids(g, next),
          "whole-graph paging lost focus/limits");
    auto pdm = fixture();
    pdm.calculationProfile = pdmProfile;
    pdm.tasks = {{"A", "A", 480, {}},
                 {"FS", "FS", 480, {"A"}},
                 {"SS", "SS", 480, {"A"}},
                 {"FF", "FF", 480, {"A"}},
                 {"SF", "SF", 480, {"A"}}};
    pdm.tasks[1].dependencyRules = {{"A", DependencyType::FS, 60}};
    pdm.tasks[2].dependencyRules = {{"A", DependencyType::SS, -60}};
    pdm.tasks[3].dependencyRules = {{"A", DependencyType::FF, 30}};
    pdm.tasks[4].dependencyRules = {{"A", DependencyType::SF, 0}};
    const auto four = buildNetworkGraph(pdm);
    check(four.edges.size() == 4 && four.edges[0].type == DependencyType::FS && four.edges[0].lagMinutes == 60 &&
              four.edges[1].type == DependencyType::SS && four.edges[1].lagMinutes == -60 &&
              four.edges[2].type == DependencyType::FF && four.edges[3].type == DependencyType::SF,
          "relation type/lag lost");
    check(row(four, "FS").start == "2026-11-03 10:00" && row(four, "SS").start == "2026-11-02 09:00" &&
              row(four, "FF").finish == "2026-11-03 09:30" && row(four, "SF").start == "2026-11-02 09:00",
          "PDM hand dates differ");
    for (const auto &edge : four.edges) {
        const auto &native = row(four, four.nodes[edge.to].id).drivingPredecessors;
        const bool binding = std::find(native.begin(), native.end(), four.nodes[edge.from].id) != native.end();
        check(edge.driving == (binding ? NetworkDriving::Yes : NetworkDriving::No),
              "driving differs from native engine witness");
    }
    auto nonbinding = fixture();
    nonbinding.tasks.push_back({"E", "E", 480, {"A", "C"}});
    const auto mixed = buildNetworkGraph(nonbinding);
    check(mixed.edges[mixed.edges.size() - 2].driving == NetworkDriving::No &&
              mixed.edges.back().driving == NetworkDriving::Yes,
          "binding confused with relation existence");
    p.statusAt = "2026-11-02 18:00";
    ProgressEntry e;
    e.id = "actual";
    e.taskId = "A";
    e.statusAt = *p.statusAt;
    e.actualStart = "2026-11-02 09:00";
    e.actualFinish = "2026-11-02 18:00";
    e.actualDurationMinutes = 480;
    e.planDurationMinutes = 480;
    e.remainingMinutes = 0;
    e.reporter = "PM";
    e.reason = "실제";
    e.recordedAt = "2026-11-03T00:00:00Z";
    e.sourceDocumentId = p.documentId;
    e.recordedRevision = 2;
    p.revision = 2;
    p.progress = {e};
    const auto forecast = buildNetworkGraph(p);
    check(row(forecast, "A").completedActual && forecast.nodes[0].actualFinish == e.actualFinish &&
              !hasScheduleFloat(forecast.calculation, row(forecast, "A")) &&
              row(forecast, "D").totalFloatMinutes == 480,
          "actual/future float confused");
    auto selected = fixture();
    Resource resource;
    resource.id = "R";
    resource.name = "공유";
    selected.resources = {resource};
    for (const auto &t : selected.tasks)
        selected.assignments.push_back({t.id, "R", 100});
    LevelingSelection selection;
    selection.policy = levelingPolicyFor(selected);
    selection.adoptedAt = "2026-11-03T00:00:00Z";
    selection.reason = "네트워크 검증";
    selection.sourceDocumentId = selected.documentId;
    selection.sourceRevision = selected.revision;
    selected.planLeveling = selection;
    const auto leveled = buildNetworkGraph(selected);
    q = {};
    q.focus = "C";
    q.mode = NetworkMode::DrivingAncestors;
    w = networkWindow(leveled, q);
    check(leveled.calculation.levelingFloatCalculated && !w.drivingUnavailable && w.nodes.size() == 3 &&
              std::any_of(
                  leveled.edges.begin(), leveled.edges.end(),
                  [](const auto &edge) { return !edge.resourceId.empty() && edge.driving == NetworkDriving::Yes; }),
          "leveling resource driving path missing");
    auto cycle = fixture();
    cycle.tasks[0].predecessors = {"B"};
    const auto cyclic = buildNetworkGraph(cycle);
    check(cyclic.cycleComponents == 1 && cyclic.nodes[0].cyclic && cyclic.nodes[1].cyclic && !cyclic.nodes[2].cyclic &&
              std::count_if(cyclic.edges.begin(), cyclic.edges.end(), [](const auto &edge) { return edge.cyclic; }) ==
                  2 &&
              cyclic.calculation.status == CalculationStatus::Conflict,
          "SCC/cycle semantics wrong");
    auto missing = fixture();
    missing.tasks[0].predecessors = {"missing"};
    const auto broken = buildNetworkGraph(missing);
    check(broken.unknownRelations == 1 && !broken.warnings.empty() && !broken.nodes[0].timing,
          "unknown predecessor silently accepted");
    auto dense = fixture();
    dense.tasks.clear();
    for (int i = 0; i < 30; ++i)
        dense.tasks.push_back({"T" + std::to_string(i), "작업", 1, {}});
    for (int to = 0; to < 30; ++to)
        for (int from = 0; from < 30; ++from)
            if (to != from)
                dense.tasks[to].predecessors.push_back(dense.tasks[from].id);
    const auto crowded = buildNetworkGraph(dense);
    q = {};
    q.focus = "T0";
    q.mode = NetworkMode::All;
    q.maxNodes = 30;
    q.maxEdges = 5;
    w = networkWindow(crowded, q);
    const auto root = std::find_if(w.nodes.begin(), w.nodes.end(), [](const auto &n) { return n.node == 0; });
    check(w.edges.size() == 5 && w.omittedEdges == 865 && root->hiddenIncoming + root->hiddenOutgoing == 53,
          "hidden edge counters did not include capped relations");
    auto control = std::make_shared<CalculationControl>();
    control->cancel();
    bool canceled = false;
    try {
        buildNetworkGraph(p, control);
    } catch (const CalculationCancelled &) {
        canceled = true;
    }
    check(canceled, "graph cancellation swallowed");
    canceled = false;
    try {
        networkWindow(g, q, control);
    } catch (const CalculationCancelled &) {
        canceled = true;
    }
    check(canceled, "view cancellation swallowed");
    q = {};
    q.focus = "A";
    q.maxNodes = 201;
    rejects([&] { networkWindow(g, q); });
    q.maxNodes = 80;
    q.focus = "missing";
    rejects([&] { networkWindow(g, q); });
    QDir().mkpath("network-qa");
    QJsonObject proof{{"plan_finish", QString::fromStdString(*g.calculation.projectFinish)},
                      {"plan_D_float", qint64(row(g, "D").totalFloatMinutes)},
                      {"forecast_D_float", qint64(row(forecast, "D").totalFloatMinutes)},
                      {"four_relations", 4},
                      {"cycle_components", int(cyclic.cycleComponents)},
                      {"unknown_relations", int(broken.unknownRelations)},
                      {"dense_shown_edges", int(w.edges.size())},
                      {"dense_omitted_edges", int(w.omittedEdges)},
                      {"source_unchanged", sourceUnchanged}};
    QFile file("network-qa/contract.json");
    check(file.open(QIODevice::WriteOnly), "contract evidence failed");
    file.write(QJsonDocument(proof).toJson());
}
void benchmark() {
    QJsonArray metrics;
    for (int count : {1000, 10000}) {
        auto p = fixture();
        p.documentId = "network-core-" + std::to_string(count);
        p.tasks.clear();
        for (int i = 0; i < count; ++i)
            p.tasks.push_back({"T" + std::to_string(i), "작업 " + std::to_string(i), 1,
                               i ? std::vector<std::string>{"T" + std::to_string(i - 1)} : std::vector<std::string>{}});
        auto start = std::chrono::steady_clock::now();
        const auto g = buildNetworkGraph(p);
        auto built = std::chrono::steady_clock::now();
        NetworkQuery q;
        q.focus = "T" + std::to_string(count / 2);
        q.radius = 2;
        auto view = networkWindow(g, q);
        auto viewed = std::chrono::steady_clock::now();
        check(g.nodes.size() == std::size_t(count) && g.edges.size() == std::size_t(count - 1) &&
                  view.nodes.size() == 5,
              "large chain indexing/window wrong");
        q.mode = NetworkMode::Ancestors;
        auto path = networkWindow(g, q);
        check(path.nodes.size() == 80 && path.candidates == std::size_t(count / 2 + 1),
              "large transitive path truncated silently");
        metrics.append(QJsonObject{{"tasks", count},
                                   {"edges", count - 1},
                                   {"build_ms", std::chrono::duration<double, std::milli>(built - start).count()},
                                   {"query_ms", std::chrono::duration<double, std::milli>(viewed - built).count()},
                                   {"window_nodes", int(view.nodes.size())},
                                   {"window_edges", int(view.edges.size())},
                                   {"ancestor_candidates", int(path.candidates)},
                                   {"ancestor_omitted", int(path.omittedNodes)}});
    }
    QFile file("network-qa/core-metrics.json");
    check(file.open(QIODevice::WriteOnly), "metrics write failed");
    file.write(QJsonDocument(metrics).toJson());
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        contracts();
        benchmark();
        std::cout << "network: plan/forecast/PDM binding, filtered graph, paging, leveling unknown, SCC, missing IDs, "
                     "capped edges, cancellation, 1k/10k passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
