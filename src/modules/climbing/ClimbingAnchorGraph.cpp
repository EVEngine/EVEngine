#include "climbing/ClimbingAnchorGraph.h"

#include "physics/Body3D.h"
#include "physics/World3D.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <unordered_set>
#include <utility>

namespace eve::climbing {
namespace {

constexpr std::size_t maxGraphNodes = 4096;
constexpr std::size_t maxGraphEdges = 16384;
constexpr std::uint32_t maxOccupancySlots = 8;
constexpr float frameTolerance = 0.02f;

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "climbing.anchor_graph"));
}

bool finite(float value) { return std::isfinite(value); }
bool finite(Vec3 value) { return finite(value.x) && finite(value.y) && finite(value.z); }
float lengthSquared(Vec3 value) { return value.x * value.x + value.y * value.y + value.z * value.z; }
float dot(Vec3 lhs, Vec3 rhs) { return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z; }

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool readString(const eve::Value::Object& object, std::string_view name, std::string& output) {
    const eve::Value* value = field(object, name);
    const auto* text = value ? value->getIf<std::string>() : nullptr;
    if (!text) return false;
    output = *text;
    return true;
}

bool readInt64(const eve::Value::Object& object, std::string_view name, std::int64_t& output) {
    const eve::Value* value = field(object, name);
    const auto* number = value ? value->getIf<std::int64_t>() : nullptr;
    if (!number) return false;
    output = *number;
    return true;
}

bool readBool(const eve::Value::Object& object, std::string_view name, bool& output) {
    const eve::Value* value = field(object, name);
    const auto* boolean = value ? value->getIf<bool>() : nullptr;
    if (!boolean) return false;
    output = *boolean;
    return true;
}

bool numericFloat(const eve::Value& value, float& output) {
    double number = 0.0;
    if (const auto* integer = value.getIf<std::int64_t>())
        number = static_cast<double>(*integer);
    else if (const auto* real = value.getIf<double>())
        number = *real;
    else
        return false;
    if (!std::isfinite(number) || number < -static_cast<double>(std::numeric_limits<float>::max()) ||
        number > static_cast<double>(std::numeric_limits<float>::max()))
        return false;
    output = static_cast<float>(number);
    return true;
}

bool readVec(const eve::Value::Object& object, std::string_view name, Vec3& output) {
    const eve::Value* value = field(object, name);
    const auto* array = value ? value->getIf<eve::Value::Array>() : nullptr;
    return array && array->size() == 3 && numericFloat((*array)[0], output.x) &&
           numericFloat((*array)[1], output.y) && numericFloat((*array)[2], output.z);
}

eve::Value vecValue(Vec3 value) {
    return eve::Value::array({eve::Value(value.x), eve::Value(value.y), eve::Value(value.z)});
}

std::string_view nodeKindName(ClimbingAnchorKind kind) {
    switch (kind) {
        case ClimbingAnchorKind::Ledge: return "ledge";
        case ClimbingAnchorKind::CornerInner: return "corner_inner";
        case ClimbingAnchorKind::CornerOuter: return "corner_outer";
        case ClimbingAnchorKind::LadderRung: return "ladder_rung";
        case ClimbingAnchorKind::Pole: return "pole";
        case ClimbingAnchorKind::Beam: return "beam";
        case ClimbingAnchorKind::Bar: return "bar";
    }
    return "unknown";
}

bool readNodeKind(std::string_view value, ClimbingAnchorKind& output) {
    if (value == "ledge")
        output = ClimbingAnchorKind::Ledge;
    else if (value == "corner_inner")
        output = ClimbingAnchorKind::CornerInner;
    else if (value == "corner_outer")
        output = ClimbingAnchorKind::CornerOuter;
    else if (value == "ladder_rung")
        output = ClimbingAnchorKind::LadderRung;
    else if (value == "pole")
        output = ClimbingAnchorKind::Pole;
    else if (value == "beam")
        output = ClimbingAnchorKind::Beam;
    else if (value == "bar")
        output = ClimbingAnchorKind::Bar;
    else
        return false;
    return true;
}

std::string_view edgeKindName(ClimbingAnchorEdgeKind kind) {
    switch (kind) {
        case ClimbingAnchorEdgeKind::Shimmy: return "shimmy";
        case ClimbingAnchorEdgeKind::Corner: return "corner";
        case ClimbingAnchorEdgeKind::Jump: return "jump";
        case ClimbingAnchorEdgeKind::Drop: return "drop";
        case ClimbingAnchorEdgeKind::Mount: return "mount";
        case ClimbingAnchorEdgeKind::Dismount: return "dismount";
        case ClimbingAnchorEdgeKind::Climb: return "climb";
        case ClimbingAnchorEdgeKind::Balance: return "balance";
        case ClimbingAnchorEdgeKind::Swing: return "swing";
    }
    return "unknown";
}

bool readEdgeKind(std::string_view value, ClimbingAnchorEdgeKind& output) {
    if (value == "shimmy")
        output = ClimbingAnchorEdgeKind::Shimmy;
    else if (value == "corner")
        output = ClimbingAnchorEdgeKind::Corner;
    else if (value == "jump")
        output = ClimbingAnchorEdgeKind::Jump;
    else if (value == "drop")
        output = ClimbingAnchorEdgeKind::Drop;
    else if (value == "mount")
        output = ClimbingAnchorEdgeKind::Mount;
    else if (value == "dismount")
        output = ClimbingAnchorEdgeKind::Dismount;
    else if (value == "climb")
        output = ClimbingAnchorEdgeKind::Climb;
    else if (value == "balance")
        output = ClimbingAnchorEdgeKind::Balance;
    else if (value == "swing")
        output = ClimbingAnchorEdgeKind::Swing;
    else
        return false;
    return true;
}

eve::Value::Array stringArray(const std::vector<std::string>& strings) {
    eve::Value::Array values;
    values.reserve(strings.size());
    for (const auto& value : strings) values.emplace_back(value);
    return values;
}

bool readStringArray(const eve::Value::Object& object, std::string_view name, std::vector<std::string>& output) {
    const eve::Value* value = field(object, name);
    const auto* array = value ? value->getIf<eve::Value::Array>() : nullptr;
    if (!array) return false;
    output.reserve(array->size());
    for (const auto& item : *array) {
        const auto* text = item.getIf<std::string>();
        if (!text) return false;
        output.push_back(*text);
    }
    return true;
}

eve::Value::Object unknownFields(const eve::Value::Object& object,
                                 std::initializer_list<std::string_view> knownNames) {
    std::unordered_set<std::string_view> known(knownNames.begin(), knownNames.end());
    eve::Value::Object result;
    for (const auto& [name, value] : object)
        if (!known.contains(name)) result.emplace(name, value);
    return result;
}

bool validUniqueStrings(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    return !std::any_of(values.begin(), values.end(), [](const auto& value) { return value.empty(); }) &&
           std::adjacent_find(values.begin(), values.end()) == values.end();
}

void canonicalize(ClimbingAnchorGraphDefinition& graph) {
    for (auto& node : graph.nodes) std::sort(node.tags.begin(), node.tags.end());
    for (auto& edge : graph.edges) std::sort(edge.requiredTags.begin(), edge.requiredTags.end());
    std::sort(graph.nodes.begin(), graph.nodes.end(), [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    std::sort(graph.edges.begin(), graph.edges.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.from != rhs.from) return lhs.from < rhs.from;
        if (lhs.to != rhs.to) return lhs.to < rhs.to;
        if (lhs.kind != rhs.kind) return lhs.kind < rhs.kind;
        return lhs.bidirectional < rhs.bidirectional;
    });
}

bool sameEdgeIdentity(const ClimbingAnchorEdgeDefinition& lhs, const ClimbingAnchorEdgeDefinition& rhs) {
    return lhs.from == rhs.from && lhs.to == rhs.to && lhs.kind == rhs.kind;
}

Vec3 vectorFrom(physics::PhysicsVector3D value) { return {value.x, value.y, value.z}; }

}  // namespace

eve::Result<void> validateClimbingAnchorGraphDefinition(const ClimbingAnchorGraphDefinition& graph) {
    if (graph.id.empty() || graph.sourceGeometryContentId.empty() || graph.buildSettingsHash.empty())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "graph id, source geometry content id, and build settings hash are required",
                             "graph.identity");
    if (graph.nodes.empty() || graph.nodes.size() > maxGraphNodes || graph.edges.size() > maxGraphEdges)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "anchor graph node and edge counts exceed supported bounds", "graph.topology");
    std::unordered_set<std::string> nodeIds;
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        const auto& node = graph.nodes[index];
        const std::string path = "nodes." + std::to_string(index);
        if (node.id.empty() || !nodeIds.emplace(node.id).second)
            return failure<void>(eve::DiagnosticCode::AlreadyExists, "anchor node ids must be non-empty and unique",
                                 path + ".id");
        if (!finite(node.localPosition) || !finite(node.localNormal) || !finite(node.localTangent) ||
            !finite(node.leftHandSocket) || !finite(node.rightHandSocket) || !finite(node.feetSocket))
            return failure<void>(eve::DiagnosticCode::InvalidArgument, "anchor frame and sockets must be finite", path);
        const float normalLength = lengthSquared(node.localNormal);
        const float tangentLength = lengthSquared(node.localTangent);
        if (std::fabs(normalLength - 1.f) > frameTolerance || std::fabs(tangentLength - 1.f) > frameTolerance ||
            std::fabs(dot(node.localNormal, node.localTangent)) > frameTolerance)
            return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "anchor normal and tangent must form an orthonormal local frame", path + ".frame");
        if (node.occupancySlots == 0 || node.occupancySlots > maxOccupancySlots)
            return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "anchor occupancy slots must be between one and eight", path + ".occupancySlots");
        if (!validUniqueStrings(node.tags))
            return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "anchor tags must be non-empty and unique", path + ".tags");
    }
    std::vector<ClimbingAnchorEdgeDefinition> edges = graph.edges;
    std::sort(edges.begin(), edges.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.from != rhs.from) return lhs.from < rhs.from;
        if (lhs.to != rhs.to) return lhs.to < rhs.to;
        return lhs.kind < rhs.kind;
    });
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const auto& edge = edges[index];
        const std::string path = "edges." + std::to_string(index);
        if (edge.from.empty() || edge.to.empty() || edge.from == edge.to || !nodeIds.contains(edge.from) ||
            !nodeIds.contains(edge.to))
            return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "anchor edge endpoints must name two distinct existing nodes", path);
        if (index != 0 && sameEdgeIdentity(edges[index - 1], edge))
            return failure<void>(eve::DiagnosticCode::AlreadyExists,
                                 "anchor edge identity must be unique", path);
        if (!validUniqueStrings(edge.requiredTags))
            return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "edge required tags must be non-empty and unique", path + ".requiredTags");
    }
    return eve::Result<void>::success();
}

eve::Result<eve::Value> encodeClimbingAnchorGraphDefinition(const ClimbingAnchorGraphDefinition& graph) {
    auto valid = validateClimbingAnchorGraphDefinition(graph);
    if (!valid) return eve::Result<eve::Value>::failure(valid.status());
    ClimbingAnchorGraphDefinition canonical = graph;
    canonicalize(canonical);
    eve::Value::Array nodes;
    nodes.reserve(canonical.nodes.size());
    for (const auto& node : canonical.nodes) {
        eve::Value::Object object = node.extensionMetadata;
        object["id"] = eve::Value(node.id);
        object["kind"] = eve::Value(std::string(nodeKindName(node.kind)));
        object["localPosition"] = vecValue(node.localPosition);
        object["localNormal"] = vecValue(node.localNormal);
        object["localTangent"] = vecValue(node.localTangent);
        object["leftHandSocket"] = vecValue(node.leftHandSocket);
        object["rightHandSocket"] = vecValue(node.rightHandSocket);
        object["feetSocket"] = vecValue(node.feetSocket);
        object["occupancySlots"] = eve::Value(static_cast<std::int64_t>(node.occupancySlots));
        object["tags"] = eve::Value(stringArray(node.tags));
        nodes.emplace_back(std::move(object));
    }
    eve::Value::Array edges;
    edges.reserve(canonical.edges.size());
    for (const auto& edge : canonical.edges) {
        eve::Value::Object object = edge.extensionMetadata;
        object["from"] = eve::Value(edge.from);
        object["to"] = eve::Value(edge.to);
        object["kind"] = eve::Value(std::string(edgeKindName(edge.kind)));
        object["bidirectional"] = eve::Value(edge.bidirectional);
        object["requiredTags"] = eve::Value(stringArray(edge.requiredTags));
        edges.emplace_back(std::move(object));
    }
    eve::Value::Object root = canonical.extensionMetadata;
    root["schemaId"] = eve::Value(std::string(ClimbingAnchorGraphDefinition::SchemaId));
    root["schemaVersion"] = eve::Value(ClimbingAnchorGraphDefinition::SchemaVersion);
    root["id"] = eve::Value(canonical.id);
    root["sourceGeometryContentId"] = eve::Value(canonical.sourceGeometryContentId);
    root["buildSettingsHash"] = eve::Value(canonical.buildSettingsHash);
    root["nodes"] = eve::Value(std::move(nodes));
    root["edges"] = eve::Value(std::move(edges));
    return eve::Result<eve::Value>::success(eve::Value(std::move(root)));
}

eve::Result<ClimbingAnchorGraphDefinition> decodeClimbingAnchorGraphDefinition(const eve::Value& value) {
    const auto* root = value.getIf<eve::Value::Object>();
    if (!root)
        return failure<ClimbingAnchorGraphDefinition>(eve::DiagnosticCode::ParseError,
                                                      "anchor graph definition must be an object");
    ClimbingAnchorGraphDefinition graph;
    std::string schemaId;
    std::int64_t version = -1;
    const eve::Value* nodesValue = field(*root, "nodes");
    const auto* nodes = nodesValue ? nodesValue->getIf<eve::Value::Array>() : nullptr;
    const eve::Value* edgesValue = field(*root, "edges");
    const auto* edges = edgesValue ? edgesValue->getIf<eve::Value::Array>() : nullptr;
    if (!readString(*root, "schemaId", schemaId) || schemaId != ClimbingAnchorGraphDefinition::SchemaId ||
        !readInt64(*root, "schemaVersion", version) ||
        (version != ClimbingAnchorGraphDefinition::SchemaVersion &&
         version != ClimbingAnchorGraphDefinition::SchemaVersion - 1) || !readString(*root, "id", graph.id) ||
        !readString(*root, "sourceGeometryContentId", graph.sourceGeometryContentId) ||
        !readString(*root, "buildSettingsHash", graph.buildSettingsHash) || !nodes || !edges) {
        if (version > ClimbingAnchorGraphDefinition::SchemaVersion)
            return failure<ClimbingAnchorGraphDefinition>(eve::DiagnosticCode::UnknownVersion,
                                                          "anchor graph schema version is unsupported",
                                                          "schemaVersion");
        return failure<ClimbingAnchorGraphDefinition>(eve::DiagnosticCode::ParseError,
                                                      "anchor graph envelope has missing or invalid known fields");
    }
    graph.nodes.reserve(nodes->size());
    for (std::size_t index = 0; index < nodes->size(); ++index) {
        const auto* object = (*nodes)[index].getIf<eve::Value::Object>();
        ClimbingAnchorNodeDefinition node;
        std::string kind;
        std::int64_t occupancySlots = 0;
        if (!object || !readString(*object, "id", node.id) || !readString(*object, "kind", kind) ||
            !readNodeKind(kind, node.kind) || !readVec(*object, "localPosition", node.localPosition) ||
            !readVec(*object, "localNormal", node.localNormal) ||
            !readVec(*object, "localTangent", node.localTangent) ||
            !readVec(*object, "leftHandSocket", node.leftHandSocket) ||
            !readVec(*object, "rightHandSocket", node.rightHandSocket) ||
            !readVec(*object, "feetSocket", node.feetSocket) ||
            !readInt64(*object, "occupancySlots", occupancySlots) || occupancySlots < 0 ||
            occupancySlots > std::numeric_limits<std::uint32_t>::max() ||
            !readStringArray(*object, "tags", node.tags))
            return failure<ClimbingAnchorGraphDefinition>(eve::DiagnosticCode::ParseError,
                                                          "anchor node has missing or invalid known fields",
                                                          "nodes." + std::to_string(index));
        node.occupancySlots = static_cast<std::uint32_t>(occupancySlots);
        node.extensionMetadata = unknownFields(*object, {"id", "kind", "localPosition", "localNormal",
                                                         "localTangent", "leftHandSocket", "rightHandSocket",
                                                         "feetSocket", "occupancySlots", "tags"});
        graph.nodes.push_back(std::move(node));
    }
    graph.edges.reserve(edges->size());
    for (std::size_t index = 0; index < edges->size(); ++index) {
        const auto* object = (*edges)[index].getIf<eve::Value::Object>();
        ClimbingAnchorEdgeDefinition edge;
        std::string kind;
        if (!object || !readString(*object, "from", edge.from) || !readString(*object, "to", edge.to) ||
            !readString(*object, "kind", kind) || !readEdgeKind(kind, edge.kind) ||
            !readBool(*object, "bidirectional", edge.bidirectional) ||
            !readStringArray(*object, "requiredTags", edge.requiredTags))
            return failure<ClimbingAnchorGraphDefinition>(eve::DiagnosticCode::ParseError,
                                                          "anchor edge has missing or invalid known fields",
                                                          "edges." + std::to_string(index));
        edge.extensionMetadata = unknownFields(*object, {"from", "to", "kind", "bidirectional", "requiredTags"});
        graph.edges.push_back(std::move(edge));
    }
    graph.extensionMetadata = unknownFields(*root, {"schemaId", "schemaVersion", "id",
                                                    "sourceGeometryContentId", "buildSettingsHash", "nodes", "edges"});
    auto valid = validateClimbingAnchorGraphDefinition(graph);
    if (!valid) return eve::Result<ClimbingAnchorGraphDefinition>::failure(valid.status());
    canonicalize(graph);
    return eve::Result<ClimbingAnchorGraphDefinition>::success(std::move(graph));
}

eve::Result<ClimbingAnchorGraphInstance> ClimbingAnchorGraphInstance::bind(ClimbingAnchorGraphDefinition graph,
                                                                           physics::World3D& world,
                                                                           physics::PhysicsBodyHandle body) {
    auto valid = validateClimbingAnchorGraphDefinition(graph);
    if (!valid) return eve::Result<ClimbingAnchorGraphInstance>::failure(valid.status());
    if (!body.isValid() || !world.findBody(body))
        return failure<ClimbingAnchorGraphInstance>(eve::DiagnosticCode::StaleHandle,
                                                    "anchor graph target body handle is stale", "body");
    canonicalize(graph);
    ClimbingAnchorGraphInstance instance;
    instance.graph_ = std::move(graph);
    instance.world_ = world.runtimeHandle();
    instance.body_ = body;
    return eve::Result<ClimbingAnchorGraphInstance>::success(std::move(instance));
}

const ClimbingAnchorNodeDefinition* ClimbingAnchorGraphInstance::findNode(std::string_view nodeId) const noexcept {
    const auto found = std::lower_bound(graph_.nodes.begin(), graph_.nodes.end(), nodeId,
                                        [](const auto& node, std::string_view id) { return node.id < id; });
    return found != graph_.nodes.end() && found->id == nodeId ? &*found : nullptr;
}

eve::Result<ClimbingAnchorNodeRef> ClimbingAnchorGraphInstance::nodeRef(std::string_view nodeId) const {
    if (!findNode(nodeId))
        return failure<ClimbingAnchorNodeRef>(eve::DiagnosticCode::NotFound, "anchor node was not found", "nodeId");
    return eve::Result<ClimbingAnchorNodeRef>::success({graph_.id, std::string(nodeId), generation_});
}

eve::Result<ResolvedClimbingAnchorNode> ClimbingAnchorGraphInstance::resolveNode(
    physics::World3D& world, const ClimbingAnchorNodeRef& reference) const {
    auto resolved = resolveNodeKinematics(world, reference);
    if (!resolved) return resolved;
    const auto* node = findNode(reference.nodeId);
    EV_ASSERT(node != nullptr, "kinematics resolution validated the anchor node");
    resolved.value().tags = node->tags;
    return resolved;
}

eve::Result<ResolvedClimbingAnchorNode> ClimbingAnchorGraphInstance::resolveNodeKinematics(
    physics::World3D& world, const ClimbingAnchorNodeRef& reference) const {
    if (reference.graphId != graph_.id)
        return failure<ResolvedClimbingAnchorNode>(eve::DiagnosticCode::Conflict,
                                                   "anchor node reference belongs to another graph",
                                                   "reference.graphId");
    if (reference.graphGeneration != generation_)
        return failure<ResolvedClimbingAnchorNode>(eve::DiagnosticCode::StaleHandle,
                                                   "anchor node reference belongs to a stale graph generation",
                                                   "reference.graphGeneration");
    const auto* node = findNode(reference.nodeId);
    if (!node)
        return failure<ResolvedClimbingAnchorNode>(eve::DiagnosticCode::NotFound,
                                                   "anchor node was not found", "reference.nodeId");
    if (world.runtimeHandle() != world_)
        return failure<ResolvedClimbingAnchorNode>(eve::DiagnosticCode::StaleHandle,
                                                   "anchor graph belongs to another or stale Physics world", "world");
    physics::Body3D* body = world.findBody(body_);
    if (!body)
        return failure<ResolvedClimbingAnchorNode>(eve::DiagnosticCode::StaleHandle,
                                                   "anchor graph target body handle is stale", "body");
    ResolvedClimbingAnchorNode result;
    result.reference = reference;
    result.kind = node->kind;
    result.body = body_;
    auto position = body->localToWorldPointOwned(node->localPosition.x, node->localPosition.y,
                                                 node->localPosition.z);
    if (!position) return eve::Result<ResolvedClimbingAnchorNode>::failure(position.status());
    auto normal = body->localToWorldVectorOwned(node->localNormal.x, node->localNormal.y,
                                                node->localNormal.z);
    if (!normal) return eve::Result<ResolvedClimbingAnchorNode>::failure(normal.status());
    auto tangent = body->localToWorldVectorOwned(node->localTangent.x, node->localTangent.y,
                                                 node->localTangent.z);
    if (!tangent) return eve::Result<ResolvedClimbingAnchorNode>::failure(tangent.status());
    auto leftHand = body->localToWorldPointOwned(node->leftHandSocket.x, node->leftHandSocket.y,
                                                 node->leftHandSocket.z);
    if (!leftHand) return eve::Result<ResolvedClimbingAnchorNode>::failure(leftHand.status());
    auto rightHand = body->localToWorldPointOwned(node->rightHandSocket.x, node->rightHandSocket.y,
                                                  node->rightHandSocket.z);
    if (!rightHand) return eve::Result<ResolvedClimbingAnchorNode>::failure(rightHand.status());
    auto feet = body->localToWorldPointOwned(node->feetSocket.x, node->feetSocket.y, node->feetSocket.z);
    if (!feet) return eve::Result<ResolvedClimbingAnchorNode>::failure(feet.status());
    auto velocity = body->getLocalPointVelocityOwned(node->localPosition.x, node->localPosition.y,
                                                     node->localPosition.z);
    if (!velocity) return eve::Result<ResolvedClimbingAnchorNode>::failure(velocity.status());
    result.position = vectorFrom(position.value());
    result.normal = vectorFrom(normal.value());
    result.tangent = vectorFrom(tangent.value());
    result.leftHandSocket = vectorFrom(leftHand.value());
    result.rightHandSocket = vectorFrom(rightHand.value());
    result.feetSocket = vectorFrom(feet.value());
    result.pointVelocity = vectorFrom(velocity.value());
    return eve::Result<ResolvedClimbingAnchorNode>::success(std::move(result));
}

eve::Result<std::vector<ClimbingAnchorEdgeDefinition>> ClimbingAnchorGraphInstance::edgesFrom(
    const ClimbingAnchorNodeRef& reference) const {
    if (reference.graphId != graph_.id)
        return failure<std::vector<ClimbingAnchorEdgeDefinition>>(
            eve::DiagnosticCode::Conflict, "anchor node reference belongs to another graph", "reference.graphId");
    if (reference.graphGeneration != generation_)
        return failure<std::vector<ClimbingAnchorEdgeDefinition>>(
            eve::DiagnosticCode::StaleHandle, "anchor node reference belongs to a stale graph generation",
            "reference.graphGeneration");
    if (!findNode(reference.nodeId))
        return failure<std::vector<ClimbingAnchorEdgeDefinition>>(eve::DiagnosticCode::NotFound,
                                                                  "anchor node was not found", "reference.nodeId");
    std::vector<ClimbingAnchorEdgeDefinition> result;
    for (const auto& edge : graph_.edges) {
        if (edge.from == reference.nodeId)
            result.push_back(edge);
        else if (edge.bidirectional && edge.to == reference.nodeId) {
            ClimbingAnchorEdgeDefinition reverse = edge;
            std::swap(reverse.from, reverse.to);
            result.push_back(std::move(reverse));
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.kind != rhs.kind) return lhs.kind < rhs.kind;
        return lhs.to < rhs.to;
    });
    return eve::Result<std::vector<ClimbingAnchorEdgeDefinition>>::success(std::move(result));
}

eve::Result<ClimbingAnchorRoute> ClimbingAnchorGraphInstance::planRoute(
    const ClimbingAnchorRouteRequest& request) const {
    if (request.start.graphId != graph_.id || request.goal.graphId != graph_.id)
        return failure<ClimbingAnchorRoute>(eve::DiagnosticCode::Conflict,
                                            "route endpoints belong to another anchor graph", "request");
    if (request.start.graphGeneration != generation_ || request.goal.graphGeneration != generation_)
        return failure<ClimbingAnchorRoute>(eve::DiagnosticCode::StaleHandle,
                                            "route endpoints belong to a stale graph generation", "request");
    if (!findNode(request.start.nodeId) || !findNode(request.goal.nodeId))
        return failure<ClimbingAnchorRoute>(eve::DiagnosticCode::NotFound,
                                            "route endpoint was not found", "request");
    if (request.maxVisitedNodes == 0 || request.maxVisitedNodes > 65536)
        return failure<ClimbingAnchorRoute>(eve::DiagnosticCode::InvalidArgument,
                                            "route maxVisitedNodes must be between one and 65536",
                                            "request.maxVisitedNodes");
    const bool requesterHasAgent = !request.requester.agentId.isZero();
    const bool requesterHasExecution = !request.requester.executionId.isZero();
    if (requesterHasAgent != requesterHasExecution)
        return failure<ClimbingAnchorRoute>(eve::DiagnosticCode::InvalidArgument,
                                            "route requester must provide both agent and execution ids",
                                            "request.requester");

    std::vector<ClimbingAnchorEdgeKind> allowed = request.allowedEdgeKinds;
    std::sort(allowed.begin(), allowed.end());
    allowed.erase(std::unique(allowed.begin(), allowed.end()), allowed.end());
    if (!validUniqueStrings(request.availableTags))
        return failure<ClimbingAnchorRoute>(eve::DiagnosticCode::InvalidArgument,
                                            "route capability tags must be non-empty and unique",
                                            "request.availableTags");
    std::vector<std::string> availableTags = request.availableTags;
    std::sort(availableTags.begin(), availableTags.end());
    const auto permits = [&allowed](ClimbingAnchorEdgeKind kind) {
        return allowed.empty() || std::binary_search(allowed.begin(), allowed.end(), kind);
    };
    const auto isFull = [this, &request](std::string_view nodeId) {
        if (request.occupancyPolicy == ClimbingRouteOccupancyPolicy::Ignore) return false;
        const auto* node = findNode(nodeId);
        if (!node) return true;
        std::uint32_t occupied = 0;
        for (const auto& [id, record] : reservations_) {
            (void)id;
            if (record.nodeId != nodeId) continue;
            if (record.occupant == request.requester) return false;
            ++occupied;
        }
        return occupied >= node->occupancySlots;
    };

    struct Previous {
        std::string            from;
        ClimbingAnchorEdgeKind kind = ClimbingAnchorEdgeKind::Shimmy;
    };
    std::deque<std::string> queue;
    std::unordered_map<std::string, Previous> previous;
    previous.emplace(request.start.nodeId, Previous{});
    queue.push_back(request.start.nodeId);
    bool found = request.start.nodeId == request.goal.nodeId;
    while (!queue.empty() && !found) {
        if (previous.size() > request.maxVisitedNodes)
            return failure<ClimbingAnchorRoute>(eve::DiagnosticCode::PreconditionViolation,
                                                "route planning exceeded maxVisitedNodes",
                                                "request.maxVisitedNodes");
        const std::string current = std::move(queue.front());
        queue.pop_front();
        auto reference = nodeRef(current);
        if (!reference) return eve::Result<ClimbingAnchorRoute>::failure(reference.status());
        auto outgoing = edgesFrom(reference.value());
        if (!outgoing) return eve::Result<ClimbingAnchorRoute>::failure(outgoing.status());
        for (const auto& edge : outgoing.value()) {
            const bool hasRequiredTags = std::all_of(
                edge.requiredTags.begin(), edge.requiredTags.end(), [&availableTags](const std::string& tag) {
                    return std::binary_search(availableTags.begin(), availableTags.end(), tag);
                });
            if (!permits(edge.kind) || !hasRequiredTags || previous.contains(edge.to) || isFull(edge.to))
                continue;
            if (previous.size() >= request.maxVisitedNodes)
                return failure<ClimbingAnchorRoute>(eve::DiagnosticCode::PreconditionViolation,
                                                    "route planning exceeded maxVisitedNodes",
                                                    "request.maxVisitedNodes");
            previous.emplace(edge.to, Previous{current, edge.kind});
            if (edge.to == request.goal.nodeId) {
                found = true;
                break;
            }
            queue.push_back(edge.to);
        }
    }
    if (!found)
        return failure<ClimbingAnchorRoute>(eve::DiagnosticCode::NotFound,
                                            "no permitted anchor route reaches the goal", "request.goal");

    std::vector<std::string> reversedNodes;
    std::vector<ClimbingAnchorEdgeKind> reversedKinds;
    for (std::string cursor = request.goal.nodeId;;) {
        reversedNodes.push_back(cursor);
        if (cursor == request.start.nodeId) break;
        const auto foundPrevious = previous.find(cursor);
        if (foundPrevious == previous.end())
            return failure<ClimbingAnchorRoute>(eve::DiagnosticCode::InvariantViolation,
                                                "route predecessor chain is incomplete", "route");
        reversedKinds.push_back(foundPrevious->second.kind);
        cursor = foundPrevious->second.from;
    }
    std::reverse(reversedNodes.begin(), reversedNodes.end());
    std::reverse(reversedKinds.begin(), reversedKinds.end());

    ClimbingAnchorRoute route;
    route.graphId = graph_.id;
    route.graphGeneration = generation_;
    route.nodes.reserve(reversedNodes.size());
    for (const auto& nodeId : reversedNodes) route.nodes.push_back({graph_.id, nodeId, generation_});
    route.steps.reserve(reversedKinds.size());
    for (std::size_t index = 0; index < reversedKinds.size(); ++index)
        route.steps.push_back({route.nodes[index], route.nodes[index + 1], reversedKinds[index]});
    return eve::Result<ClimbingAnchorRoute>::success(std::move(route));
}

eve::Result<ClimbingAnchorReservation> ClimbingAnchorGraphInstance::reserve(
    const ClimbingAnchorNodeRef& reference, ClimbingAnchorOccupant occupant) {
    if (reference.graphId != graph_.id)
        return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::Conflict,
                                                  "anchor node reference belongs to another graph",
                                                  "reference.graphId");
    if (reference.graphGeneration != generation_)
        return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::StaleHandle,
                                                  "anchor node reference belongs to a stale graph generation",
                                                  "reference.graphGeneration");
    const auto* node = findNode(reference.nodeId);
    if (!node)
        return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::NotFound,
                                                  "anchor node was not found", "reference.nodeId");
    if (occupant.agentId.isZero() || occupant.executionId.isZero())
        return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::InvalidArgument,
                                                  "anchor reservation requires non-zero agent and execution ids",
                                                  "occupant");
    std::vector<bool> occupied(node->occupancySlots, false);
    for (const auto& [id, record] : reservations_) {
        (void)id;
        if (record.nodeId != node->id) continue;
        if (record.occupant == occupant)
            return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::Conflict,
                                                      "occupant already owns a slot on this anchor", "occupant");
        if (record.slot < occupied.size()) occupied[record.slot] = true;
    }
    const auto free = std::find(occupied.begin(), occupied.end(), false);
    if (free == occupied.end())
        return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::Conflict,
                                                  "all anchor occupancy slots are reserved", "reference.nodeId");
    const auto next = nextReservationId_.incremented();
    if (!next)
        return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::PreconditionViolation,
                                                  "anchor reservation identity is exhausted", "reservationId");
    const ClimbingAnchorReservationId id = nextReservationId_;
    const std::uint32_t slot = static_cast<std::uint32_t>(std::distance(occupied.begin(), free));
    reservations_.emplace(id, ReservationRecord{node->id, slot, occupant, 1});
    nextReservationId_ = *next;
    return eve::Result<ClimbingAnchorReservation>::success(
        {id, generation_, 1, node->id, slot, occupant}, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ClimbingAnchorGraphInstance::release(const ClimbingAnchorReservation& reservation) {
    auto valid = validateReservation(reservation);
    if (!valid) return valid;
    reservations_.erase(reservation.id);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ClimbingAnchorGraphInstance::validateReservation(
    const ClimbingAnchorReservation& reservation) const {
    if (reservation.graphGeneration != generation_)
        return failure<void>(eve::DiagnosticCode::StaleHandle,
                             "anchor reservation belongs to a stale graph generation",
                             "reservation.graphGeneration");
    const auto found = reservations_.find(reservation.id);
    if (found == reservations_.end())
        return failure<void>(eve::DiagnosticCode::NotFound, "anchor reservation is not live", "reservation.id");
    const auto& record = found->second;
    if (record.nodeId != reservation.nodeId || record.slot != reservation.slot ||
        record.occupant != reservation.occupant || record.claimGeneration != reservation.claimGeneration)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "anchor reservation credential does not match the live record", "reservation");
    return eve::Result<void>::success();
}

eve::Result<ClimbingAnchorReservation> ClimbingAnchorGraphInstance::transferReservation(
    const ClimbingAnchorReservation& reservation) {
    auto valid = validateReservation(reservation);
    if (!valid) return eve::Result<ClimbingAnchorReservation>::failure(valid.status());
    auto found = reservations_.find(reservation.id);
    if (found->second.claimGeneration == std::numeric_limits<std::uint64_t>::max())
        return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::PreconditionViolation,
                                                  "anchor reservation claim generation is exhausted",
                                                  "reservation.claimGeneration");
    ++found->second.claimGeneration;
    ClimbingAnchorReservation transferred = reservation;
    transferred.claimGeneration = found->second.claimGeneration;
    return eve::Result<ClimbingAnchorReservation>::success(
        std::move(transferred), eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ClimbingAnchorReservation> ClimbingAnchorGraphInstance::restoreReservation(
    const ClimbingAnchorReservation& reservation) {
    if (reservation.graphGeneration != generation_)
        return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::StaleHandle,
                                                  "snapshot reservation belongs to a stale graph generation",
                                                  "reservation.graphGeneration");
    const auto* node = findNode(reservation.nodeId);
    if (!node)
        return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::NotFound,
                                                  "snapshot reservation node was not found", "reservation.nodeId");
    if (reservation.id.isZero() || reservation.claimGeneration == 0 || reservation.occupant.agentId.isZero() ||
        reservation.occupant.executionId.isZero() || reservation.slot >= node->occupancySlots)
        return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::InvalidArgument,
                                                  "snapshot reservation credential is invalid", "reservation");
    if (const auto live = reservations_.find(reservation.id); live != reservations_.end()) {
        auto valid = validateReservation(reservation);
        if (!valid) return eve::Result<ClimbingAnchorReservation>::failure(valid.status());
        return transferReservation(reservation);
    }
    for (const auto& [id, record] : reservations_) {
        (void)id;
        if (record.nodeId == reservation.nodeId &&
            (record.slot == reservation.slot || record.occupant == reservation.occupant))
            return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::Conflict,
                                                      "snapshot anchor slot or occupant is already reserved",
                                                      "reservation.slot");
    }
    const auto next = nextReservationId_.incremented();
    if (!next)
        return failure<ClimbingAnchorReservation>(eve::DiagnosticCode::PreconditionViolation,
                                                  "anchor reservation identity is exhausted", "reservationId");
    const ClimbingAnchorReservationId id = nextReservationId_;
    reservations_.emplace(id, ReservationRecord{reservation.nodeId, reservation.slot,
                                                 reservation.occupant, 1});
    nextReservationId_ = *next;
    ClimbingAnchorReservation restored{id, generation_, 1, reservation.nodeId, reservation.slot,
                                       reservation.occupant};
    return eve::Result<ClimbingAnchorReservation>::success(
        std::move(restored), eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::uint32_t> ClimbingAnchorGraphInstance::releaseOccupant(ClimbingAnchorOccupant occupant) {
    if (occupant.agentId.isZero() || occupant.executionId.isZero())
        return failure<std::uint32_t>(eve::DiagnosticCode::InvalidArgument,
                                      "release requires non-zero agent and execution ids", "occupant");
    std::uint32_t count = 0;
    for (auto iterator = reservations_.begin(); iterator != reservations_.end();) {
        if (iterator->second.occupant == occupant) {
            iterator = reservations_.erase(iterator);
            ++count;
        } else {
            ++iterator;
        }
    }
    const eve::StatusCode code = count == 0 ? eve::StatusCode::NoOp : eve::StatusCode::Applied;
    return eve::Result<std::uint32_t>::success(count, eve::Status::success(code));
}

eve::Result<ClimbingAnchorGraphReload> ClimbingAnchorGraphInstance::reload(ClimbingAnchorGraphDefinition graph) {
    auto valid = validateClimbingAnchorGraphDefinition(graph);
    if (!valid) return eve::Result<ClimbingAnchorGraphReload>::failure(valid.status());
    if (generation_ == std::numeric_limits<std::uint64_t>::max())
        return failure<ClimbingAnchorGraphReload>(eve::DiagnosticCode::PreconditionViolation,
                                                  "anchor graph generation is exhausted", "generation");
    canonicalize(graph);
    ClimbingAnchorGraphReload result;
    result.oldGeneration = generation_;
    result.newGeneration = generation_ + 1;
    result.invalidatedOccupants.reserve(reservations_.size());
    for (const auto& [id, record] : reservations_) {
        (void)id;
        result.invalidatedOccupants.push_back(record.occupant);
    }
    std::sort(result.invalidatedOccupants.begin(), result.invalidatedOccupants.end());
    result.invalidatedOccupants.erase(
        std::unique(result.invalidatedOccupants.begin(), result.invalidatedOccupants.end()),
        result.invalidatedOccupants.end());
    graph_ = std::move(graph);
    generation_ = result.newGeneration;
    reservations_.clear();
    return eve::Result<ClimbingAnchorGraphReload>::success(std::move(result),
                                                            eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::climbing
