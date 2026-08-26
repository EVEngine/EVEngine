#pragma once

#include "editor/EditorGraph.h"

#include <string>
#include <vector>

namespace eve::procgen {
class PointSet;
}

namespace eve::editor {

/** @brief Result of compiling a generic editor graph into a PointGraph asset definition. */
struct PcgGraphCompileResult {
    EditorStatus                  status           = EditorStatus::Failed;
    Revision                      documentRevision = 0;
    std::string                   definition;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Transactional result of upgrading an editor PCG graph to the current schema. */
struct PcgGraphMigrationResult {
    EditorStatus                  status      = EditorStatus::Failed;
    std::uint32_t                 fromVersion = 0;
    std::uint32_t                 toVersion   = 1;
    GraphDocumentData             graph;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief One node's isolated PointGraph preview telemetry for graph overlays. */
struct PcgGraphPreviewMetric {
    std::string nodeId;
    int         outputCount    = 0;
    float       milliseconds   = 0.f;
    bool        cacheHit       = false;
    float       minX           = 0.f;
    float       minY           = 0.f;
    float       minZ           = 0.f;
    float       maxX           = 0.f;
    float       maxY           = 0.f;
    float       maxZ           = 0.f;
    float       averageDensity = 0.f;
};

/** @brief Revision-tagged result of compiling and executing an isolated editor preview. */
struct PcgGraphPreviewResult {
    EditorStatus                       status           = EditorStatus::Failed;
    Revision                           documentRevision = 0;
    int                                outputCount      = 0;
    std::vector<PcgGraphPreviewMetric> metrics;
    std::vector<EditorDiagnostic>      diagnostics;
};

/**
 * @brief `procgen.point` editor graph domain backed by procgen::PointGraph schema.
 *
 * This optional bridge is compiled only when both editor and procgen modules
 * are enabled. It creates typed pins/default properties from runtime operation
 * reflection and compiles the neutral GraphDocument into a versioned asset.
 */
class PcgPointGraphDomain final : public IGraphDomainProvider {
public:
    std::string domain() const override { return "procgen.point"; }
    GraphConnectionDecision canConnect(const GraphPinRecord& from,
                                       const GraphPinRecord& to) const override;
    /** @brief Construct a generic editor node from one reflected PointGraph operation. */
    EditorResult<GraphNodeRecord> makeNode(const GraphNodeId& id,
                                           const std::string& operation) const;
    /** @brief Upgrade a legacy graph without mutating the source document. */
    PcgGraphMigrationResult migrate(const GraphDocumentData& graph) const;
    /** @brief Compile topology and scalar properties into a reusable PointGraph definition. */
    PcgGraphCompileResult compile(const GraphDocumentData& graph) const;
    /**
     * @brief Compile and execute an isolated point-input preview for editor visualization.
     * @param graph Immutable graph document snapshot.
     * @param inputNode PointGraph input node receiving previewInput.
     * @param previewInput Non-owning point data used only for this execution.
     * @param outputNode Node whose evaluated output determines outputCount.
     * @param nodeBudget Maximum newly evaluated nodes, or zero for unlimited.
     * @param pointBudget Maximum points cached by one node, or zero for unlimited.
     */
    PcgGraphPreviewResult preview(const GraphDocumentData& graph, const std::string& inputNode,
                                  procgen::PointSet* previewInput,
                                  const std::string& outputNode, int nodeBudget = 0,
                                  int pointBudget = 100000) const;
};

}  // namespace eve::editor
