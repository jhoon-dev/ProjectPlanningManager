#pragma once
#include "calculation/Scheduler.h"
#include <set>
#include <unordered_map>
namespace ppm {
enum class NetworkMode { Neighbors, Ancestors, Descendants, DrivingAncestors, DrivingDescendants, All };
enum class NetworkDriving { Unknown, No, Yes, NotApplicable, Inactive };
struct NetworkNode {
    std::string id, name, calendarId, wbsId;
    std::optional<std::string> actualStart, actualFinish;
    std::optional<std::size_t> timing;
    std::vector<std::size_t> incoming, outgoing;
    std::size_t component = 0, layer = 0;
    bool cyclic = false;
};
struct NetworkEdge {
    std::size_t from = 0, to = 0;
    DependencyType type = DependencyType::FS;
    std::int64_t lagMinutes = 0;
    NetworkDriving driving = NetworkDriving::Unknown;
    bool cyclic = false;
    std::string resourceId;
    int resourceUnits = 0;
    std::string releaseAt{}, acquireAt{};
};
struct NetworkGraph {
    std::string documentId, name;
    std::uint64_t revision = 0;
    ScheduleResult calculation;
    std::vector<NetworkNode> nodes;
    std::vector<NetworkEdge> edges;
    std::unordered_map<std::string, std::size_t> indices;
    std::size_t cycleComponents = 0, unknownRelations = 0;
    std::vector<std::string> warnings;
};
struct NetworkQuery {
    std::string focus;
    NetworkMode mode = NetworkMode::Neighbors;
    std::size_t radius = 1, maxNodes = 80, maxEdges = 600, offset = 0;
    bool visibleOnly = false;
    std::set<std::string> visible;
};
struct NetworkPlacedNode {
    std::size_t node = 0, column = 0, row = 0, hiddenIncoming = 0, hiddenOutgoing = 0;
    bool inTable = false;
};
struct NetworkWindow {
    std::vector<NetworkPlacedNode> nodes;
    std::vector<std::size_t> edges;
    std::size_t candidates = 0, omittedNodes = 0, omittedEdges = 0, scopeExcluded = 0;
    std::size_t offset = 0, nextOffset = 0;
    bool drivingUnavailable = false;
};
NetworkGraph buildNetworkGraph(const Project &, const std::shared_ptr<CalculationControl> &control = {});
NetworkWindow networkWindow(const NetworkGraph &, const NetworkQuery &,
                            const std::shared_ptr<CalculationControl> &control = {});
} // namespace ppm
