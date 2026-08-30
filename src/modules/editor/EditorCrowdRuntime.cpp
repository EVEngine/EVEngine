#include "editor/EditorCrowdDocument.h"

#include "crowd/Crowd.h"

namespace eve::editor {

EditorResult<void> CrowdRuntimeApplier::apply(const CrowdDocumentTarget& document,
                                              crowd::Crowd* runtime) const {
    if (!runtime)
        return EditorResult<void>::error(EditorStatus::Rejected, RuleId("editor.crowd.runtime"),
                                         "Live Crowd runtime is required");
    const auto diagnostics = document.validate();
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            EditorResult<void> result; result.status = EditorStatus::Rejected;
            result.diagnostics = diagnostics; return result;
        }
    const auto agents = document.agents();
    if (agents.size() > static_cast<std::size_t>(runtime->getMaxAgents()))
        return EditorResult<void>::error(EditorStatus::Rejected, RuleId("editor.crowd.runtime-capacity"),
                                         "Crowd document exceeds the runtime agent capacity");
    std::map<StableId, CrowdPathRecord> paths;
    for (const auto& path : document.paths()) paths.emplace(path.id, path);
    runtime->clearAgents();
    for (const auto& agent : agents) {
        const int index = runtime->addNamedAgent(agent.id.value(), static_cast<float>(agent.x),
            static_cast<float>(agent.y), static_cast<float>(agent.heading), static_cast<float>(agent.radius));
        if (index < 0) {
            runtime->clearAgents();
            return EditorResult<void>::error(EditorStatus::Failed, RuleId("editor.crowd.runtime-capacity"),
                                              "Crowd runtime rejected an agent or reached capacity");
        }
        runtime->setAgentSpeed(index, static_cast<float>(agent.maximumSpeed));
        std::string action = agent.behavior;
        if (action == "path") {
            const auto found = paths.find(agent.path);
            if (found == paths.end() || found->second.points.empty()) {
                runtime->clearAgents();
                return EditorResult<void>::error(EditorStatus::Conflict, RuleId("editor.crowd.runtime-path"),
                                                  "Path agent has no runtime waypoint");
            }
            action = "seek";
            runtime->setAgentTarget(index, static_cast<float>(found->second.points.front().x),
                                    static_cast<float>(found->second.points.front().y));
        }
        if (!runtime->setAgentAction(index, action)) {
            runtime->clearAgents();
            return EditorResult<void>::error(EditorStatus::Failed, RuleId("editor.crowd.runtime-action"),
                                              "Crowd runtime rejected an agent behavior");
        }
    }
    return EditorResult<void>::applied();
}

}  // namespace eve::editor
