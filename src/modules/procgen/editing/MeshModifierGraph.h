#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/editing/ProcgenEditingTypes.h"

#include <string>
#include <vector>

namespace eve::procgen_editing {

/** @brief Structural compilation result for a `procgen.meshModifier` graph document. */
struct MeshModifierGraphCompileResult {
    EditorStatus                  status           = EditorStatus::Failed;
    Revision                      documentRevision = 0;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Revision-tagged owning preview from a mesh modifier graph document. */
struct MeshModifierGraphPreviewResult {
    EditorStatus                  status           = EditorStatus::Failed;
    Revision                      documentRevision = 0;
    procgen::MeshBuild            mesh;
    int                           segmentCount        = 0;
    int                           fusedOperationCount = 0;
    std::vector<EditorDiagnostic> diagnostics;
};

/**
 * @brief UI-independent editor domain for the procgen mesh modifier graph.
 *
 * GraphDocument owns authoring state. Preview compiles an isolated runtime graph,
 * binds a copied input mesh, and publishes an owning output only after success.
 */
class MeshModifierGraphDomain final : public IGraphDomainProvider {
public:
    /** @brief Stable graph document domain id. */
    [[nodiscard]] std::string domain() const override { return "procgen.meshModifier"; }
    /** @brief Allow only mesh output-to-input edges between distinct nodes. */
    [[nodiscard]] GraphConnectionDecision canConnect(const GraphPinRecord& from,
                                                     const GraphPinRecord& to) const override;
    /** @brief Construct an editor node from the runtime operation catalogue. */
    [[nodiscard]] EditorResult<GraphNodeRecord> makeNode(const GraphNodeId& id, const std::string& operation) const;
    /** @brief Validate schema, node properties, edges, and graph topology without executing. */
    [[nodiscard]] MeshModifierGraphCompileResult compile(const GraphDocumentData& graph) const;
    /**
     * @brief Compile and execute an isolated preview.
     * @param graph Immutable editor graph snapshot with schema version 1.
     * @param inputNode `mesh.input` node receiving the copied source mesh, or empty for generator-only graphs.
     * @param input Owning source data is copied when inputNode is not empty; the borrow lasts only for this call.
     * @param outputNode Requested graph output.
     */
    [[nodiscard]] MeshModifierGraphPreviewResult preview(const GraphDocumentData& graph, const std::string& inputNode,
                                                         const procgen::MeshBuild& input,
                                                         const std::string&        outputNode) const;
};

}  // namespace eve::procgen_editing
