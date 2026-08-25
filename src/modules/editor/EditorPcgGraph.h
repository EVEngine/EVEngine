#pragma once

#include "editor/EditorGraph.h"

#include <string>
#include <vector>

namespace eve::editor {

/** @brief Result of compiling a generic editor graph into a PointGraph asset definition. */
struct PcgGraphCompileResult {
    EditorStatus                  status           = EditorStatus::Failed;
    Revision                      documentRevision = 0;
    std::string                   definition;
    std::vector<EditorDiagnostic> diagnostics;
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
    /** @brief Compile topology and scalar properties into a reusable PointGraph definition. */
    PcgGraphCompileResult compile(const GraphDocumentData& graph) const;
};

}  // namespace eve::editor
