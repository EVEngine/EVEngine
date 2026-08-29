#pragma once

#include "editor/EditorDocumentService.h"
#include "editor/EditorTaskService.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::editor {

/** @brief Direction of a typed graph pin. */
enum class GraphPinDirection { Input, Output };

/** @brief Stable typed pin owned by a graph node. */
struct GraphPinRecord {
    GraphPinId        id;
    GraphNodeId       node;
    std::string       type;
    GraphPinDirection direction = GraphPinDirection::Input;
};

/** @brief Generic graph node independent of its material/animation/etc. domain. */
struct GraphNodeRecord {
    GraphNodeId                 id;
    std::string                 type;
    EditorValue                 properties;
    std::vector<GraphPinRecord> pins;
};

/** @brief Directed connection between an output and input pin. */
struct GraphEdgeRecord {
    StableId   id;
    GraphPinId from;
    GraphPinId to;
};

/** @brief Immutable graph document value sent to a domain compiler. */
struct GraphDocumentData {
    std::string                  domain;
    std::uint32_t                schemaVersion = 1;
    Revision                     revision      = 0;
    std::vector<GraphNodeRecord> nodes;
    std::vector<GraphEdgeRecord> edges;
    EditorValue                  parameters;
};

/** @brief Result of domain-specific connection validation. */
struct GraphConnectionDecision {
    bool                          allowed = false;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Generic mutable graph model; all mutations advance one revision clock. */
class GraphDocument {
public:
    /** @brief Create a node with stable pins. */
    EditorResult<void> createNode(GraphNodeRecord node);
    /** @brief Delete a node and every incident edge. */
    EditorResult<void> deleteNode(const GraphNodeId& node);
    /** @brief Connect two pins after domain validation. */
    EditorResult<void> connect(GraphEdgeRecord edge, const GraphConnectionDecision& decision);
    /** @brief Disconnect one edge. */
    EditorResult<void> disconnect(const StableId& edge);
    /** @brief Replace one node's structured property object. */
    EditorResult<void> setNodeProperties(const GraphNodeId& node, EditorValue properties);
    /** @brief Replace domain-specific graph parameters stored with the document. */
    EditorResult<void> setParameters(EditorValue parameters);
    /** @brief Capture deterministic graph values for compilation/persistence. */
    GraphDocumentData snapshot(std::string domain) const;
    /** @brief Return the current graph edit revision. */
    Revision revision() const { return revision_; }
    /** @brief Find a pin snapshot or nullptr. */
    const GraphPinRecord* findPin(const GraphPinId& pin) const;

private:
    std::map<GraphNodeId, GraphNodeRecord> nodes_;
    std::map<StableId, GraphEdgeRecord>    edges_;
    EditorValue                            parameters_;
    Revision                               revision_ = 0;
};

/** @brief Generic graph domain contract that owns type/connection/compile semantics. */
class IGraphDomainProvider {
public:
    virtual ~IGraphDomainProvider() = default;
    /** @brief Return the stable graph domain id. */
    virtual std::string domain() const = 0;
    /** @brief Validate a proposed typed connection. */
    virtual GraphConnectionDecision canConnect(const GraphPinRecord& from, const GraphPinRecord& to) const = 0;
};

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
                                      const MaterialGraphDomain& domain, EditorTaskService& tasks);
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
        EditorTaskService*    service = nullptr;
    };

    std::unordered_map<TaskId, Task, StrongEditorIdHash<TaskId>>                tasks_;
    std::unordered_map<DocumentId, std::string, StrongEditorIdHash<DocumentId>> previews_;
    std::uint64_t                                                               taskSequence_ = 0;
};

}  // namespace eve::editor
