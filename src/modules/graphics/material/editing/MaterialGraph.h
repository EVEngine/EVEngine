#pragma once

#include "editing/EditingGraph.h"
#include "editing/EditingTaskService.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::material_editing {

using editing::Diagnostic;
using editing::DiagnosticSeverity;
using editing::DocumentId;
using editing::GraphConnectionDecision;
using editing::GraphDocument;
using editing::GraphDocumentData;
using editing::GraphEdgeRecord;
using editing::GraphNodeRecord;
using editing::GraphPinDirection;
using editing::GraphPinRecord;
using editing::IGraphDomainProvider;
using editing::Revision;
using editing::RuleId;
using editing::Status;
using editing::StableId;
using editing::StrongIdHash;
using editing::TaskContext;
using editing::TaskId;
using editing::TaskOutcome;
using editing::TaskService;
using editing::TaskState;
using editing::Value;
template <class T>
using EditorResult = editing::Result<T>;
using EditorStatus = editing::Status;
using EditorValue = editing::Value;
using EditorDiagnostic = editing::Diagnostic;

/** @brief Compatibility aliases; canonical graph contracts live in editing. */
using GraphPinDirection       = editing::GraphPinDirection;
using GraphPinRecord          = editing::GraphPinRecord;
using GraphNodeRecord         = editing::GraphNodeRecord;
using GraphEdgeRecord         = editing::GraphEdgeRecord;
using GraphDocumentData       = editing::GraphDocumentData;
using GraphConnectionDecision = editing::GraphConnectionDecision;
using GraphDocument           = editing::GraphDocument;
using IGraphDomainProvider    = editing::IGraphDomainProvider;

/** @brief Material graph compilation value; artifact is published separately. */
struct MaterialCompileResult {
    EditorStatus                  status           = EditorStatus::Failed;
    Revision                      documentRevision = 0;
    std::string                   shaderArtifact;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Minimal material domain with typed pin validation and deterministic compile artifacts. */
class MaterialGraphDomain final : public IGraphDomainProvider {
public:
    std::string             domain() const override { return "material"; }
    GraphConnectionDecision canConnect(const GraphPinRecord& from, const GraphPinRecord& to) const override;
    /** @brief Compile a material graph without mutating preview state. */
    MaterialCompileResult compile(const GraphDocumentData& graph) const;
};

/**
 * @brief Revision-aware material compile result and preview publication service.
 *
 * Both synchronous compatibility and real background compilation share the
 * same result/publication contract.
 */
class MaterialEditorService {
public:
    /** @brief Compile and cache a task result. */
    EditorResult<TaskId> compile(const DocumentId& document, const GraphDocumentData& graph,
                                 const MaterialGraphDomain& domain);
    /** @brief Queue a cancellable compile using an immutable graph snapshot. */
    EditorResult<TaskId> compileAsync(const DocumentId& document, GraphDocumentData graph,
                                      const MaterialGraphDomain& domain, TaskService& tasks);
    /** @brief Request cancellation of an asynchronous compile task. */
    EditorResult<void> cancel(const TaskId& task);
    /** @brief Return one cached compile task result. */
    EditorResult<MaterialCompileResult> result(const TaskId& task) const;
    /** @brief Publish only a successful artifact compiled from the current revision. */
    EditorResult<void> publishPreview(const DocumentId& document, Revision currentRevision, const TaskId& task);
    /** @brief Return the last successfully published artifact, if any. */
    std::string previewArtifact(const DocumentId& document) const;

private:
    struct Task {
        DocumentId            document;
        MaterialCompileResult result;
        TaskService*          service = nullptr;
    };

    std::unordered_map<TaskId, Task, StrongIdHash<TaskId>>                tasks_;
    std::unordered_map<DocumentId, std::string, StrongIdHash<DocumentId>> previews_;
    std::uint64_t                                                               taskSequence_ = 0;
};

}  // namespace eve::material_editing
