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

EditorResult<void> GraphDocument::setParameters(EditorValue parameters) {
    if (parameters.type() != EditorValue::Type::Object)
        return graphError<void>(EditorStatus::Rejected, "editor.graph.invalid-parameters",
                                "Graph parameters must be an object");
    if (!parameters.isWithinLimits(4, 4096, 256 * 1024))
        return graphError<void>(EditorStatus::Rejected, "editor.graph.parameters-too-large",
                                "Graph parameters exceed editor document limits");
    parameters_ = std::move(parameters);
    ++revision_;
    return EditorResult<void>::applied();
}

GraphDocumentData GraphDocument::snapshot(std::string domain) const {
    GraphDocumentData result;
    result.domain   = std::move(domain);
    result.revision = revision_;
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
    tasks_.emplace(task, Task{document, domain.compile(graph), nullptr});
    return EditorResult<TaskId>::applied(std::move(task));
}

EditorResult<TaskId> MaterialEditorService::compileAsync(const DocumentId& document, GraphDocumentData graph,
                                                         const MaterialGraphDomain& domain, EditorTaskService& tasks) {
    if (document.empty())
        return graphError<TaskId>(EditorStatus::Rejected, "editor.material.missing-document",
                                  "Material document id is required");
    const MaterialGraphDomain domainCopy = domain;
    auto queued = tasks.submit("Compile material " + document.value(), [graph = std::move(graph),
                                                                        domainCopy](const EditorTaskContext& context) {
        EditorTaskOutcome outcome;
        if (context.cancelled()) {
            outcome.status = EditorStatus::Cancelled;
            return outcome;
        }
        context.reportProgress(0.25F);
        const MaterialCompileResult compiled = domainCopy.compile(graph);
        EditorValue::Object         value;
        value["status"]     = EditorValue(static_cast<std::int64_t>(compiled.status));
        value["revision"]   = EditorValue(static_cast<std::int64_t>(compiled.documentRevision));
        value["artifact"]   = EditorValue(compiled.shaderArtifact);
        outcome.status      = compiled.status;
        outcome.output      = EditorValue(std::move(value));
        outcome.diagnostics = compiled.diagnostics;
        context.reportProgress(1.0F);
        return outcome;
    });
    if (!queued.isAccepted() || !queued.value) return queued;
    tasks_.emplace(*queued.value, Task{document, {}, &tasks});
    return queued;
}

EditorResult<void> MaterialEditorService::cancel(const TaskId& task) {
    auto found = tasks_.find(task);
    if (found == tasks_.end() || !found->second.service)
        return graphError<void>(EditorStatus::NotFound, "editor.material.async-task-not-found",
                                "Asynchronous material task does not exist");
    return found->second.service->cancel(task);
}

EditorResult<MaterialCompileResult> MaterialEditorService::result(const TaskId& task) const {
    auto found = tasks_.find(task);
    if (found == tasks_.end())
        return graphError<MaterialCompileResult>(EditorStatus::NotFound, "editor.material.task-not-found",
                                                 "Material compile task does not exist");
    if (found->second.service) {
        const auto snapshot = found->second.service->snapshot(task);
        if (!snapshot.isAccepted() || !snapshot.value)
            return graphError<MaterialCompileResult>(EditorStatus::NotFound, "editor.material.task-not-found",
                                                     "Material compile task does not exist");
        if (snapshot.value->state == EditorTaskState::Queued || snapshot.value->state == EditorTaskState::Running) {
            EditorResult<MaterialCompileResult> pending;
            pending.status = EditorStatus::Pending;
            return pending;
        }
        MaterialCompileResult compiled;
        compiled.diagnostics = snapshot.value->diagnostics;
        if (snapshot.value->state == EditorTaskState::Cancelled) {
            compiled.status = EditorStatus::Cancelled;
            return EditorResult<MaterialCompileResult>::applied(std::move(compiled));
        }
        const auto* object = snapshot.value->output.getIf<EditorValue::Object>();
        if (!object)
            return graphError<MaterialCompileResult>(EditorStatus::Failed, "editor.material.invalid-task-output",
                                                     "Material worker returned an invalid result");
        const auto  status        = object->find("status");
        const auto  revision      = object->find("revision");
        const auto  artifact      = object->find("artifact");
        const auto* statusValue   = status == object->end() ? nullptr : status->second.getIf<std::int64_t>();
        const auto* revisionValue = revision == object->end() ? nullptr : revision->second.getIf<std::int64_t>();
        const auto* artifactValue = artifact == object->end() ? nullptr : artifact->second.getIf<std::string>();
        if (!statusValue || !revisionValue || !artifactValue)
            return graphError<MaterialCompileResult>(EditorStatus::Failed, "editor.material.invalid-task-output",
                                                     "Material worker result fields are invalid");
        compiled.status           = static_cast<EditorStatus>(*statusValue);
        compiled.documentRevision = static_cast<Revision>(*revisionValue);
        compiled.shaderArtifact   = *artifactValue;
        return EditorResult<MaterialCompileResult>::applied(std::move(compiled));
    }
    return EditorResult<MaterialCompileResult>::applied(found->second.result);
}

EditorResult<void> MaterialEditorService::publishPreview(const DocumentId& document, Revision currentRevision,
                                                         const TaskId& task) {
    auto found = tasks_.find(task);
    if (found == tasks_.end() || found->second.document != document)
        return graphError<void>(EditorStatus::NotFound, "editor.material.task-not-found",
                                "Material compile task does not belong to this document");
    const auto compileResult = result(task);
    if (compileResult.status == EditorStatus::Pending)
        return graphError<void>(EditorStatus::Conflict, "editor.material.compile-pending",
                                "Material compile task has not completed");
    if (!compileResult.isAccepted() || !compileResult.value)
        return graphError<void>(EditorStatus::Failed, "editor.material.compile-result-unavailable",
                                "Material compile result is unavailable");
    const MaterialCompileResult& compiled = *compileResult.value;
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
