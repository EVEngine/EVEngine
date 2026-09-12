#include "procgen/editing/MeshModifierGraph.h"

#include "procgen/editing/SplinePathDocument.h"
#include "procgen/mesh/MeshModifierGraph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace eve::procgen_editing {
namespace {

EditorDiagnostic meshGraphDiagnostic(const char* rule, DiagnosticSeverity severity, std::string message) {
    return eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument, RuleId(rule), severity,
                                        std::move(message));
}

template <class T>
EditorResult<T> meshGraphError(const char* rule, std::string message) {
    return eve::editing::failed<T>(EditorStatus::Rejected, RuleId(rule), std::move(message));
}

EditorValue defaultValue(const std::string& kind, const std::string& encoded) {
    if (kind == "int") return EditorValue(static_cast<std::int64_t>(std::stoll(encoded)));
    if (kind == "float") return EditorValue(std::stod(encoded));
    return EditorValue(encoded);
}

const GraphPinRecord* findPin(const GraphDocumentData& graph, const GraphPinId& id) {
    for (const auto& node : graph.nodes) {
        const auto found =
            std::find_if(node.pins.begin(), node.pins.end(), [&](const GraphPinRecord& pin) { return pin.id == id; });
        if (found != node.pins.end()) return &*found;
    }
    return nullptr;
}

const GraphNodeRecord* findNode(const GraphDocumentData& graph, const GraphNodeId& id) {
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                    [&](const GraphNodeRecord& node) { return node.id == id; });
    return found == graph.nodes.end() ? nullptr : &*found;
}

int inputIndex(const GraphNodeRecord& node, const GraphPinId& pin) {
    int index = 0;
    for (const auto& candidate : node.pins) {
        if (candidate.direction != GraphPinDirection::Input) continue;
        if (candidate.id == pin) return index;
        ++index;
    }
    return -1;
}

Result<void> validateTopology(const GraphDocumentData& graph) {
    std::unordered_map<std::string, const GraphNodeRecord*>   nodes;
    std::unordered_map<std::string, std::vector<std::string>> dependencies;
    std::unordered_set<std::string>                           connectedInputs;
    for (const auto& node : graph.nodes) {
        if (node.id.empty() || !nodes.emplace(node.id.value(), &node).second)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                           "mesh modifier node ids must be non-empty and unique",
                                                           node.id.value(), {}, "procgen.meshModifier.editor"));
        if (procgen::MeshModifierGraph::operationInputCount(node.type) < 0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown mesh modifier operation",
                                                           node.type, {}, "procgen.meshModifier.editor"));
    }
    for (const auto& edge : graph.edges) {
        const auto* from = findPin(graph, edge.from);
        const auto* to   = findPin(graph, edge.to);
        if (!from || !to || from->direction != GraphPinDirection::Output || to->direction != GraphPinDirection::Input ||
            from->type != "mesh" || to->type != "mesh" || from->node == to->node)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch, "invalid mesh modifier edge",
                                                           edge.id.value(), {}, "procgen.meshModifier.editor"));
        const auto* target = findNode(graph, to->node);
        if (!target || inputIndex(*target, to->id) < 0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                           "mesh modifier edge target input is missing",
                                                           edge.id.value(), {}, "procgen.meshModifier.editor"));
        if (!connectedInputs.insert(to->id.value()).second)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                           "mesh modifier input has multiple connections",
                                                           to->id.value(), {}, "procgen.meshModifier.editor"));
        dependencies[to->node.value()].push_back(from->node.value());
    }
    for (const auto& node : graph.nodes) {
        const int requiredInputs = procgen::MeshModifierGraph::operationInputCount(node.type);
        for (int input = 0; input < requiredInputs; ++input) {
            const auto pin = node.id.value() + ".in" + std::to_string(input);
            if (!connectedInputs.contains(pin))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                               "required mesh modifier input is disconnected", pin, {},
                                                               "procgen.meshModifier.editor"));
        }
    }
    std::unordered_set<std::string>                 visiting;
    std::unordered_set<std::string>                 visited;
    std::function<Result<void>(const std::string&)> visit = [&](const std::string& node) {
        if (visited.contains(node)) return Result<void>::success();
        if (!visiting.insert(node).second)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                           "mesh modifier graph contains a cycle", node, {},
                                                           "procgen.meshModifier.editor"));
        for (const auto& dependency : dependencies[node]) {
            auto result = visit(dependency);
            if (!result.ok()) return result;
        }
        visiting.erase(node);
        visited.insert(node);
        return Result<void>::success();
    };
    for (const auto& [id, unused] : nodes) {
        (void)unused;
        auto result = visit(id);
        if (!result.ok()) return result;
    }
    return Result<void>::success();
}

Result<void> populateRuntime(const GraphDocumentData& graph, procgen::MeshModifierGraph& runtime) {
    for (const auto& node : graph.nodes) {
        auto added = runtime.addNode(node.id.value(), node.type);
        if (!added.ok()) return added;
        const auto* properties = node.properties.getIf<EditorValue::Object>();
        if (!properties)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch,
                                                           "mesh modifier node properties must be an object",
                                                           node.id.value(), {}, "procgen.meshModifier.editor"));
        for (const auto& [key, value] : *properties) {
            if (key == "splinePath" && (node.type == "deform.splinePath" || node.type == "mesh.splineTube" ||
                                        node.type == "mesh.splineRibbon" || node.type == "mesh.splineExtrude")) {
                auto path = compileSplinePathSnapshot(value);
                if (!path.ok()) return Result<void>::failure(path.status());
                auto bound = runtime.setNodeSplinePath(node.id.value(), path.value());
                if (!bound.ok()) return bound;
                continue;
            }
            if (key == "splineProfile" && node.type == "mesh.splineExtrude") {
                const auto* object = value.getIf<EditorValue::Object>();
                if (!object)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch,
                                                                   "spline profile must be an object", node.id.value(),
                                                                   {}, "procgen.meshModifier.editor"));
                const auto  closedValue = object->find("closed");
                const auto  pointsValue = object->find("points");
                const auto* closed      = closedValue != object->end() ? closedValue->second.getIf<bool>() : nullptr;
                const auto* points =
                    pointsValue != object->end() ? pointsValue->second.getIf<EditorValue::Array>() : nullptr;
                if (!closed || !points)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch,
                                                                   "spline profile requires closed and points",
                                                                   node.id.value(), {}, "procgen.meshModifier.editor"));
                procgen::SplineProfile profile;
                profile.closed = *closed;
                for (const auto& pointValue : *points) {
                    const auto* point = pointValue.getIf<EditorValue::Object>();
                    if (!point)
                        return Result<void>::failure(
                            Diagnostic::error(DiagnosticCode::TypeMismatch, "spline profile point must be an object",
                                              node.id.value(), {}, "procgen.meshModifier.editor"));
                    const auto  xValue = point->find("x");
                    const auto  yValue = point->find("y");
                    const auto* x      = xValue != point->end() ? xValue->second.getIf<double>() : nullptr;
                    const auto* y      = yValue != point->end() ? yValue->second.getIf<double>() : nullptr;
                    if (!x || !y || !std::isfinite(*x) || !std::isfinite(*y))
                        return Result<void>::failure(
                            Diagnostic::error(DiagnosticCode::TypeMismatch, "spline profile points require finite x/y",
                                              node.id.value(), {}, "procgen.meshModifier.editor"));
                    profile.points.push_back({static_cast<float>(*x), static_cast<float>(*y)});
                }
                auto bound = runtime.setNodeSplineProfile(node.id.value(), profile);
                if (!bound.ok()) return bound;
                continue;
            }
            int       parameterIndex = -1;
            const int count          = procgen::MeshModifierGraph::operationParamCount(node.type);
            for (int index = 0; index < count; ++index)
                if (procgen::MeshModifierGraph::operationParamKey(node.type, index) == key) {
                    parameterIndex = index;
                    break;
                }
            if (parameterIndex < 0)
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "unknown mesh modifier property",
                                      node.id.value() + "." + key, {}, "procgen.meshModifier.editor"));
            const std::string kind          = procgen::MeshModifierGraph::operationParamKind(node.type, parameterIndex);
            auto              applyProperty = [&]() -> Result<void> {
                if (kind == "float") {
                    if (const auto* number = value.getIf<double>();
                        number && std::isfinite(*number) && std::abs(*number) <= std::numeric_limits<float>::max())
                        return runtime.setNodeFloat(node.id.value(), key, static_cast<float>(*number));
                } else if (kind == "int") {
                    if (const auto* integer = value.getIf<std::int64_t>();
                        integer && *integer >= std::numeric_limits<int>::min() &&
                        *integer <= std::numeric_limits<int>::max())
                        return runtime.setNodeInt(node.id.value(), key, static_cast<int>(*integer));
                } else if (kind == "string") {
                    if (const auto* text = value.getIf<std::string>())
                        return runtime.setNodeString(node.id.value(), key, *text);
                }
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::TypeMismatch, "mesh modifier property has the wrong type",
                                      node.id.value() + "." + key, {}, "procgen.meshModifier.editor"));
            };
            auto applied = applyProperty();
            if (!applied.ok()) return applied;
        }
    }
    for (const auto& edge : graph.edges) {
        const auto* from = findPin(graph, edge.from);
        const auto* to   = findPin(graph, edge.to);
        if (!from || !to)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                           "mesh modifier edge references a missing pin",
                                                           edge.id.value(), {}, "procgen.meshModifier.editor"));
        const auto* target = findNode(graph, to->node);
        if (!target)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                           "mesh modifier edge target node is missing", edge.id.value(),
                                                           {}, "procgen.meshModifier.editor"));
        auto connected = runtime.connect(from->node.value(), to->node.value(), inputIndex(*target, to->id));
        if (!connected.ok()) return connected;
    }
    return Result<void>::success();
}

}  // namespace

GraphConnectionDecision MeshModifierGraphDomain::canConnect(const GraphPinRecord& from,
                                                            const GraphPinRecord& to) const {
    GraphConnectionDecision decision;
    decision.allowed = from.direction == GraphPinDirection::Output && to.direction == GraphPinDirection::Input &&
                       from.type == "mesh" && to.type == "mesh" && from.node != to.node;
    if (!decision.allowed)
        decision.diagnostics.push_back(
            meshGraphDiagnostic("editor.mesh-modifier.invalid-connection", DiagnosticSeverity::Error,
                                "Mesh modifier connections require mesh output-to-input pins on distinct nodes"));
    return decision;
}

EditorResult<GraphNodeRecord> MeshModifierGraphDomain::makeNode(const GraphNodeId& id,
                                                                const std::string& operation) const {
    if (id.empty() || procgen::MeshModifierGraph::operationInputCount(operation) < 0)
        return meshGraphError<GraphNodeRecord>("editor.mesh-modifier.unknown-operation",
                                               "Unknown mesh modifier operation");
    GraphNodeRecord node;
    node.id   = id;
    node.type = operation;
    EditorValue::Object properties;
    const int           parameterCount = procgen::MeshModifierGraph::operationParamCount(operation);
    for (int i = 0; i < parameterCount; ++i) {
        const auto key  = procgen::MeshModifierGraph::operationParamKey(operation, i);
        properties[key] = defaultValue(procgen::MeshModifierGraph::operationParamKind(operation, i),
                                       procgen::MeshModifierGraph::operationParamDefault(operation, i));
    }
    if (operation == "deform.splinePath" || operation == "mesh.splineTube" || operation == "mesh.splineRibbon" ||
        operation == "mesh.splineExtrude") {
        SplinePathDocument path(id.value() + ".splinePath");
        properties["splinePath"] = path.snapshotValue();
    }
    if (operation == "mesh.splineExtrude") {
        EditorValue::Array points;
        for (const auto& point :
             std::array<std::array<double, 2>, 4>{{{{-0.5, -0.5}}, {{0.5, -0.5}}, {{0.5, 0.5}}, {{-0.5, 0.5}}}})
            points.emplace_back(EditorValue::Object{{"x", EditorValue(point[0])}, {"y", EditorValue(point[1])}});
        properties["splineProfile"] =
            EditorValue(EditorValue::Object{{"closed", EditorValue(true)}, {"points", EditorValue(std::move(points))}});
    }
    node.properties      = EditorValue(std::move(properties));
    const int inputCount = procgen::MeshModifierGraph::operationInputCount(operation);
    for (int input = 0; input < inputCount; ++input)
        node.pins.push_back(
            {GraphPinId(id.value() + ".in" + std::to_string(input)), id, "mesh", GraphPinDirection::Input});
    node.pins.push_back({GraphPinId(id.value() + ".out"), id, "mesh", GraphPinDirection::Output});
    return eve::editing::applied<GraphNodeRecord>(std::move(node));
}

MeshModifierGraphCompileResult MeshModifierGraphDomain::compile(const GraphDocumentData& graph) const {
    MeshModifierGraphCompileResult result;
    result.documentRevision = graph.revision;
    if (graph.domain != domain()) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(meshGraphDiagnostic("editor.mesh-modifier.wrong-domain", DiagnosticSeverity::Error,
                                                         "Graph is not a procgen.meshModifier document"));
        return result;
    }
    if (graph.schemaVersion != 1) {
        result.status = EditorStatus::Unsupported;
        result.diagnostics.push_back(meshGraphDiagnostic("editor.mesh-modifier.unsupported-schema",
                                                         DiagnosticSeverity::Error,
                                                         "Unsupported mesh modifier graph schema version"));
        return result;
    }
    auto topology = validateTopology(graph);
    if (!topology.ok()) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(meshGraphDiagnostic("editor.mesh-modifier.invalid-topology",
                                                         DiagnosticSeverity::Error, topology.status().describe()));
        return result;
    }
    procgen::MeshModifierGraph runtime;
    auto                       populated = populateRuntime(graph, runtime);
    if (!populated.ok()) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(meshGraphDiagnostic("editor.mesh-modifier.compile-failed",
                                                         DiagnosticSeverity::Error, populated.status().describe()));
        return result;
    }
    result.status = EditorStatus::Applied;
    return result;
}

MeshModifierGraphPreviewResult MeshModifierGraphDomain::preview(const GraphDocumentData&  graph,
                                                                const std::string&        inputNode,
                                                                const procgen::MeshBuild& input,
                                                                const std::string&        outputNode) const {
    MeshModifierGraphPreviewResult result;
    result.documentRevision = graph.revision;
    const auto compiled     = compile(graph);
    result.diagnostics      = compiled.diagnostics;
    if (compiled.status != EditorStatus::Applied) {
        result.status = compiled.status;
        return result;
    }
    procgen::MeshModifierGraph runtime;
    auto                       populated = populateRuntime(graph, runtime);
    if (!populated.ok()) {
        result.status = EditorStatus::Failed;
        result.diagnostics.push_back(meshGraphDiagnostic("editor.mesh-modifier.preview-compile",
                                                         DiagnosticSeverity::Error, populated.status().describe()));
        return result;
    }
    if (!inputNode.empty()) {
        auto bound = runtime.setNodeMesh(inputNode, input);
        if (!bound.ok()) {
            result.status = EditorStatus::Rejected;
            result.diagnostics.push_back(meshGraphDiagnostic("editor.mesh-modifier.preview-input",
                                                             DiagnosticSeverity::Error, bound.status().describe()));
            return result;
        }
    }
    auto output = runtime.executeResult(outputNode);
    if (!output.ok()) {
        result.status = EditorStatus::Failed;
        result.diagnostics.push_back(meshGraphDiagnostic("editor.mesh-modifier.preview-execution",
                                                         DiagnosticSeverity::Error, output.status().describe()));
        return result;
    }
    result.mesh                = std::move(output).takeValue();
    result.segmentCount        = runtime.compiledSegmentCount();
    result.fusedOperationCount = runtime.fusedOperationCount();
    result.status              = EditorStatus::Applied;
    return result;
}

}  // namespace eve::procgen_editing
