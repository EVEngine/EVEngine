#include "npc_ai_editing/BehaviorGraph.h"

#include <algorithm>
#include <functional>
#include <set>

namespace eve::npc_ai_editing {

GraphConnectionDecision BehaviorGraphDomain::canConnect(const GraphPinRecord& from,
                                                         const GraphPinRecord& to) const {
    GraphConnectionDecision result;
    if (from.direction != GraphPinDirection::Output || to.direction != GraphPinDirection::Input)
        result.diagnostics.push_back({RuleId("editor.behavior.pin-direction"), DiagnosticSeverity::Error,
                                      "Behavior edges must connect an output to an input"});
    else if (from.node == to.node)
        result.diagnostics.push_back({RuleId("editor.behavior.self-edge"), DiagnosticSeverity::Error,
                                      "Behavior nodes cannot connect to themselves"});
    else if (from.type != "flow" || to.type != "flow")
        result.diagnostics.push_back({RuleId("editor.behavior.pin-type"), DiagnosticSeverity::Error,
                                      "Behavior control edges require flow pins"});
    result.allowed = result.diagnostics.empty();
    return result;
}

BehaviorCompileResult BehaviorGraphDomain::compile(const GraphDocumentData& graph) const {
    BehaviorCompileResult result; result.documentRevision = graph.revision;
    if (graph.domain != domain()) {
        result.diagnostics.push_back({RuleId("editor.behavior.domain"), DiagnosticSeverity::Error,
                                      "Graph domain must be behavior"}); return result;
    }
    std::map<GraphNodeId, const GraphNodeRecord*> nodes;
    std::map<GraphPinId, GraphNodeId> pins;
    std::vector<GraphNodeId> roots;
    static const std::set<std::string> supported{"root", "sequence", "selector", "condition", "action"};
    for (const auto& node : graph.nodes) {
        if (!nodes.emplace(node.id, &node).second)
            result.diagnostics.push_back({RuleId("editor.behavior.duplicate-node"), DiagnosticSeverity::Error,
                                          "Behavior node ids must be unique"});
        if (!supported.contains(node.type))
            result.diagnostics.push_back({RuleId("editor.behavior.unsupported-node"), DiagnosticSeverity::Error,
                                          "Unsupported behavior node type: " + node.type});
        if (node.type == "root") roots.push_back(node.id);
        for (const auto& pin : node.pins) pins.emplace(pin.id, node.id);
    }
    if (roots.size() != 1)
        result.diagnostics.push_back({RuleId("editor.behavior.root-count"), DiagnosticSeverity::Error,
                                      "Behavior graph requires exactly one root"});
    std::map<GraphNodeId, std::vector<GraphNodeId>> children;
    std::map<GraphNodeId, int> parents;
    for (const auto& edge : graph.edges) {
        const auto from = pins.find(edge.from), to = pins.find(edge.to);
        if (from == pins.end() || to == pins.end()) {
            result.diagnostics.push_back({RuleId("editor.behavior.dangling-edge"), DiagnosticSeverity::Error,
                                          "Behavior edge references a missing pin"}); continue;
        }
        children[from->second].push_back(to->second); ++parents[to->second];
    }
    for (const auto& [node, count] : parents) if (count > 1)
        result.diagnostics.push_back({RuleId("editor.behavior.multiple-parents"), DiagnosticSeverity::Error,
                                      "Behavior node " + node.value() + " has multiple parents"});
    if (!roots.empty() && parents[roots.front()] != 0)
        result.diagnostics.push_back({RuleId("editor.behavior.root-parent"), DiagnosticSeverity::Error,
                                      "Behavior root cannot have a parent"});
    std::set<GraphNodeId> visiting, visited;
    std::function<void(const GraphNodeId&)> visit = [&](const GraphNodeId& id) {
        if (visiting.contains(id)) {
            result.diagnostics.push_back({RuleId("editor.behavior.cycle"), DiagnosticSeverity::Error,
                                          "Behavior graph contains a cycle"}); return;
        }
        if (visited.contains(id)) return;
        visiting.insert(id);
        for (const auto& child : children[id]) visit(child);
        visiting.erase(id); visited.insert(id);
    };
    if (!roots.empty()) visit(roots.front());
    for (const auto& [id, node] : nodes) {
        if (!visited.contains(id)) result.diagnostics.push_back({RuleId("editor.behavior.unreachable-node"),
            DiagnosticSeverity::Error, "Behavior node " + id.value() + " is unreachable from root"});
        const std::size_t count = children[id].size();
        if ((node->type == "root" && count != 1) ||
            ((node->type == "condition" || node->type == "action") && count != 0) ||
            ((node->type == "sequence" || node->type == "selector") && count == 0))
            result.diagnostics.push_back({RuleId("editor.behavior.child-count"), DiagnosticSeverity::Error,
                                          "Behavior node has an invalid number of children: " + id.value()});
    }
    if (std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const EditorDiagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Error;
        })) return result;
    result.root = roots.front();
    std::function<void(const GraphNodeId&)> emit = [&](const GraphNodeId& id) {
        const auto* node = nodes.at(id);
        result.instructions.push_back({id, node->type, node->properties, children[id]});
        for (const auto& child : children[id]) emit(child);
    };
    emit(result.root); result.status = EditorStatus::Applied; return result;
}

}  // namespace eve::npc_ai_editing
