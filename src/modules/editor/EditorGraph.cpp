#include "editor/EditorGraph.h"

#include <algorithm>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> graphError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

}  // namespace

EditorResult<void> GraphDocument::createNode(GraphNodeRecord node) {
    if (node.id.empty() || node.type.empty() || nodes_.contains(node.id))
        return graphError<void>(EditorStatus::Conflict, "editor.graph.invalid-node",
                                "Graph node id/type is missing or the id already exists");
    for (GraphPinRecord& pin : node.pins) {
        if (pin.id.empty())
            return graphError<void>(EditorStatus::Rejected, "editor.graph.invalid-pin", "Graph pin id is required");
        pin.node = node.id;
        if (findPin(pin.id))
            return graphError<void>(EditorStatus::Conflict, "editor.graph.duplicate-pin",
                                    "Graph pin id already exists");
    }
    nodes_.emplace(node.id, std::move(node));
    ++revision_;
    return EditorResult<void>::applied();
}

EditorResult<void> GraphDocument::deleteNode(const GraphNodeId& node) {
    if (!nodes_.contains(node))
        return graphError<void>(EditorStatus::NotFound, "editor.graph.node-not-found", "Graph node does not exist");
    std::vector<GraphPinId> pins;
    for (const GraphPinRecord& pin : nodes_.at(node).pins) pins.push_back(pin.id);
    std::erase_if(edges_, [&](const auto& edge) {
        return std::find(pins.begin(), pins.end(), edge.second.from) != pins.end() ||
               std::find(pins.begin(), pins.end(), edge.second.to) != pins.end();
    });
    nodes_.erase(node);
    ++revision_;
    return EditorResult<void>::applied();
}

EditorResult<void> GraphDocument::connect(GraphEdgeRecord edge, const GraphConnectionDecision& decision) {
    if (!decision.allowed) {
        EditorResult<void> result;
        result.status      = EditorStatus::Rejected;
        result.diagnostics = decision.diagnostics;
        if (result.diagnostics.empty())
            result.diagnostics.push_back(
                {RuleId("editor.graph.connection-rejected"), DiagnosticSeverity::Error, "Connection was rejected"});
        return result;
    }
    if (edge.id.empty() || edges_.contains(edge.id) || !findPin(edge.from) || !findPin(edge.to))
        return graphError<void>(EditorStatus::Rejected, "editor.graph.invalid-edge",
                                "Graph edge id/pins are invalid or duplicated");
    edges_.emplace(edge.id, std::move(edge));
    ++revision_;
    return EditorResult<void>::applied();
}

EditorResult<void> GraphDocument::disconnect(const StableId& edge) {
    if (!edges_.erase(edge))
        return graphError<void>(EditorStatus::NotFound, "editor.graph.edge-not-found", "Graph edge does not exist");
    ++revision_;
    return EditorResult<void>::applied();
}

EditorResult<void> GraphDocument::setNodeProperties(const GraphNodeId& node, EditorValue properties) {
    auto found = nodes_.find(node);
    if (found == nodes_.end())
        return graphError<void>(EditorStatus::NotFound, "editor.graph.node-not-found", "Graph node does not exist");
    if (properties.type() != EditorValue::Type::Object)
        return graphError<void>(EditorStatus::Rejected, "editor.graph.invalid-properties",
                                "Graph node properties must be an object");
    found->second.properties = std::move(properties);
    ++revision_;
    return EditorResult<void>::applied();
}

GraphDocumentData GraphDocument::snapshot(std::string domain) const {
    GraphDocumentData result;
    result.domain   = std::move(domain);
    result.revision = revision_;
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

GraphConnectionDecision MaterialGraphDomain::canConnect(const GraphPinRecord& from, const GraphPinRecord& to) const {
    GraphConnectionDecision decision;
    decision.allowed = from.direction == GraphPinDirection::Output && to.direction == GraphPinDirection::Input &&
                       from.type == to.type && from.node != to.node;
    if (!decision.allowed)
        decision.diagnostics.push_back({RuleId("editor.material.pin-type-mismatch"), DiagnosticSeverity::Error,
                                        "Material pins require matching types, output-to-input and distinct nodes"});
    return decision;
}

MaterialCompileResult MaterialGraphDomain::compile(const GraphDocumentData& graph) const {
    MaterialCompileResult result;
    result.documentRevision = graph.revision;
    if (graph.domain != domain()) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back({RuleId("editor.material.wrong-domain"), DiagnosticSeverity::Error,
                                      "Graph is not a material domain document"});
        return result;
    }
    const std::size_t outputs =
        static_cast<std::size_t>(std::count_if(graph.nodes.begin(), graph.nodes.end(), [](const GraphNodeRecord& node) {
            return node.type == "material.output";
        }));
    const bool errorNode = std::any_of(graph.nodes.begin(), graph.nodes.end(),
                                       [](const GraphNodeRecord& node) { return node.type == "material.error"; });
    if (outputs != 1 || errorNode) {
        result.status = EditorStatus::Failed;
        result.diagnostics.push_back({RuleId("editor.material.compile-failed"), DiagnosticSeverity::Error,
                                      outputs != 1 ? "Material graph requires exactly one output node"
                                                   : "Material graph contains an invalid node"});
        return result;
    }
    result.status         = EditorStatus::Applied;
    result.shaderArtifact = "material-shader:r" + std::to_string(graph.revision) + ":n" +
                            std::to_string(graph.nodes.size()) + ":e" + std::to_string(graph.edges.size());
    return result;
}

EditorResult<TaskId> MaterialEditorService::compile(const DocumentId& document, const GraphDocumentData& graph,
                                                    const MaterialGraphDomain& domain) {
    if (document.empty())
        return graphError<TaskId>(EditorStatus::Rejected, "editor.material.missing-document",
                                  "Material document id is required");
    TaskId task(document.value() + ".compile." + std::to_string(++taskSequence_));
    tasks_.emplace(task, Task{document, domain.compile(graph)});
    return EditorResult<TaskId>::applied(std::move(task));
}

EditorResult<MaterialCompileResult> MaterialEditorService::result(const TaskId& task) const {
    auto found = tasks_.find(task);
    if (found == tasks_.end())
        return graphError<MaterialCompileResult>(EditorStatus::NotFound, "editor.material.task-not-found",
                                                 "Material compile task does not exist");
    return EditorResult<MaterialCompileResult>::applied(found->second.result);
}

EditorResult<void> MaterialEditorService::publishPreview(const DocumentId& document, Revision currentRevision,
                                                         const TaskId& task) {
    auto found = tasks_.find(task);
    if (found == tasks_.end() || found->second.document != document)
        return graphError<void>(EditorStatus::NotFound, "editor.material.task-not-found",
                                "Material compile task does not belong to this document");
    const MaterialCompileResult& compiled = found->second.result;
    if (compiled.status != EditorStatus::Applied)
        return graphError<void>(EditorStatus::Rejected, "editor.material.compile-not-successful",
                                "Failed material output cannot replace the current preview");
    if (compiled.documentRevision != currentRevision)
        return graphError<void>(EditorStatus::Conflict, "editor.material.stale-compile",
                                "Material changed after this compile task started");
    previews_.insert_or_assign(document, compiled.shaderArtifact);
    return EditorResult<void>::applied();
}

std::string MaterialEditorService::previewArtifact(const DocumentId& document) const {
    auto found = previews_.find(document);
    return found == previews_.end() ? std::string{} : found->second;
}

}  // namespace eve::editor
