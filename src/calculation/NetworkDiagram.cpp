#include "calculation/NetworkDiagram.h"
#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <stdexcept>
namespace ppm {
namespace {
[[noreturn]] void fail(const char *text) {
    throw std::runtime_error(text);
}
void components(NetworkGraph &g) {
    const auto n = g.nodes.size();
    std::vector<bool> seen(n, false);
    std::vector<std::size_t> order;
    for (std::size_t root = 0; root < n; ++root) {
        calculationCheckpoint();
        if (seen[root])
            continue;
        seen[root] = true;
        std::vector<std::pair<std::size_t, std::size_t>> stack{{root, 0}};
        while (!stack.empty()) {
            calculationCheckpoint();
            auto &[v, next] = stack.back();
            if (next < g.nodes[v].outgoing.size()) {
                const auto to = g.edges[g.nodes[v].outgoing[next++]].to;
                if (!seen[to]) {
                    seen[to] = true;
                    stack.emplace_back(to, 0);
                }
            } else {
                order.push_back(v);
                stack.pop_back();
            }
        }
    }
    std::fill(seen.begin(), seen.end(), false);
    std::vector<std::size_t> sizes;
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if (seen[*it])
            continue;
        const auto id = sizes.size();
        std::size_t size = 0;
        std::vector<std::size_t> stack{*it};
        seen[*it] = true;
        while (!stack.empty()) {
            calculationCheckpoint();
            const auto v = stack.back();
            stack.pop_back();
            g.nodes[v].component = id;
            ++size;
            for (auto e : g.nodes[v].incoming) {
                const auto from = g.edges[e].from;
                if (!seen[from]) {
                    seen[from] = true;
                    stack.push_back(from);
                }
            }
        }
        sizes.push_back(size);
    }
    std::vector<std::vector<std::size_t>> outgoing(sizes.size());
    std::vector<std::size_t> degree(sizes.size(), 0), rank(sizes.size(), 0);
    std::vector<bool> cyclic(sizes.size(), false);
    for (auto &e : g.edges) {
        calculationCheckpoint();
        const auto a = g.nodes[e.from].component, b = g.nodes[e.to].component;
        e.cyclic = a == b && (sizes[a] > 1 || e.from == e.to);
        if (e.cyclic)
            cyclic[a] = true;
        if (a != b) {
            outgoing[a].push_back(b);
            ++degree[b];
        }
    }
    std::deque<std::size_t> ready;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        if (!degree[i])
            ready.push_back(i);
        if (cyclic[i])
            ++g.cycleComponents;
    }
    while (!ready.empty()) {
        calculationCheckpoint();
        const auto v = ready.front();
        ready.pop_front();
        for (auto to : outgoing[v]) {
            rank[to] = std::max(rank[to], rank[v] + 1);
            if (--degree[to] == 0)
                ready.push_back(to);
        }
    }
    for (auto &node : g.nodes) {
        node.layer = rank[node.component];
        node.cyclic = cyclic[node.component];
    }
}
} // namespace
NetworkGraph buildNetworkGraph(const Project &p, const std::shared_ptr<CalculationControl> &control) {
    EvaluationScope evaluation(control);
    calculationCheckpoint();
    if (p.tasks.size() > 10000)
        fail("네트워크 작업 한도(10,000)를 초과했습니다.");
    std::size_t count = 0;
    for (const auto &task : p.tasks) {
        count += task.predecessors.size();
        if (count > 200000)
            fail("네트워크 관계 한도(200,000)를 초과했습니다.");
    }
    NetworkGraph g;
    g.documentId = p.documentId;
    g.name = p.name;
    g.revision = p.revision;
    g.nodes.reserve(p.tasks.size());
    for (const auto &t : p.tasks) {
        calculationCheckpoint();
        if (!g.indices.emplace(t.id, g.nodes.size()).second)
            fail("중복 작업 ID는 네트워크에서 표시할 수 없습니다.");
        NetworkNode node;
        node.id = t.id;
        node.name = t.name;
        node.calendarId = t.calendarId;
        node.wbsId = t.wbsId;
        g.nodes.push_back(std::move(node));
    }
    g.calculation = calculate(p);
    if (g.calculation.status == CalculationStatus::Valid)
        for (std::size_t i = 0; i < g.calculation.tasks.size(); ++i) {
            const auto &row = g.calculation.tasks[i];
            if (g.indices.contains(row.taskId))
                g.nodes[g.indices.at(row.taskId)].timing = i;
        }
    std::unordered_map<std::string, const ProgressEntry *> actuals;
    if (p.statusAt)
        for (const auto &e : p.progress) {
            calculationCheckpoint();
            if (e.statusAt <= *p.statusAt &&
                (!actuals.contains(e.taskId) || actuals.at(e.taskId)->statusAt <= e.statusAt))
                actuals[e.taskId] = &e;
        }
    for (auto &node : g.nodes)
        if (actuals.contains(node.id)) {
            node.actualStart = actuals.at(node.id)->actualStart;
            node.actualFinish = actuals.at(node.id)->actualFinish;
        }
    for (std::size_t to = 0; to < p.tasks.size(); ++to) {
        calculationCheckpoint();
        const auto &task = p.tasks[to];
        std::unordered_map<std::string, const Dependency *> rules;
        for (const auto &d : task.dependencyRules)
            rules.emplace(d.predecessorId, &d);
        std::set<std::string> driving;
        const auto &node = g.nodes[to];
        const TaskSchedule *timing = node.timing ? &g.calculation.tasks[*node.timing] : nullptr;
        if (timing)
            driving.insert(timing->drivingPredecessors.begin(), timing->drivingPredecessors.end());
        for (const auto &id : task.predecessors) {
            calculationCheckpoint();
            if (!g.indices.contains(id)) {
                ++g.unknownRelations;
                if (g.warnings.size() < 100)
                    g.warnings.push_back("알 수 없는 선행 작업: " + id + " → " + task.id);
                continue;
            }
            NetworkEdge e;
            e.from = g.indices.at(id);
            e.to = to;
            if (rules.contains(id)) {
                e.type = rules.at(id)->type;
                e.lagMinutes = rules.at(id)->lagMinutes;
            }
            if (timing && timing->completedActual)
                e.driving = NetworkDriving::NotApplicable;
            else if (timing && p.statusAt && p.progressPolicy && p.progressPolicy->policy == overrideProgressPolicy &&
                     node.actualStart && !node.actualFinish)
                e.driving = NetworkDriving::Inactive;
            else if (timing && hasScheduleFloat(g.calculation, *timing))
                e.driving = driving.contains(id) ? NetworkDriving::Yes : NetworkDriving::No;
            const auto index = g.edges.size();
            g.edges.push_back(e);
            g.nodes[e.from].outgoing.push_back(index);
            g.nodes[to].incoming.push_back(index);
        }
    }
    components(g);
    for (const auto &link : g.calculation.resourceOrderLinks) {
        if (g.edges.size() >= 200000)
            fail("자원 관계 포함 네트워크 한도 200,000개 초과");
        NetworkEdge edge;
        edge.from = g.indices.at(link.predecessorId);
        edge.to = g.indices.at(link.taskId);
        edge.resourceId = link.resourceId;
        edge.resourceUnits = link.units;
        edge.releaseAt = link.releaseAt;
        edge.acquireAt = link.acquireAt;
        edge.driving = link.driving ? NetworkDriving::Yes : NetworkDriving::No;
        g.nodes[edge.from].outgoing.push_back(g.edges.size());
        g.nodes[edge.to].incoming.push_back(g.edges.size());
        g.edges.push_back(std::move(edge));
    }
    return g;
}
NetworkWindow networkWindow(const NetworkGraph &g, const NetworkQuery &q,
                            const std::shared_ptr<CalculationControl> &control) {
    EvaluationScope evaluation(control);
    calculationCheckpoint();
    if (q.maxNodes < 2 || q.maxNodes > 200 || q.maxEdges < 1 || q.maxEdges > 600 || q.radius < 1 || q.radius > 10 ||
        int(q.mode) < 0 || int(q.mode) > int(NetworkMode::All))
        fail("네트워크 표시 설정을 확인하세요.");
    NetworkWindow out;
    if (g.nodes.empty())
        return out;
    if (!g.indices.contains(q.focus))
        fail("기준 작업을 찾지 못했습니다.");
    const auto root = g.indices.at(q.focus), n = g.nodes.size(), none = std::numeric_limits<std::size_t>::max();
    const bool driver = q.mode == NetworkMode::DrivingAncestors || q.mode == NetworkMode::DrivingDescendants;
    out.drivingUnavailable = driver && (g.calculation.status != CalculationStatus::Valid ||
                                        (g.calculation.resourceLeveled
                                             ? !g.calculation.levelingFloatCalculated
                                             : (g.calculation.isForecast && !g.calculation.forecastFloatCalculated)));
    std::vector<std::size_t> distance(n, none);
    distance[root] = 0;
    std::deque<std::size_t> queue{root};
    if (q.mode == NetworkMode::All)
        std::fill(distance.begin(), distance.end(), 0);
    else
        while (!queue.empty()) {
            calculationCheckpoint();
            const auto v = queue.front();
            queue.pop_front();
            if (q.mode == NetworkMode::Neighbors && distance[v] >= q.radius)
                continue;
            auto visit = [&](const std::vector<std::size_t> &edges, bool upstream) {
                for (auto id : edges) {
                    calculationCheckpoint();
                    const auto &e = g.edges[id];
                    if (driver && e.driving != NetworkDriving::Yes)
                        continue;
                    const auto to = upstream ? e.from : e.to;
                    if (distance[to] == none) {
                        distance[to] = distance[v] + 1;
                        queue.push_back(to);
                    }
                }
            };
            if (q.mode == NetworkMode::Neighbors || q.mode == NetworkMode::Ancestors ||
                q.mode == NetworkMode::DrivingAncestors)
                visit(g.nodes[v].incoming, true);
            if (q.mode == NetworkMode::Neighbors || q.mode == NetworkMode::Descendants ||
                q.mode == NetworkMode::DrivingDescendants)
                visit(g.nodes[v].outgoing, false);
        }
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < n; ++i) {
        calculationCheckpoint();
        if (distance[i] == none)
            continue;
        if (q.visibleOnly && i != root && !q.visible.contains(g.nodes[i].id)) {
            ++out.scopeExcluded;
            continue;
        }
        if (i != root)
            candidates.push_back(i);
    }
    if (q.mode != NetworkMode::All)
        std::stable_sort(candidates.begin(), candidates.end(),
                         [&](auto a, auto b) { return distance[a] < distance[b]; });
    out.candidates = candidates.size() + 1;
    const auto offset = q.mode == NetworkMode::All ? std::min(q.offset, candidates.size()) : 0;
    const auto end = std::min(candidates.size(), offset + q.maxNodes - 1);
    out.offset = offset;
    out.nextOffset = end;
    std::vector<std::size_t> included{root};
    included.insert(included.end(), candidates.begin() + std::ptrdiff_t(offset),
                    candidates.begin() + std::ptrdiff_t(end));
    std::sort(included.begin(), included.end());
    out.omittedNodes = out.candidates - included.size();
    std::vector<bool> selected(n, false);
    std::vector<std::size_t> positions(n, none);
    std::set<std::size_t> layers;
    for (auto i : included) {
        selected[i] = true;
        layers.insert(g.nodes[i].layer);
    }
    std::unordered_map<std::size_t, std::size_t> columns, rows;
    for (auto layer : layers)
        columns[layer] = columns.size();
    for (auto i : included) {
        const auto &node = g.nodes[i];
        const auto column = columns.at(node.layer);
        positions[i] = out.nodes.size();
        out.nodes.push_back(
            {i, column, rows[column]++, node.incoming.size(), node.outgoing.size(), q.visible.contains(node.id)});
    }
    std::array<std::vector<std::size_t>, 3> buckets;
    for (std::size_t i = 0; i < g.edges.size(); ++i) {
        calculationCheckpoint();
        const auto &e = g.edges[i];
        if (selected[e.from] && selected[e.to])
            buckets[e.from == root || e.to == root ? 0 : e.driving == NetworkDriving::Yes ? 1 : 2].push_back(i);
    }
    for (const auto &bucket : buckets)
        for (auto i : bucket) {
            calculationCheckpoint();
            if (out.edges.size() >= q.maxEdges) {
                ++out.omittedEdges;
                continue;
            }
            out.edges.push_back(i);
            const auto &e = g.edges[i];
            --out.nodes[positions[e.from]].hiddenOutgoing;
            --out.nodes[positions[e.to]].hiddenIncoming;
        }
    return out;
}
} // namespace ppm
