#include "procgen/editing/RoadNetworkDocument.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::procgen_editing {
namespace {

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

DomainOperation operation(const char* type, const char* inverseType, const std::string& target, EditorValue payload,
                          EditorValue inverse, const StableId& affected) {
    DomainOperation result;
    result.type        = type;
    result.inverseType = inverseType;
    result.target      = TargetId(target);
    result.payload     = std::move(payload);
    result.inverse     = std::move(inverse);
    result.hasInverse  = true;
    result.affectedObjects.push_back({TargetId(target), affected.value(), 0});
    return result;
}

EditorValue controlPointValue(const RoadControlPointRecord& point) {
    return EditorValue::Object{{"x", point.x}, {"y", point.y}, {"z", point.z}};
}

EditorValue nodeValue(const RoadNodeRecord& node) {
    return EditorValue::Object{{"id", node.id.value()},
                               {"x", node.x},
                               {"y", node.y},
                               {"z", node.z},
                               {"junctionRadius", node.junctionRadius}};
}

EditorValue edgeValue(const RoadEdgeRecord& edge) {
    EditorValue::Array points;
    points.reserve(edge.controlPoints.size());
    for (const auto& point : edge.controlPoints) points.push_back(controlPointValue(point));
    return EditorValue::Object{{"id", edge.id.value()},
                               {"from", edge.from.value()},
                               {"to", edge.to.value()},
                               {"controlPoints", std::move(points)},
                               {"lanesForward", static_cast<std::int64_t>(edge.lanesForward)},
                               {"lanesBackward", static_cast<std::int64_t>(edge.lanesBackward)},
                               {"laneWidth", edge.laneWidth}};
}

EditorResult<RoadControlPointRecord> parseControlPoint(const EditorValue& value) {
    const auto* x = field(value, "x") ? field(value, "x")->getIf<double>() : nullptr;
    const auto* y = field(value, "y") ? field(value, "y")->getIf<double>() : nullptr;
    const auto* z = field(value, "z") ? field(value, "z")->getIf<double>() : nullptr;
    if (!x || !y || !z || !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z))
        return eve::editing::failed<RoadControlPointRecord>(
            EditorStatus::Rejected, RuleId("editor.road.invalid-control-point"),
            "Road control point requires finite x/y/z");
    return eve::editing::applied<RoadControlPointRecord>({*x, *y, *z});
}

EditorResult<RoadNodeRecord> parseNode(const EditorValue& value) {
    const auto* id     = field(value, "id") ? field(value, "id")->getIf<std::string>() : nullptr;
    const auto* x      = field(value, "x") ? field(value, "x")->getIf<double>() : nullptr;
    const auto* y      = field(value, "y") ? field(value, "y")->getIf<double>() : nullptr;
    const auto* z      = field(value, "z") ? field(value, "z")->getIf<double>() : nullptr;
    const auto* radius = field(value, "junctionRadius") ? field(value, "junctionRadius")->getIf<double>() : nullptr;
    const double jr    = radius ? *radius : 6.0;
    if (!id || id->empty() || !x || !y || !z || !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z) ||
        !std::isfinite(jr) || jr <= 0.0)
        return eve::editing::failed<RoadNodeRecord>(EditorStatus::Rejected, RuleId("editor.road.invalid-node"),
                                                    "Road node requires stable id, finite position and positive radius");
    return eve::editing::applied<RoadNodeRecord>({StableId(*id), *x, *y, *z, jr});
}

EditorResult<RoadEdgeRecord> parseEdge(const EditorValue& value) {
    const auto* id   = field(value, "id") ? field(value, "id")->getIf<std::string>() : nullptr;
    const auto* from = field(value, "from") ? field(value, "from")->getIf<std::string>() : nullptr;
    const auto* to   = field(value, "to") ? field(value, "to")->getIf<std::string>() : nullptr;
    const auto* cps  = field(value, "controlPoints") ? field(value, "controlPoints")->getIf<EditorValue::Array>()
                                                     : nullptr;
    const auto* lf   = field(value, "lanesForward") ? field(value, "lanesForward")->getIf<std::int64_t>() : nullptr;
    const auto* lb   = field(value, "lanesBackward") ? field(value, "lanesBackward")->getIf<std::int64_t>() : nullptr;
    const auto* lw   = field(value, "laneWidth") ? field(value, "laneWidth")->getIf<double>() : nullptr;
    const int   lanesF = lf ? static_cast<int>(*lf) : 2;
    const int   lanesB = lb ? static_cast<int>(*lb) : 0;
    const double width = lw ? *lw : 3.5;
    if (!id || id->empty() || !from || from->empty() || !to || to->empty() || !cps || cps->size() < 2 || lanesF < 1 ||
        lanesF > 4 || lanesB < 0 || lanesB > 4 || !std::isfinite(width) || width <= 0.0)
        return eve::editing::failed<RoadEdgeRecord>(
            EditorStatus::Rejected, RuleId("editor.road.invalid-edge"),
            "Road edge requires id, endpoints, >=2 control points and valid lane counts");
    RoadEdgeRecord edge;
    edge.id            = StableId(*id);
    edge.from          = StableId(*from);
    edge.to            = StableId(*to);
    edge.lanesForward  = lanesF;
    edge.lanesBackward = lanesB;
    edge.laneWidth     = width;
    edge.controlPoints.reserve(cps->size());
    for (const auto& entry : *cps) {
        auto point = parseControlPoint(entry);
        if (!point.ok())
            return eve::editing::failed<RoadEdgeRecord>(point.code(), RuleId("editor.road.invalid-control-point"),
                                                        point.status().describe());
        edge.controlPoints.push_back(std::move(point).takeValue());
    }
    return eve::editing::applied<RoadEdgeRecord>(std::move(edge));
}

EditorResult<void> validateGraph(const std::map<StableId, RoadNodeRecord>& nodes,
                                 const std::map<StableId, RoadEdgeRecord>& edges) {
    for (const auto& [unused, edge] : edges) {
        (void)unused;
        if (!nodes.contains(edge.from) || !nodes.contains(edge.to))
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.road.dangling-endpoint"),
                                              "Road edge endpoints must reference live nodes");
        if (edge.controlPoints.size() < 2)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.road.short-polyline"),
                                              "Road edge needs at least two control points");
    }
    return eve::editing::applied<void>();
}

}  // namespace

RoadNetworkDocument::RoadNetworkDocument(std::string id) : id_(std::move(id)) {}

TargetDescriptor RoadNetworkDocument::describe() const {
    return {TargetId(id_),
            "road-network-document",
            revisionValue(),
            false,
            {IRoadNetworkDocumentEditTarget::editingCapabilityId()}};
}

void* RoadNetworkDocument::queryCapability(const CapabilityId& capability) {
    return capability == IRoadNetworkDocumentEditTarget::editingCapabilityId()
               ? static_cast<IRoadNetworkDocumentEditTarget*>(this)
               : nullptr;
}

EditorResult<void> RoadNetworkDocument::applyDomainOperation(const DomainOperation& value) {
    if (value.target != TargetId(id_))
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.road.target-mismatch"),
                                          "Road operation targets another document");
    if (value.type == "road.snapshot.replace.v1") {
        RoadNetworkDocument candidate(id_);
        auto                loaded = candidate.loadSnapshot(value.payload);
        if (!loaded.ok())
            return eve::editing::failed<void>(loaded.code(), RuleId("editor.road.invalid-replacement"),
                                              loaded.status().describe());
        candidate.setRevision(revisionValue() + 1);
        candidate.widenDirty(0, 0);
        *this = std::move(candidate);
        return eve::editing::applied<void>();
    }
    if (value.type == "road.node.set.v1") {
        auto parsed = parseNode(value.payload);
        if (!parsed.ok())
            return eve::editing::failed<void>(parsed.code(), RuleId("editor.road.invalid-node"), "Invalid road node");
        nodes_[parsed.value().id] = std::move(parsed.value());
    } else     if (value.type == "road.node.delete.v1") {
        auto parsed = parseNode(value.payload);
        if (!parsed.ok() || !nodes_.contains(parsed.value().id))
            return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.road.node-not-found"),
                                              "Road node was not found");
        for (const auto& [unused, edge] : edges_) {
            (void)unused;
            if (edge.from == parsed.value().id || edge.to == parsed.value().id)
                return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.road.node-still-referenced"),
                                                  "Delete incident edges before deleting a road node");
        }
        nodes_.erase(parsed.value().id);
    } else if (value.type == "road.edge.set.v1") {
        auto parsed = parseEdge(value.payload);
        if (!parsed.ok())
            return eve::editing::failed<void>(parsed.code(), RuleId("editor.road.invalid-edge"), "Invalid road edge");
        if (!nodes_.contains(parsed.value().from) || !nodes_.contains(parsed.value().to))
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.road.dangling-endpoint"),
                                              "Road edge endpoints must reference live nodes");
        edges_[parsed.value().id] = std::move(parsed.value());
    } else if (value.type == "road.edge.delete.v1") {
        auto parsed = parseEdge(value.payload);
        if (!parsed.ok() || !edges_.erase(parsed.value().id))
            return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.road.edge-not-found"),
                                              "Road edge was not found");
    } else {
        return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("editor.road.operation-unsupported"),
                                          "Road operation is unsupported");
    }
    bumpRevision();
    widenDirty(0, 0);
    return eve::editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> RoadNetworkDocument::cloneDomainState() const {
    return std::make_unique<RoadNetworkDocument>(*this);
}

EditorResult<void> RoadNetworkDocument::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<RoadNetworkDocument*>(candidate.get());
    if (!typed || typed->id_ != id_)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.road.invalid-candidate"),
                                          "Road candidate must match this document");
    auto graph = validateGraph(typed->nodes_, typed->edges_);
    if (!graph.ok()) return graph;
    typed->setRevision(revisionValue() + 1);
    *this = *typed;
    widenDirty(0, 0);
    return eve::editing::applied<void>();
}

EditorResult<DomainOperation> RoadNetworkDocument::makeSetNode(const RoadNodeRecord& node) const {
    auto parsed = parseNode(nodeValue(node));
    if (!parsed.ok())
        return eve::editing::failed<DomainOperation>(parsed.code(), RuleId("editor.road.invalid-node"),
                                                     parsed.status().describe());
    const bool exists           = nodes_.contains(node.id);
    const char* inverseType     = exists ? "road.node.set.v1" : "road.node.delete.v1";
    const EditorValue inverse   = exists ? nodeValue(nodes_.at(node.id)) : nodeValue(node);
    return eve::editing::applied<DomainOperation>(
        operation("road.node.set.v1", inverseType, id_, nodeValue(node), inverse, node.id));
}

EditorResult<DomainOperation> RoadNetworkDocument::makeDeleteNode(const StableId& node) const {
    const auto found = nodes_.find(node);
    if (found == nodes_.end())
        return eve::editing::failed<DomainOperation>(EditorStatus::NotFound, RuleId("editor.road.node-not-found"),
                                                     "Road node was not found");
    for (const auto& [unused, edge] : edges_) {
        (void)unused;
        if (edge.from == node || edge.to == node)
            return eve::editing::failed<DomainOperation>(
                EditorStatus::Rejected, RuleId("editor.road.node-still-referenced"),
                "Delete incident edges before deleting a road node");
    }
    return eve::editing::applied<DomainOperation>(
        operation("road.node.delete.v1", "road.node.set.v1", id_, nodeValue(found->second), nodeValue(found->second),
                  node));
}

EditorResult<DomainOperation> RoadNetworkDocument::makeSetEdge(const RoadEdgeRecord& edge) const {
    auto parsed = parseEdge(edgeValue(edge));
    if (!parsed.ok())
        return eve::editing::failed<DomainOperation>(parsed.code(), RuleId("editor.road.invalid-edge"),
                                                     parsed.status().describe());
    if (!nodes_.contains(edge.from) || !nodes_.contains(edge.to))
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.road.dangling-endpoint"),
                                                     "Road edge endpoints must reference live nodes");
    const char* inverseType = edges_.contains(edge.id) ? "road.edge.set.v1" : "road.edge.delete.v1";
    EditorValue inverse =
        edges_.contains(edge.id) ? edgeValue(edges_.at(edge.id)) : edgeValue(edge);
    return eve::editing::applied<DomainOperation>(
        operation("road.edge.set.v1", inverseType, id_, edgeValue(edge), std::move(inverse), edge.id));
}

EditorResult<DomainOperation> RoadNetworkDocument::makeDeleteEdge(const StableId& edge) const {
    const auto found = edges_.find(edge);
    if (found == edges_.end())
        return eve::editing::failed<DomainOperation>(EditorStatus::NotFound, RuleId("editor.road.edge-not-found"),
                                                     "Road edge was not found");
    return eve::editing::applied<DomainOperation>(
        operation("road.edge.delete.v1", "road.edge.set.v1", id_, edgeValue(found->second), edgeValue(found->second),
                  edge));
}

EditorResult<DomainOperation> RoadNetworkDocument::makeMoveNode(const StableId& node, double x, double y,
                                                                double z) const {
    const auto found = nodes_.find(node);
    if (found == nodes_.end())
        return eve::editing::failed<DomainOperation>(EditorStatus::NotFound, RuleId("editor.road.node-not-found"),
                                                     "Road node was not found");
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.road.invalid-node"),
                                                     "Road node move requires finite coordinates");
    RoadNetworkDocument candidate = *this;
    RoadNodeRecord      moved     = found->second;
    moved.x                       = x;
    moved.y                       = y;
    moved.z                       = z;
    candidate.nodes_[node]        = moved;
    for (auto& [unused, edge] : candidate.edges_) {
        (void)unused;
        if (edge.controlPoints.empty()) continue;
        if (edge.from == node) {
            edge.controlPoints.front().x = x;
            edge.controlPoints.front().y = y;
            edge.controlPoints.front().z = z;
        }
        if (edge.to == node) {
            edge.controlPoints.back().x = x;
            edge.controlPoints.back().y = y;
            edge.controlPoints.back().z = z;
        }
    }
    DomainOperation result;
    result.type        = "road.snapshot.replace.v1";
    result.inverseType = "road.snapshot.replace.v1";
    result.target      = TargetId(id_);
    result.payload     = candidate.snapshotValue();
    result.inverse     = snapshotValue();
    result.hasInverse  = true;
    result.affectedObjects.push_back({TargetId(id_), node.value(), 0});
    return eve::editing::applied<DomainOperation>(std::move(result));
}

std::vector<RoadNodeRecord> RoadNetworkDocument::nodes() const {
    std::vector<RoadNodeRecord> result;
    result.reserve(nodes_.size());
    for (const auto& [unused, node] : nodes_) {
        (void)unused;
        result.push_back(node);
    }
    return result;
}

std::vector<RoadEdgeRecord> RoadNetworkDocument::edges() const {
    std::vector<RoadEdgeRecord> result;
    result.reserve(edges_.size());
    for (const auto& [unused, edge] : edges_) {
        (void)unused;
        result.push_back(edge);
    }
    return result;
}

EditorResult<procgen::road::RoadNetwork> RoadNetworkDocument::compileNetwork() const {
    auto graph = validateGraph(nodes_, edges_);
    if (!graph.ok())
        return eve::editing::failed<procgen::road::RoadNetwork>(graph.code(), RuleId("editor.road.compile-invalid"),
                                                                graph.status().describe());
    procgen::road::RoadNetwork network;
    std::map<StableId, std::uint32_t> runtimeIds;
    for (const auto& [id, node] : nodes_) {
        auto added = network.addNode(static_cast<float>(node.x), static_cast<float>(node.y),
                                     static_cast<float>(node.z), static_cast<float>(node.junctionRadius));
        if (!added.ok())
            return eve::editing::failed<procgen::road::RoadNetwork>(
                EditorStatus::Failed, RuleId("editor.road.compile-node"), added.status().describe());
        runtimeIds[id] = added.value();
    }
    for (const auto& [unused, edge] : edges_) {
        (void)unused;
        std::vector<procgen::road::RoadControlPoint> points;
        points.reserve(edge.controlPoints.size());
        for (const auto& point : edge.controlPoints)
            points.push_back({static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)});
        procgen::road::RoadStyle style;
        style.laneWidth      = static_cast<float>(edge.laneWidth);
        style.deckThickness  = 0.08f;
        style.pierClearance  = 100.f;
        style.sidewalkWidth  = 1.2f;
        style.curbWidth      = 0.35f;
        style.curbHeight     = 0.45f;
        auto added =
            network.addEdge(runtimeIds.at(edge.from), runtimeIds.at(edge.to), std::move(points), edge.lanesForward,
                            edge.lanesBackward, style);
        if (!added.ok())
            return eve::editing::failed<procgen::road::RoadNetwork>(
                EditorStatus::Failed, RuleId("editor.road.compile-edge"), added.status().describe());
    }
    for (const auto& [id, unused] : nodes_) {
        (void)unused;
        auto turns = network.connectAllTurns(runtimeIds.at(id));
        if (!turns.ok())
            return eve::editing::failed<procgen::road::RoadNetwork>(
                EditorStatus::Failed, RuleId("editor.road.compile-turns"), turns.status().describe());
    }
    return eve::editing::applied<procgen::road::RoadNetwork>(std::move(network));
}

EditorResult<RoadBakePreviewResult> RoadNetworkDocument::previewBake(procgen::road::RoadBakeOptions options) const {
    if (edges_.empty())
        return eve::editing::failed<RoadBakePreviewResult>(EditorStatus::Rejected, RuleId("editor.road.preview-empty"),
                                                           "Road preview requires at least one edge");
    options.includeNavigation = false;
    if (options.pathSegmentsPerEdge < 2) options.pathSegmentsPerEdge = 16;
    auto network = compileNetwork();
    if (!network.ok())
        return eve::editing::failed<RoadBakePreviewResult>(network.code(), RuleId("editor.road.preview-compile"),
                                                           network.status().describe());
    auto baked = procgen::road::bakeRoadNetwork(network.value(), options);
    if (!baked.ok())
        return eve::editing::failed<RoadBakePreviewResult>(EditorStatus::Failed, RuleId("editor.road.preview-bake"),
                                                           baked.status().describe());
    RoadBakePreviewResult result;
    result.mesh      = std::move(baked.value().mesh);
    result.edgeCount = network.value().edgeCount();
    result.nodeCount = network.value().nodeCount();
    return eve::editing::applied<RoadBakePreviewResult>(std::move(result));
}

EditorValue RoadNetworkDocument::snapshotValue() const {
    EditorValue::Array nodeArray;
    EditorValue::Array edgeArray;
    nodeArray.reserve(nodes_.size());
    edgeArray.reserve(edges_.size());
    for (const auto& [unused, node] : nodes_) {
        (void)unused;
        nodeArray.push_back(nodeValue(node));
    }
    for (const auto& [unused, edge] : edges_) {
        (void)unused;
        edgeArray.push_back(edgeValue(edge));
    }
    return EditorValue::Object{{"schema", std::string("eve.procgen.roadNetwork")},
                               {"schemaVersion", static_cast<std::int64_t>(1)},
                               {"nodes", std::move(nodeArray)},
                               {"edges", std::move(edgeArray)}};
}

EditorResult<void> RoadNetworkDocument::loadSnapshot(const EditorValue& snapshot) {
    const auto* schema = field(snapshot, "schema") ? field(snapshot, "schema")->getIf<std::string>() : nullptr;
    const auto* version =
        field(snapshot, "schemaVersion") ? field(snapshot, "schemaVersion")->getIf<std::int64_t>() : nullptr;
    if (!schema || *schema != "eve.procgen.roadNetwork" || !version || *version != 1)
        return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("editor.road.invalid-snapshot"),
                                          "Road snapshot requires eve.procgen.roadNetwork schema version 1");
    const auto* nodeArray = field(snapshot, "nodes") ? field(snapshot, "nodes")->getIf<EditorValue::Array>() : nullptr;
    const auto* edgeArray = field(snapshot, "edges") ? field(snapshot, "edges")->getIf<EditorValue::Array>() : nullptr;
    if (!nodeArray || !edgeArray)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.road.invalid-snapshot"),
                                          "Road snapshot requires nodes and edges arrays");
    std::map<StableId, RoadNodeRecord> nodes;
    std::map<StableId, RoadEdgeRecord> edges;
    for (const auto& entry : *nodeArray) {
        auto parsed = parseNode(entry);
        if (!parsed.ok())
            return eve::editing::failed<void>(parsed.code(), RuleId("editor.road.invalid-snapshot-node"),
                                              parsed.status().describe());
        if (!nodes.emplace(parsed.value().id, parsed.value()).second)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.road.duplicate-node"),
                                              "Road snapshot contains duplicate node ids");
    }
    for (const auto& entry : *edgeArray) {
        auto parsed = parseEdge(entry);
        if (!parsed.ok())
            return eve::editing::failed<void>(parsed.code(), RuleId("editor.road.invalid-snapshot-edge"),
                                              parsed.status().describe());
        if (!edges.emplace(parsed.value().id, parsed.value()).second)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.road.duplicate-edge"),
                                              "Road snapshot contains duplicate edge ids");
    }
    auto graph = validateGraph(nodes, edges);
    if (!graph.ok()) return graph;
    nodes_ = std::move(nodes);
    edges_ = std::move(edges);
    bumpRevision();
    widenDirty(0, 0);
    return eve::editing::applied<void>();
}

}  // namespace eve::procgen_editing
