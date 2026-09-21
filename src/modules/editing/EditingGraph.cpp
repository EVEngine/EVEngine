#include "editing/EditingGraph.h"

#include <algorithm>
#include <utility>

namespace eve::editing {
namespace {}  // namespace

Result<void> GraphDocument::createNode(GraphNodeRecord node) {
    if (node.id.empty() || node.type.empty() || nodes_.contains(node.id))
        return eve::editing::failed<void>(Status::Conflict, RuleId("editing.graph.invalid-node"),
                                          "Graph node id/type is missing or the id already exists");
    for (GraphPinRecord& pin : node.pins) {
        if (pin.id.empty())
            return eve::editing::failed<void>(Status::Rejected, RuleId("editing.graph.invalid-pin"),
                                              "Graph pin id is required");
        pin.node = node.id;
        if (findPin(pin.id))
            return eve::editing::failed<void>(Status::Conflict, RuleId("editing.graph.duplicate-pin"),
                                              "Graph pin id already exists");
    }
    nodes_.emplace(node.id, std::move(node));
    ++revision_;
    return eve::editing::applied<void>();
}

Result<void> GraphDocument::deleteNode(const GraphNodeId& node) {
    if (!nodes_.contains(node))
        return eve::editing::failed<void>(Status::NotFound, RuleId("editing.graph.node-not-found"),
                                          "Graph node does not exist");
    std::vector<GraphPinId> pins;
    for (const GraphPinRecord& pin : nodes_.at(node).pins) pins.push_back(pin.id);
    std::erase_if(edges_, [&](const auto& edge) {
        return std::find(pins.begin(), pins.end(), edge.second.from) != pins.end() ||
               std::find(pins.begin(), pins.end(), edge.second.to) != pins.end();
    });
    nodes_.erase(node);
    ++revision_;
    return eve::editing::applied<void>();
}

Result<void> GraphDocument::connect(GraphEdgeRecord edge, const GraphConnectionDecision& decision) {
    if (!decision.allowed) {
        auto diagnostics = decision.diagnostics;
        if (diagnostics.empty())
            diagnostics.push_back(ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation,
                                                 RuleId("editing.graph.connection-rejected"),
                                                 DiagnosticSeverity::Error, "Connection was rejected"));
        return Result<void>::failure(eve::Status(Status::Rejected, std::move(diagnostics)));
    }
    if (edge.id.empty() || edges_.contains(edge.id) || !findPin(edge.from) || !findPin(edge.to))
        return eve::editing::failed<void>(Status::Rejected, RuleId("editing.graph.invalid-edge"),
                                          "Graph edge id/pins are invalid or duplicated");
    edges_.emplace(edge.id, std::move(edge));
    ++revision_;
    return eve::editing::applied<void>();
}

Result<void> GraphDocument::disconnect(const StableId& edge) {
    if (!edges_.erase(edge))
        return eve::editing::failed<void>(Status::NotFound, RuleId("editing.graph.edge-not-found"),
                                          "Graph edge does not exist");
    ++revision_;
    return eve::editing::applied<void>();
}

Result<void> GraphDocument::setNodeProperties(const GraphNodeId& node, Value properties) {
    auto found = nodes_.find(node);
    if (found == nodes_.end())
        return eve::editing::failed<void>(Status::NotFound, RuleId("editing.graph.node-not-found"),
                                          "Graph node does not exist");
    if (properties.type() != Value::Type::Object)
        return eve::editing::failed<void>(Status::Rejected, RuleId("editing.graph.invalid-properties"),
                                          "Graph node properties must be an object");
    found->second.properties = std::move(properties);
    ++revision_;
    return eve::editing::applied<void>();
}

Result<void> GraphDocument::setParameters(Value parameters) {
    if (parameters.type() != Value::Type::Object)
        return eve::editing::failed<void>(Status::Rejected, RuleId("editing.graph.invalid-parameters"),
                                          "Graph parameters must be an object");
    if (!parameters.isWithinLimits(4, 4096, 256 * 1024))
        return eve::editing::failed<void>(Status::Rejected, RuleId("editing.graph.parameters-too-large"),
                                          "Graph parameters exceed editing document limits");
    parameters_ = std::move(parameters);
    ++revision_;
    return eve::editing::applied<void>();
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
