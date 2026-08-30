#include "material_editing/MaterialGraph.h"

#include <algorithm>

namespace eve::material_editing {
namespace {

template <class T>
EditorResult<T> graphError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

}  // namespace

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
                                                         const MaterialGraphDomain& domain, TaskService& tasks) {
    if (document.empty())
        return graphError<TaskId>(EditorStatus::Rejected, "editor.material.missing-document",
                                  "Material document id is required");
    const MaterialGraphDomain domainCopy = domain;
    auto queued = tasks.submit("Compile material " + document.value(), [graph = std::move(graph),
                                                                        domainCopy](const TaskContext& context) {
        TaskOutcome outcome;
        if (context.isCancellationRequested()) {
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
        if (snapshot.value->state == TaskState::Queued || snapshot.value->state == TaskState::Running) {
            EditorResult<MaterialCompileResult> pending;
            pending.status = EditorStatus::Pending;
            return pending;
        }
        MaterialCompileResult compiled;
        compiled.diagnostics = snapshot.value->diagnostics;
        if (snapshot.value->state == TaskState::Cancelled) {
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

}  // namespace eve::material_editing
