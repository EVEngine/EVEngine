#pragma once

#include "editor/EditorGraph.h"

#include <string>
#include <vector>

namespace eve::dialogue {
class ConversationDocument;
}

namespace eve::editor {

/** @brief Validated, runtime-neutral conversation definition compiled from a graph. */
struct DialogueGraphCompileResult {
    EditorStatus status = EditorStatus::Failed;
    Revision documentRevision = 0;
    EditorValue definition;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief `dialogue.conversation` domain for line, choice, branch and action flow. */
class DialogueGraphDomain final : public IGraphDomainProvider {
public:
    std::string domain() const override { return "dialogue.conversation"; }
    GraphConnectionDecision canConnect(const GraphPinRecord& from,
                                       const GraphPinRecord& to) const override;
    /** @brief Construct a line, branch, choice, call, command, wait, or end node. */
    EditorResult<GraphNodeRecord> makeNode(const GraphNodeId& id, const std::string& kind) const;
    /** @brief Construct one stable labelled route for a branch or choice node. */
    EditorResult<GraphNodeRecord> makeRouteNode(const GraphNodeId& id,
                                                const std::string& label = {}) const;
    /** @brief Validate and compile the graph into a deterministic value definition. */
    DialogueGraphCompileResult compile(const GraphDocumentData& graph) const;
};

/** @brief Optional bridge constructing the dialogue module's editable document. */
class DialogueGraphRuntimeBuilder {
public:
    /** @brief Build a validated ConversationDocument; caller owns the returned document. */
    EditorResult<dialogue::ConversationDocument*> build(const GraphDocumentData& graph) const;
};

}  // namespace eve::editor
