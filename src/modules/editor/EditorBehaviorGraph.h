#pragma once

#include "editor/EditorGraph.h"

#include <map>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Deterministic behavior-graph instruction consumed by runtime adapters. */
struct BehaviorInstruction {
    GraphNodeId node;
    std::string opcode;
    EditorValue parameters;
    std::vector<GraphNodeId> children;
};

/** @brief Immutable compiled behavior graph with source revision. */
struct BehaviorCompileResult {
    EditorStatus status = EditorStatus::Failed;
    Revision documentRevision = 0;
    GraphNodeId root;
    std::vector<BehaviorInstruction> instructions;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Typed behavior-tree/decision-graph connection and compilation policy. */
class BehaviorGraphDomain final : public IGraphDomainProvider {
public:
    std::string domain() const override { return "behavior"; }
    GraphConnectionDecision canConnect(const GraphPinRecord& from,
                                       const GraphPinRecord& to) const override;
    /** @brief Validate and compile root/sequence/selector/condition/action nodes. */
    BehaviorCompileResult compile(const GraphDocumentData& graph) const;
};

}  // namespace eve::editor
