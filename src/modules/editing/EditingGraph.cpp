#include "editing/EditingGraph.h"

#include <algorithm>
#include <utility>

namespace eve::editing {
namespace {
template <class T>
Result<T> graphError(Status status, const char* rule, std::string message) {
    return Result<T>::error(status, RuleId(rule), std::move(message));
}
}  // namespace

Result<void> GraphDocument::createNode(GraphNodeRecord node) {
    if (node.id.empty() || node.type.empty() || nodes_.contains(node.id))
        return graphError<void>(Status::Conflict, "editing.graph.invalid-node",
                                "Graph node id/type is missing or the id already exists");
    for (GraphPinRecord& pin : node.pins) {
        if (pin.id.empty())
            return graphError<void>(Status::Rejected, "editing.graph.invalid-pin", "Graph pin id is required");
        pin.node = node.id;
        if (findPin(pin.id))
            return graphError<void>(Status::Conflict, "editing.graph.duplicate-pin", "Graph pin id already exists");
    }
    nodes_.emplace(node.id, std::move(node));
    ++revision_;
    return Result<void>::applied();
}

Result<void> GraphDocument::deleteNode(const GraphNodeId& node) {
    if (!nodes_.contains(node))
        return graphError<void>(Status::NotFound, "editing.graph.node-not-found", "Graph node does not exist");
    std::vector<GraphPinId> pins;
    for (const GraphPinRecord& pin : nodes_.at(node).pins) pins.push_back(pin.id);
    std::erase_if(edges_, [&](const auto& edge) {
        return std::find(pins.begin(), pins.end(), edge.second.from) != pins.end() ||
               std::find(pins.begin(), pins.end(), edge.second.to) != pins.end();
    });
    nodes_.erase(node);
    ++revision_;
    return Result<void>::applied();
}

Result<void> GraphDocument::connect(GraphEdgeRecord edge, const GraphConnectionDecision& decision) {
    if (!decision.allowed) {
        Result<void> result;
        result.status      = Status::Rejected;
        result.diagnostics = decision.diagnostics;
        if (result.diagnostics.empty())
            result.diagnostics.push_back(
                {RuleId("editing.graph.connection-rejected"), DiagnosticSeverity::Error, "Connection was rejected"});
        return result;
    }
    if (edge.id.empty() || edges_.contains(edge.id) || !findPin(edge.from) || !findPin(edge.to))
        return graphError<void>(Status::Rejected, "editing.graph.invalid-edge",
                                "Graph edge id/pins are invalid or duplicated");
    edges_.emplace(edge.id, std::move(edge));
    ++revision_;
    return Result<void>::applied();
}

Result<void> GraphDocument::disconnect(const StableId& edge) {
    if (!edges_.erase(edge))
        return graphError<void>(Status::NotFound, "editing.graph.edge-not-found", "Graph edge does not exist");
    ++revision_;
    return Result<void>::applied();
}

Result<void> GraphDocument::setNodeProperties(const GraphNodeId& node, Value properties) {
    auto found = nodes_.find(node);
    if (found == nodes_.end())
        return graphError<void>(Status::NotFound, "editing.graph.node-not-found", "Graph node does not exist");
    if (properties.type() != Value::Type::Object)
        return graphError<void>(Status::Rejected, "editing.graph.invalid-properties",
                                "Graph node properties must be an object");
    found->second.properties = std::move(properties);
    ++revision_;
    return Result<void>::applied();
}

Result<void> GraphDocument::setParameters(Value parameters) {
    if (parameters.type() != Value::Type::Object)
        return graphError<void>(Status::Rejected, "editing.graph.invalid-parameters",
                                "Graph parameters must be an object");
    if (!parameters.isWithinLimits(4, 4096, 256 * 1024))
        return graphError<void>(Status::Rejected, "editing.graph.parameters-too-large",
                                "Graph parameters exceed editing document limits");
    parameters_ = std::move(parameters);
    ++revision_;
    return Result<void>::applied();
}

GraphDocumentData GraphDocument::snapshot(std::string domain) const {
    GraphDocumentData result;
    result.domain     = std::move(domain);
    result.revision   = revision_;
    result.parameters = parameters_;
    for (const auto& [id, node] : nodes_) {
        (void)id;
        result.nodes.push_back(node);
    }
    for (const auto& [id, edge] : edges_) {
        (void)id;
        result.edges.push_back(edge);
    }
    return result;
}

const GraphPinRecord* GraphDocument::findPin(const GraphPinId& pin) const {
    for (const auto& [id, node] : nodes_) {
        (void)id;
        auto found = std::find_if(node.pins.begin(), node.pins.end(),
                                  [&](const GraphPinRecord& candidate) { return candidate.id == pin; });
        if (found != node.pins.end()) return &*found;
    }
    return nullptr;
}

}  // namespace eve::editing
