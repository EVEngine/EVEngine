#include "editor/EditorParticleGraph.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> particleError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

void error(ParticleGraphCompileResult& result, const char* rule, std::string message) {
    result.diagnostics.push_back({RuleId(rule), DiagnosticSeverity::Error, std::move(message)});
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

GraphNodeRecord node(const GraphNodeId& id, std::string type, EditorValue::Object properties,
                     bool input = true, bool output = true) {
    GraphNodeRecord result;
    result.id = id;
    result.type = std::move(type);
    result.properties = EditorValue(std::move(properties));
    if (input)
        result.pins.push_back({GraphPinId(id.value() + ".in"), id, "particle.flow",
                               GraphPinDirection::Input});
    if (output)
        result.pins.push_back({GraphPinId(id.value() + ".out"), id, "particle.flow",
                               GraphPinDirection::Output});
    return result;
}

bool number(const EditorValue& properties, const char* key, double minimum, double maximum) {
    const EditorValue* value = field(properties, key);
    const auto* typed = value ? value->getIf<double>() : nullptr;
    return typed && std::isfinite(*typed) && *typed >= minimum && *typed <= maximum;
}

bool integer(const EditorValue& properties, const char* key, int64_t minimum, int64_t maximum) {
    const EditorValue* value = field(properties, key);
    const auto* typed = value ? value->getIf<int64_t>() : nullptr;
    return typed && *typed >= minimum && *typed <= maximum;
}

bool boolean(const EditorValue& properties, const char* key) {
    const EditorValue* value = field(properties, key);
    return value && value->getIf<bool>();
}

bool choice(const EditorValue& properties, const char* key,
            std::initializer_list<const char*> choices) {
    const EditorValue* value = field(properties, key);
    const auto* typed = value ? value->getIf<std::string>() : nullptr;
    if (!typed) return false;
    return std::any_of(choices.begin(), choices.end(), [&](const char* item) { return *typed == item; });
}

bool tuple(const EditorValue& properties, const char* key, std::size_t size,
           bool nonNegative = false) {
    const EditorValue* value = field(properties, key);
    const auto* typed = value ? value->getIf<EditorValue::Array>() : nullptr;
    if (!typed || typed->size() != size) return false;
    return std::all_of(typed->begin(), typed->end(), [&](const EditorValue& component) {
        const auto* item = component.getIf<double>();
        return item && std::isfinite(*item) && (!nonNegative || *item >= 0.0);
    });
}

}  // namespace

GraphConnectionDecision ParticleGraphDomain::canConnect(const GraphPinRecord& from,
                                                         const GraphPinRecord& to) const {
    GraphConnectionDecision result;
    result.allowed = from.direction == GraphPinDirection::Output &&
                     to.direction == GraphPinDirection::Input && from.type == "particle.flow" &&
                     to.type == "particle.flow" && from.node != to.node;
    if (!result.allowed)
        result.diagnostics.push_back({RuleId("editor.particles.invalid-connection"),
                                      DiagnosticSeverity::Error,
                                      "Particle graph requires flow output-to-input between distinct nodes"});
    return result;
}

EditorResult<GraphNodeRecord> ParticleGraphDomain::makeNode(const GraphNodeId& id,
                                                            const std::string& type) const {
    if (id.empty())
        return particleError<GraphNodeRecord>(EditorStatus::Rejected, "editor.particles.node-id",
                                              "Particle node id is required");
    EditorValue::Object properties;
    if (type == "emission") {
        properties["rate"] = 10.0;
        properties["lifetime"] = EditorValue::Array{1.0, 1.0};
        properties["area"] = "none";
        properties["areaSize"] = EditorValue::Array{0.0, 0.0};
        properties["randomSeed"] = 0;
        properties["autoRandomSeed"] = false;
        properties["looping"] = true;
        properties["emitterLife"] = -1.0;
        return EditorResult<GraphNodeRecord>::applied(node(id, type, std::move(properties), false, true));
    }
    if (type == "motion") {
        properties["direction"] = 0.0;
        properties["spread"] = 0.0;
        properties["speed"] = EditorValue::Array{0.0, 0.0};
        properties["gravity"] = EditorValue::Array{0.0, 0.0};
        properties["damping"] = 0.0;
        properties["simulationSpace"] = "world";
    } else if (type == "collision") {
        properties["mode"] = "none";
        properties["radius"] = 0.0;
        properties["restitution"] = 0.6;
        properties["lifetimeLoss"] = 0.0;
        properties["world"] = false;
    } else if (type == "renderer") {
        properties["mode"] = "billboard";
        properties["material"] = "unlit";
        properties["texture"] = "";
        properties["size"] = EditorValue::Array{8.0, 8.0};
        properties["sort"] = "none";
        properties["gpu"] = false;
        properties["softParticles"] = false;
        properties["softDepth"] = 0.5;
        properties["softFade"] = 0.05;
    } else if (type == "output") {
        properties["bufferSize"] = 1000;
        properties["priority"] = 0;
        properties["minimumQuality"] = 0;
        properties["maxSpawnPerFrame"] = 0;
        properties["overflow"] = "drop";
        return EditorResult<GraphNodeRecord>::applied(node(id, type, std::move(properties), true, false));
    } else {
        return particleError<GraphNodeRecord>(EditorStatus::Unsupported, "editor.particles.node-type",
                                              "Unknown particle node type: " + type);
    }
    return EditorResult<GraphNodeRecord>::applied(node(id, type, std::move(properties)));
}

ParticleGraphCompileResult ParticleGraphDomain::compile(const GraphDocumentData& graph) const {
    ParticleGraphCompileResult result;
    result.documentRevision = graph.revision;
    if (graph.domain != domain()) {
        result.status = EditorStatus::Rejected;
        error(result, "editor.particles.wrong-domain", "Graph is not a particle emitter document");
        return result;
    }
    if (graph.schemaVersion != 1) {
        result.status = EditorStatus::Unsupported;
        error(result, "editor.particles.unsupported-schema", "Particle graph schema version is unsupported");
        return result;
    }
    std::map<GraphNodeId, const GraphNodeRecord*> nodes;
    std::map<GraphPinId, GraphNodeId> pins;
    std::map<GraphPinId, const GraphPinRecord*> pinRecords;
    int emissionCount = 0;
    int rendererCount = 0;
    int outputCount = 0;
    for (const GraphNodeRecord& item : graph.nodes) {
        if (!nodes.emplace(item.id, &item).second)
            error(result, "editor.particles.duplicate-node", "Duplicate particle node id: " + item.id.value());
        for (const GraphPinRecord& pin : item.pins) {
            pins.emplace(pin.id, item.id);
            pinRecords.emplace(pin.id, &pin);
        }
        emissionCount += item.type == "emission";
        rendererCount += item.type == "renderer";
        outputCount += item.type == "output";
    }
    if (emissionCount != 1 || rendererCount != 1 || outputCount != 1)
        error(result, "editor.particles.required-modules",
              "Particle graph requires exactly one emission, renderer and output node");
    std::map<GraphNodeId, GraphNodeId> next;
    std::map<GraphNodeId, int> incoming;
    for (const GraphEdgeRecord& edge : graph.edges) {
        if (!pins.contains(edge.from) || !pins.contains(edge.to)) {
            error(result, "editor.particles.dangling-edge", "Particle edge references a missing pin");
            continue;
        }
        if (!canConnect(*pinRecords.at(edge.from), *pinRecords.at(edge.to)).allowed) {
            error(result, "editor.particles.edge-direction", "Particle edge has an invalid direction or type");
            continue;
        }
        const GraphNodeId from = pins[edge.from];
        const GraphNodeId to = pins[edge.to];
        if (!next.emplace(from, to).second || ++incoming[to] > 1)
            error(result, "editor.particles.branching", "Particle module chain cannot branch or merge");
    }
    const auto emission = std::find_if(nodes.begin(), nodes.end(), [](const auto& entry) {
        return entry.second->type == "emission";
    });
    std::set<GraphNodeId> visited;
    if (emission != nodes.end()) {
        GraphNodeId current = emission->first;
        while (visited.insert(current).second && next.contains(current)) current = next[current];
        if (visited.size() != nodes.size() || (next.contains(current) && visited.contains(next[current])))
            error(result, "editor.particles.chain", "Particle modules must form one acyclic emission-to-output chain");
        else if (nodes.at(current)->type != "output")
            error(result, "editor.particles.output-terminal", "Particle module chain must terminate at output");
    }

    EditorValue::Object config;
    for (const auto& [id, item] : nodes) {
        (void)id;
        const EditorValue& p = item->properties;
        if (item->type == "emission") {
            if (!number(p, "rate", 0.0, 10000000.0) || !tuple(p, "lifetime", 2, true) ||
                !choice(p, "area", {"none", "ellipse", "rect"}) || !tuple(p, "areaSize", 2, true) ||
                !integer(p, "randomSeed", -2147483648LL, 2147483647LL) ||
                !boolean(p, "autoRandomSeed") || !boolean(p, "looping") ||
                !number(p, "emitterLife", -1.0, 86400.0))
                error(result, "editor.particles.emission-properties", "Emission properties are invalid");
            config["emission"] = p;
        } else if (item->type == "motion") {
            if (!number(p, "direction", -360000.0, 360000.0) || !number(p, "spread", 0.0, 360.0) ||
                !tuple(p, "speed", 2, true) || !tuple(p, "gravity", 2) ||
                !number(p, "damping", 0.0, 1.0) ||
                !choice(p, "simulationSpace", {"world", "local"}))
                error(result, "editor.particles.motion-properties", "Motion properties are invalid");
            config["motion"] = p;
        } else if (item->type == "collision") {
            if (!choice(p, "mode", {"none", "kill", "bounce", "stop"}) ||
                !number(p, "radius", 0.0, 1000000.0) ||
                !number(p, "restitution", 0.0, 1.0) ||
                !number(p, "lifetimeLoss", 0.0, 1.0) || !boolean(p, "world"))
                error(result, "editor.particles.collision-properties", "Collision properties are invalid");
            config["collision"] = p;
        } else if (item->type == "renderer") {
            if (!choice(p, "mode", {"billboard", "axis", "stretched", "ribbon"}) ||
                !choice(p, "material", {"unlit", "lit", "distortion"}) ||
                !field(p, "texture") || !field(p, "texture")->getIf<std::string>() ||
                !tuple(p, "size", 2, true) || !choice(p, "sort", {"none", "oldest", "youngest", "distance"}) ||
                !boolean(p, "gpu") || !boolean(p, "softParticles") ||
                !number(p, "softDepth", 0.0, 1.0) || !number(p, "softFade", 0.0, 1.0))
                error(result, "editor.particles.renderer-properties", "Renderer properties are invalid");
            config["renderer"] = p;
        } else if (item->type == "output") {
            if (!integer(p, "bufferSize", 1, 10000000) || !integer(p, "priority", -100000, 100000) ||
                !integer(p, "minimumQuality", 0, 3) || !integer(p, "maxSpawnPerFrame", 0, 10000000) ||
                !choice(p, "overflow", {"drop", "pause", "warn"}))
                error(result, "editor.particles.output-properties", "Output/budget properties are invalid");
            config["output"] = p;
        } else {
            error(result, "editor.particles.unknown-node", "Unknown particle module: " + item->type);
        }
    }
    if (!result.diagnostics.empty()) {
        result.status = EditorStatus::Failed;
        return result;
    }
    result.status = EditorStatus::Applied;
    result.configuration = EditorValue(std::move(config));
    return result;
}

ParticleGraphPreviewResult ParticleGraphDomain::preview(const GraphDocumentData& graph, double seconds,
                                                        double fixedStep, int particleBudget,
                                                        int spawnBudgetPerFrame) const {
    ParticleGraphPreviewResult result;
    result.documentRevision = graph.revision;
    result.simulatedSeconds = seconds;
    const auto compiled = compile(graph);
    result.diagnostics = compiled.diagnostics;
    if (compiled.status != EditorStatus::Applied || !std::isfinite(seconds) ||
        !std::isfinite(fixedStep) || seconds < 0.0 || seconds > 600.0 || fixedStep <= 0.0 ||
        seconds / fixedStep > 1000000.0 ||
        particleBudget < 1 || spawnBudgetPerFrame < 0) {
        result.status = EditorStatus::Rejected;
        if (compiled.status == EditorStatus::Applied)
            result.diagnostics.push_back({RuleId("editor.particles.preview-input"), DiagnosticSeverity::Error,
                                          "Preview time, step, and budgets are invalid"});
        return result;
    }
    const EditorValue* emission = field(compiled.configuration, "emission");
    const EditorValue* output = field(compiled.configuration, "output");
    const double rate = *field(*emission, "rate")->getIf<double>();
    const auto& lifetime = *field(*emission, "lifetime")->getIf<EditorValue::Array>();
    const double maxLifetime = *lifetime[1].getIf<double>();
    const int bufferSize = static_cast<int>(*field(*output, "bufferSize")->getIf<int64_t>());
    const int configuredFrameCap = static_cast<int>(*field(*output, "maxSpawnPerFrame")->getIf<int64_t>());
    const int frames = static_cast<int>(std::ceil(seconds / fixedStep));
    const int requested = static_cast<int>(std::floor(rate * seconds));
    int frameCap = configuredFrameCap;
    if (spawnBudgetPerFrame > 0 && (frameCap == 0 || spawnBudgetPerFrame < frameCap))
        frameCap = spawnBudgetPerFrame;
    const int admittedByFrames = frameCap > 0 ? std::min(requested, frames * frameCap) : requested;
    result.spawnedParticles = std::min(admittedByFrames, particleBudget);
    result.droppedParticles = requested - result.spawnedParticles;
    result.peakLiveParticles = std::min({result.spawnedParticles, bufferSize, particleBudget,
                                         static_cast<int>(std::ceil(rate * maxLifetime))});
    if (result.droppedParticles > 0)
        result.diagnostics.push_back({RuleId("editor.particles.preview-budget"), DiagnosticSeverity::Warning,
                                      "Preview dropped spawns because a particle or per-frame budget was exceeded"});
    if (result.peakLiveParticles >= bufferSize && requested > bufferSize)
        result.diagnostics.push_back({RuleId("editor.particles.preview-buffer"), DiagnosticSeverity::Warning,
                                      "Preview reaches the emitter buffer capacity"});
    result.status = EditorStatus::Applied;
    return result;
}

}  // namespace eve::editor
