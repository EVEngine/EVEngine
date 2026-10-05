#include "procgen/editing/RoadNetworkGizmo.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::procgen_editing {
namespace {

editing::GizmoSnapshot failedSnapshot(const RoadNetworkDocument& document, const char* rule, std::string message) {
    editing::GizmoSnapshot result;
    result.target         = document.targetId().value();
    result.targetRevision = document.revision();
    result.diagnostics.push_back(editing::ruleDiagnostic(DiagnosticCode::InvalidArgument, RuleId(rule),
                                                         DiagnosticSeverity::Error, std::move(message)));
    return result;
}

void addLine(editing::GizmoSnapshot& output, std::string id, const std::array<double, 3>& from,
             const std::array<double, 3>& to, std::array<double, 4> color, bool dashed = false) {
    std::array<double, 3> direction{to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    const double          length =
        std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (length <= 1e-12) return;
    for (double& value : direction) value /= length;
    output.primitives.push_back({std::move(id), "line", from, {}, direction, color, 0.0, length, dashed});
}

bool normalizeRay(double x, double y, double z, std::array<double, 3>& output) {
    const double length = std::sqrt(x * x + y * y + z * z);
    if (!std::isfinite(length) || length < 1e-9) return false;
    output = {x / length, y / length, z / length};
    return true;
}

bool validColor(const std::array<double, 4>& color) {
    return std::all_of(color.begin(), color.end(),
                       [](double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; });
}

bool intersectPlane(const std::array<double, 3>& origin, const std::array<double, 3>& direction,
                    const std::array<double, 3>& point, const std::array<double, 3>& normal,
                    std::array<double, 3>& hit) {
    const double denominator = direction[0] * normal[0] + direction[1] * normal[1] + direction[2] * normal[2];
    if (std::abs(denominator) < 1e-9) return false;
    const double distance =
        ((point[0] - origin[0]) * normal[0] + (point[1] - origin[1]) * normal[1] + (point[2] - origin[2]) * normal[2]) /
        denominator;
    if (distance < 0.0) return false;
    hit = {origin[0] + direction[0] * distance, origin[1] + direction[1] * distance,
           origin[2] + direction[2] * distance};
    return true;
}

editing::GizmoSnapshot buildDocumentOverlay(const RoadNetworkDocument& document, const RoadGizmoStyle& style) {
    editing::GizmoSnapshot result;
    result.target         = document.targetId().value();
    result.targetRevision = document.revision();
    for (const auto& node : document.nodes()) {
        const std::array<double, 3> position{node.x, node.y, node.z};
        result.primitives.push_back(
            {"road.node." + node.id.value(), "sphere", position, {}, {}, {1.0, 0.55, 0.15, 1.0}, style.nodeSize});
        if (style.showLabels) {
            editing::GizmoPrimitive label;
            label.id       = "road.label." + node.id.value();
            label.kind     = "text";
            label.position = position;
            label.color    = style.textColor;
            label.text     = node.id.value();
            result.primitives.push_back(std::move(label));
        }
    }
    for (const auto& edge : document.edges()) {
        for (std::size_t index = 1; index < edge.controlPoints.size(); ++index) {
            const auto& previous = edge.controlPoints[index - 1];
            const auto& current  = edge.controlPoints[index];
            addLine(result, "road.edge." + edge.id.value() + ".segment." + std::to_string(index - 1),
                    {previous.x, previous.y, previous.z}, {current.x, current.y, current.z}, style.edgeColor);
        }
        if (!style.showControlPoints) continue;
        for (std::size_t index = 1; index + 1 < edge.controlPoints.size(); ++index) {
            const auto& point = edge.controlPoints[index];
            result.primitives.push_back({"road.cp." + edge.id.value() + "." + std::to_string(index), "sphere",
                                         {point.x, point.y, point.z},
                                         {},
                                         {},
                                         {0.35, 0.95, 0.55, 1.0},
                                         style.controlPointSize});
        }
    }
    if (document.nodes().empty()) {
        result.diagnostics.push_back(editing::ruleDiagnostic(
            DiagnosticCode::PreconditionViolation, RuleId("editor.road.gizmo-empty"), DiagnosticSeverity::Warning,
            "Add a junction node to begin laying a road network"));
    } else if (document.edges().empty()) {
        result.diagnostics.push_back(editing::ruleDiagnostic(
            DiagnosticCode::PreconditionViolation, RuleId("editor.road.gizmo-incomplete"), DiagnosticSeverity::Warning,
            "Connect at least two junction nodes with an edge to preview road strips"));
    }
    result.status = EditorStatus::Applied;
    return result;
}

}  // namespace

editing::GizmoSnapshot RoadNetworkGizmoBuilder::build(const RoadNetworkDocument& document,
                                                      RoadGizmoStyle             style) const {
    if (!std::isfinite(style.nodeSize) || !std::isfinite(style.controlPointSize) || style.nodeSize <= 0.0 ||
        style.controlPointSize <= 0.0 || !validColor(style.edgeColor) || !validColor(style.textColor) ||
        !validColor(style.layPreviewColor))
        return failedSnapshot(document, "editor.road.gizmo-style-invalid",
                              "Road gizmo sizes must be positive and colors must be finite normalized values");
    return buildDocumentOverlay(document, style);
}

RoadNetworkDragSession::RoadNetworkDragSession(RoadNetworkDocument* document, RoadGizmoStyle style)
    : document_(document), style_(std::move(style)) {}

EditorResult<RoadDragPreview> RoadNetworkDragSession::previewCurrent() const {
    if (!document_ || !dragging_)
        return eve::editing::failed<RoadDragPreview>(EditorStatus::Rejected, RuleId("editor.road.drag-not-active"),
                                                     "Road drag has not begun");
    RoadNetworkDocument candidate = *document_;
    if (activeHandle_ == "node") {
        auto operation = candidate.makeMoveNode(activeNode_, nodeDraft_.x, nodeDraft_.y, nodeDraft_.z);
        if (!operation.ok() || !candidate.applyDomainOperation(operation.value()).ok())
            return eve::editing::failed<RoadDragPreview>(EditorStatus::Rejected,
                                                         RuleId("editor.road.drag-preview-invalid"),
                                                         "Road node draft could not be previewed");
    } else {
        auto operation = candidate.makeSetEdge(edgeDraft_);
        if (!operation.ok() || !candidate.applyDomainOperation(operation.value()).ok())
            return eve::editing::failed<RoadDragPreview>(EditorStatus::Rejected,
                                                         RuleId("editor.road.drag-preview-invalid"),
                                                         "Road control-point draft could not be previewed");
    }
    RoadDragPreview result;
    result.status        = EditorStatus::Applied;
    result.handle        = activeHandle_;
    result.node          = activeNode_;
    result.edge          = activeEdge_;
    result.controlIndex  = activeControlIndex_;
    result.nodeDraft     = nodeDraft_;
    result.edgeDraft     = edgeDraft_;
    result.gizmo         = RoadNetworkGizmoBuilder().build(candidate, style_);
    if (result.gizmo.status != EditorStatus::Applied) result.status = result.gizmo.status;
    return eve::editing::applied<RoadDragPreview>(std::move(result));
}

EditorResult<RoadDragPreview> RoadNetworkDragSession::beginDrag(double rayOriginX, double rayOriginY,
                                                                double rayOriginZ, double rayDirectionX,
                                                                double rayDirectionY, double rayDirectionZ) {
    cancelDrag();
    if (!document_)
        return eve::editing::failed<RoadDragPreview>(EditorStatus::Rejected,
                                                     RuleId("editor.road.drag-target-required"),
                                                     "Road drag target is unavailable");
    std::array<double, 3> direction;
    if (!normalizeRay(rayDirectionX, rayDirectionY, rayDirectionZ, direction))
        return eve::editing::failed<RoadDragPreview>(EditorStatus::Rejected, RuleId("editor.road.drag-ray-invalid"),
                                                     "Road drag requires a finite non-zero pointer ray");
    const std::array<double, 3> origin{rayOriginX, rayOriginY, rayOriginZ};
    const auto                  gizmo = RoadNetworkGizmoBuilder().build(*document_, style_);
    if (gizmo.status != EditorStatus::Applied)
        return eve::editing::failed<RoadDragPreview>(EditorStatus::Rejected, RuleId("editor.road.drag-gizmo-invalid"),
                                                     "Road gizmo is not available for picking");
    double      closest = std::numeric_limits<double>::infinity();
    std::string pickedId;
    for (const auto& primitive : gizmo.primitives) {
        if (primitive.kind != "sphere") continue;
        if (primitive.id.rfind("road.node.", 0) != 0 && primitive.id.rfind("road.cp.", 0) != 0) continue;
        const std::array<double, 3> offset{origin[0] - primitive.position[0], origin[1] - primitive.position[1],
                                           origin[2] - primitive.position[2]};
        const double                b = offset[0] * direction[0] + offset[1] * direction[1] + offset[2] * direction[2];
        const double                c =
            offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2] - primitive.radius * primitive.radius;
        const double discriminant = b * b - c;
        if (discriminant < 0.0) continue;
        const double nearHit = -b - std::sqrt(discriminant);
        const double farHit  = -b + std::sqrt(discriminant);
        const double hit     = nearHit >= 0.0 ? nearHit : farHit;
        if (hit < 0.0 || hit >= closest) continue;
        closest         = hit;
        pickedId        = primitive.id;
        dragPlanePoint_ = primitive.position;
    }
    if (pickedId.empty())
        return eve::editing::failed<RoadDragPreview>(EditorStatus::NotFound, RuleId("editor.road.handle-not-hit"),
                                                     "Pointer ray did not hit a road handle");
    dragPlaneNormal_ = direction;
    baseRevision_    = document_->revision();
    if (pickedId.rfind("road.node.", 0) == 0) {
        activeHandle_ = "node";
        activeNode_   = StableId(pickedId.substr(std::string("road.node.").size()));
        const auto nodes = document_->nodes();
        const auto found =
            std::find_if(nodes.begin(), nodes.end(), [&](const auto& node) { return node.id == activeNode_; });
        if (found == nodes.end())
            return eve::editing::failed<RoadDragPreview>(EditorStatus::NotFound, RuleId("editor.road.node-not-found"),
                                                         "Picked road node no longer exists");
        nodeDraft_ = *found;
    } else {
        activeHandle_                = "cp";
        const auto remainder         = pickedId.substr(std::string("road.cp.").size());
        const auto separator         = remainder.rfind('.');
        if (separator == std::string::npos)
            return eve::editing::failed<RoadDragPreview>(EditorStatus::Rejected, RuleId("editor.road.handle-invalid"),
                                                         "Road control-point pick id is malformed");
        activeEdge_                  = StableId(remainder.substr(0, separator));
        activeControlIndex_          = std::stoi(remainder.substr(separator + 1));
        const auto edges             = document_->edges();
        const auto found =
            std::find_if(edges.begin(), edges.end(), [&](const auto& edge) { return edge.id == activeEdge_; });
        if (found == edges.end() || activeControlIndex_ <= 0 ||
            activeControlIndex_ >= static_cast<int>(found->controlPoints.size()) - 1)
            return eve::editing::failed<RoadDragPreview>(EditorStatus::NotFound, RuleId("editor.road.edge-not-found"),
                                                         "Picked road control point no longer exists");
        edgeDraft_ = *found;
    }
    dragging_ = true;
    return previewCurrent();
}

EditorResult<RoadDragPreview> RoadNetworkDragSession::updateDrag(double rayOriginX, double rayOriginY,
                                                                 double rayOriginZ, double rayDirectionX,
                                                                 double rayDirectionY, double rayDirectionZ) {
    if (!dragging_ || !document_)
        return eve::editing::failed<RoadDragPreview>(EditorStatus::Rejected, RuleId("editor.road.drag-not-active"),
                                                     "Road drag has not begun");
    if (document_->revision() != baseRevision_) {
        cancelDrag();
        return eve::editing::failed<RoadDragPreview>(EditorStatus::Conflict, RuleId("editor.road.drag-stale"),
                                                     "Road document changed while dragging");
    }
    std::array<double, 3> direction;
    if (!normalizeRay(rayDirectionX, rayDirectionY, rayDirectionZ, direction))
        return eve::editing::failed<RoadDragPreview>(EditorStatus::Rejected, RuleId("editor.road.drag-ray-invalid"),
                                                     "Road drag requires a finite non-zero pointer ray");
    std::array<double, 3> world;
    if (!intersectPlane({rayOriginX, rayOriginY, rayOriginZ}, direction, dragPlanePoint_, dragPlaneNormal_, world))
        return eve::editing::failed<RoadDragPreview>(EditorStatus::Rejected, RuleId("editor.road.drag-ray-parallel"),
                                                     "Pointer ray does not intersect the road drag plane");
    if (activeHandle_ == "node") {
        nodeDraft_.x = world[0];
        nodeDraft_.y = world[1];
        nodeDraft_.z = world[2];
    } else {
        auto& point = edgeDraft_.controlPoints[static_cast<std::size_t>(activeControlIndex_)];
        point.x     = world[0];
        point.y     = world[1];
        point.z     = world[2];
    }
    return previewCurrent();
}

EditorResult<DomainOperation> RoadNetworkDragSession::finishDrag() {
    if (!dragging_ || !document_)
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.road.drag-not-active"),
                                                     "Road drag has not begun");
    if (document_->revision() != baseRevision_) {
        cancelDrag();
        return eve::editing::failed<DomainOperation>(EditorStatus::Conflict, RuleId("editor.road.drag-stale"),
                                                     "Road document changed before drag commit");
    }
    EditorResult<DomainOperation> operation =
        activeHandle_ == "node"
            ? document_->makeMoveNode(activeNode_, nodeDraft_.x, nodeDraft_.y, nodeDraft_.z)
            : document_->makeSetEdge(edgeDraft_);
    if (operation.ok()) cancelDrag();
    return operation;
}

void RoadNetworkDragSession::cancelDrag() {
    dragging_           = false;
    activeHandle_.clear();
    activeNode_         = {};
    activeEdge_         = {};
    activeControlIndex_ = -1;
}

RoadLaySession::RoadLaySession(RoadNetworkDocument* document, RoadLayDefaults defaults, RoadGizmoStyle style)
    : document_(document), defaults_(defaults), style_(std::move(style)) {}

StableId RoadLaySession::findSnapNode(double x, double y, double z) const {
    if (!document_ || !(defaults_.snapRadius > 0.0)) return {};
    StableId best;
    double   bestDistance = defaults_.snapRadius * defaults_.snapRadius;
    for (const auto& node : document_->nodes()) {
        const double dx = node.x - x;
        const double dy = node.y - y;
        const double dz = node.z - z;
        const double d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= bestDistance) {
            bestDistance = d2;
            best         = node.id;
        }
    }
    return best;
}

EditorResult<RoadLayPreview> RoadLaySession::previewCurrent(bool withHover, double hx, double hy, double hz) const {
    if (!document_ || !laying_)
        return eve::editing::failed<RoadLayPreview>(EditorStatus::Rejected, RuleId("editor.road.lay-not-active"),
                                                    "Road lay has not begun");
    RoadLayPreview result;
    result.status     = EditorStatus::Applied;
    result.activeFrom = activeFrom_;
    result.gizmo      = RoadNetworkGizmoBuilder().build(*document_, style_);
    if (withHover) {
        result.hasHover = true;
        result.hover    = {hx, hy, hz};
        result.snapNode = findSnapNode(hx, hy, hz);
        std::array<double, 3> tip = result.hover;
        if (!result.snapNode.empty()) {
            for (const auto& node : document_->nodes()) {
                if (node.id != result.snapNode) continue;
                tip = {node.x, node.y, node.z};
                break;
            }
        }
        if (!activeFrom_.empty()) {
            for (const auto& node : document_->nodes()) {
                if (node.id != activeFrom_) continue;
                addLine(result.gizmo, "road.lay.preview", {node.x, node.y, node.z}, tip, style_.layPreviewColor, true);
                break;
            }
        } else {
            result.gizmo.primitives.push_back(
                {"road.lay.tip", "sphere", tip, {}, {}, style_.layPreviewColor, style_.nodeSize * 0.85});
        }
    }
    if (result.gizmo.status != EditorStatus::Applied) result.status = result.gizmo.status;
    return eve::editing::applied<RoadLayPreview>(std::move(result));
}

EditorResult<RoadLayPreview> RoadLaySession::begin() {
    if (!document_)
        return eve::editing::failed<RoadLayPreview>(EditorStatus::Rejected, RuleId("editor.road.lay-target-required"),
                                                    "Road lay target is unavailable");
    cancel();
    laying_ = true;
    return previewCurrent(false, 0.0, 0.0, 0.0);
}

EditorResult<RoadLayPreview> RoadLaySession::beginFromNode(const StableId& node) {
    if (!document_)
        return eve::editing::failed<RoadLayPreview>(EditorStatus::Rejected, RuleId("editor.road.lay-target-required"),
                                                    "Road lay target is unavailable");
    const auto nodes = document_->nodes();
    const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const auto& entry) { return entry.id == node; });
    if (found == nodes.end())
        return eve::editing::failed<RoadLayPreview>(EditorStatus::NotFound, RuleId("editor.road.node-not-found"),
                                                    "Road lay start node was not found");
    cancel();
    laying_     = true;
    activeFrom_ = node;
    return previewCurrent(false, 0.0, 0.0, 0.0);
}

EditorResult<RoadLayPreview> RoadLaySession::updateHover(double x, double y, double z) {
    if (!laying_ || !document_)
        return eve::editing::failed<RoadLayPreview>(EditorStatus::Rejected, RuleId("editor.road.lay-not-active"),
                                                    "Road lay has not begun");
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return eve::editing::failed<RoadLayPreview>(EditorStatus::Rejected, RuleId("editor.road.lay-point-invalid"),
                                                    "Road lay hover requires finite coordinates");
    return previewCurrent(true, x, y, z);
}

EditorResult<RoadLayCommit> RoadLaySession::commitClick(double x, double y, double z, StableId newNodeId,
                                                        StableId newEdgeId) {
    if (!laying_ || !document_)
        return eve::editing::failed<RoadLayCommit>(EditorStatus::Rejected, RuleId("editor.road.lay-not-active"),
                                                   "Road lay has not begun");
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return eve::editing::failed<RoadLayCommit>(EditorStatus::Rejected, RuleId("editor.road.lay-point-invalid"),
                                                   "Road lay click requires finite coordinates");
    if (!(defaults_.junctionRadius > 0.0) || !(defaults_.laneWidth > 0.0) || defaults_.lanesForward < 1 ||
        defaults_.lanesForward > 4 || defaults_.lanesBackward < 0 || defaults_.lanesBackward > 4)
        return eve::editing::failed<RoadLayCommit>(EditorStatus::Rejected, RuleId("editor.road.lay-defaults-invalid"),
                                                   "Road lay defaults require positive radius/width and valid lanes");

    RoadLayCommit commit;
    StableId      tip = findSnapNode(x, y, z);
    if (tip.empty()) {
        if (newNodeId.empty())
            return eve::editing::failed<RoadLayCommit>(EditorStatus::Rejected, RuleId("editor.road.lay-node-id-required"),
                                                       "Creating a road node requires a stable id");
        RoadNodeRecord node;
        node.id             = newNodeId;
        node.x              = x;
        node.y              = y;
        node.z              = z;
        node.junctionRadius = defaults_.junctionRadius;
        auto setNode        = document_->makeSetNode(node);
        if (!setNode.ok())
            return eve::editing::failed<RoadLayCommit>(setNode.code(), RuleId("editor.road.lay-node-failed"),
                                                       setNode.status().describe());
        commit.operations.push_back(std::move(setNode).takeValue());
        commit.createdNode = newNodeId;
        tip                = newNodeId;
    }

    if (!activeFrom_.empty()) {
        if (tip == activeFrom_)
            return eve::editing::failed<RoadLayCommit>(EditorStatus::Rejected, RuleId("editor.road.lay-same-node"),
                                                       "Road lay cannot create a zero-length edge");
        if (newEdgeId.empty())
            return eve::editing::failed<RoadLayCommit>(EditorStatus::Rejected, RuleId("editor.road.lay-edge-id-required"),
                                                       "Creating a road edge requires a stable id");
        RoadNetworkDocument candidate = *document_;
        for (const auto& operation : commit.operations) {
            auto applied = candidate.applyDomainOperation(operation);
            if (!applied.ok())
                return eve::editing::failed<RoadLayCommit>(applied.code(), RuleId("editor.road.lay-preview-invalid"),
                                                           applied.status().describe());
        }
        const auto nodes = candidate.nodes();
        const auto from  = std::find_if(nodes.begin(), nodes.end(),
                                        [&](const auto& node) { return node.id == activeFrom_; });
        const auto to    = std::find_if(nodes.begin(), nodes.end(), [&](const auto& node) { return node.id == tip; });
        if (from == nodes.end() || to == nodes.end())
            return eve::editing::failed<RoadLayCommit>(EditorStatus::NotFound, RuleId("editor.road.lay-endpoint-missing"),
                                                       "Road lay endpoints must exist after node creation");
        RoadEdgeRecord edge;
        edge.id            = newEdgeId;
        edge.from          = activeFrom_;
        edge.to            = tip;
        edge.lanesForward  = defaults_.lanesForward;
        edge.lanesBackward = defaults_.lanesBackward;
        edge.laneWidth     = defaults_.laneWidth;
        edge.controlPoints = {{from->x, from->y, from->z}, {to->x, to->y, to->z}};
        auto setEdge       = candidate.makeSetEdge(edge);
        if (!setEdge.ok())
            return eve::editing::failed<RoadLayCommit>(setEdge.code(), RuleId("editor.road.lay-edge-failed"),
                                                       setEdge.status().describe());
        commit.operations.push_back(std::move(setEdge).takeValue());
        commit.createdEdge = newEdgeId;
    }

    activeFrom_        = tip;
    commit.activeFrom  = activeFrom_;
    auto preview       = previewCurrent(false, 0.0, 0.0, 0.0);
    if (!preview.ok())
        return eve::editing::failed<RoadLayCommit>(preview.code(), RuleId("editor.road.lay-preview-invalid"),
                                                   preview.status().describe());
    // Preview gizmo is based on the live document (ops not yet applied). Overlay the tip marker.
    commit.gizmo = std::move(preview).takeValue().gizmo;
    return eve::editing::applied<RoadLayCommit>(std::move(commit));
}

void RoadLaySession::cancel() {
    laying_     = false;
    activeFrom_ = {};
}

}  // namespace eve::procgen_editing
