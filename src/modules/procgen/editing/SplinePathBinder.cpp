#include "procgen/editing/SplinePathBinder.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "common/SceneQuery.h"
#include "procgen/editing/SplinePathGizmo.h"

// SplinePathBinder.cpp
namespace eve::procgen_editing {
namespace {

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

EditorValue bindingValue(const SplinePointBinding& binding) {
    return EditorValue::Object{{"point", EditorValue(binding.point.value())},
                               {"host", EditorValue(binding.host)},
                               {"object", EditorValue(binding.object)}};
}

}  // namespace

Result<SplineBindingPose> SceneQuerySplineBindingSource::resolve(const std::string& host,
                                                                 const std::string& object) const {
    if (!query_)
        return eve::editing::failed<SplineBindingPose>(EditorStatus::Unsupported,
                                                       RuleId("editor.spline.binding-provider-absent"),
                                                       "Scene transform provider is not available");
    SceneNodeInfo info;
    if (!query_->getNodeIn(host, object, &info))
        return eve::editing::failed<SplineBindingPose>(EditorStatus::Conflict,
                                                       RuleId("editor.spline.binding-source-stale"),
                                                       "Bound scene transform is missing or stale");
    return eve::editing::applied<SplineBindingPose>({info.x, info.y, info.z});
}

SplinePathBinder::SplinePathBinder(std::string documentTarget) : documentTarget_(std::move(documentTarget)) {}

Result<void> SplinePathBinder::bindResult(const SplinePathDocument& document, const StableId& point, std::string host,
                                          std::string object, const ISplineBindingTransformSource& source) {
    if (document.targetId().value() != documentTarget_)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.spline.binding-target-mismatch"),
                                          "Spline binder targets another document");
    const auto points = document.points();
    if (point.empty() ||
        std::none_of(points.begin(), points.end(), [&](const auto& value) { return value.id == point; }))
        return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.spline.binding-point-missing"),
                                          "Bound spline point does not exist");
    if (host.empty() || object.empty())
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.spline.binding-identity-empty"),
                                          "Spline binding requires non-empty host and object identities");
    auto resolved = source.resolve(host, object);
    if (!resolved.ok())
        return eve::editing::failed<void>(resolved.code(), RuleId("editor.spline.binding-source-stale"),
                                          resolved.status().describe());
    bindings_[point] = {point, std::move(host), std::move(object)};
    return eve::editing::applied<void>();
}

Result<void> SplinePathBinder::unbindResult(const StableId& point) {
    if (!bindings_.erase(point))
        return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.spline.binding-not-found"),
                                          "Spline point binding was not found");
    return eve::editing::applied<void>();
}

Result<void> SplinePathBinder::refreshResult(SplinePathDocument&                  document,
                                             const ISplineBindingTransformSource& source) const {
    if (document.targetId().value() != documentTarget_)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.spline.binding-target-mismatch"),
                                          "Spline binder targets another document");
    auto       candidate = document;
    const auto points    = document.points();
    for (const auto& [id, binding] : bindings_) {
        const auto found =
            std::find_if(points.begin(), points.end(), [&](const auto& value) { return value.id == id; });
        if (found == points.end())
            return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.spline.binding-point-stale"),
                                              "Bound spline point was removed");
        auto pose = source.resolve(binding.host, binding.object);
        if (!pose.ok())
            return eve::editing::failed<void>(pose.code(), RuleId("editor.spline.binding-source-stale"),
                                              pose.status().describe());
        if (!std::isfinite(pose.value().x) || !std::isfinite(pose.value().y) || !std::isfinite(pose.value().z))
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.spline.binding-pose-invalid"),
                                              "Bound transform returned non-finite coordinates");
        auto updated   = *found;
        updated.x      = pose.value().x;
        updated.y      = pose.value().y;
        updated.z      = pose.value().z;
        auto operation = candidate.makeSetPoint(updated);
        if (!operation.ok())
            return eve::editing::failed<void>(operation.code(), RuleId("editor.spline.binding-operation-failed"),
                                              operation.status().describe());
        auto applied = candidate.applyDomainOperation(operation.value());
        if (!applied.ok()) return applied;
    }
    return document.commitDomainState(std::make_unique<SplinePathDocument>(std::move(candidate)));
}

std::vector<SplinePointBinding> SplinePathBinder::bindings() const {
    std::vector<SplinePointBinding> result;
    result.reserve(bindings_.size());
    for (const auto& [unused, binding] : bindings_) {
        (void)unused;
        result.push_back(binding);
    }
    return result;
}

EditorValue SplinePathBinder::snapshotValue() const {
    EditorValue::Array bindings;
    for (const auto& [unused, binding] : bindings_) {
        (void)unused;
        bindings.emplace_back(bindingValue(binding));
    }
    return EditorValue::Object{{"schema", EditorValue("eve.procgen.splineBinder")},
                               {"version", EditorValue(std::int64_t{1})},
                               {"document", EditorValue(documentTarget_)},
                               {"bindings", EditorValue(std::move(bindings))}};
}

Result<void> SplinePathBinder::loadSnapshot(const EditorValue& snapshot) {
    const auto* schemaValue   = field(snapshot, "schema");
    const auto* versionValue  = field(snapshot, "version");
    const auto* documentValue = field(snapshot, "document");
    const auto* bindingsValue = field(snapshot, "bindings");
    const auto* schema        = schemaValue ? schemaValue->getIf<std::string>() : nullptr;
    const auto* version       = versionValue ? versionValue->getIf<std::int64_t>() : nullptr;
    const auto* document      = documentValue ? documentValue->getIf<std::string>() : nullptr;
    const auto* values        = bindingsValue ? bindingsValue->getIf<EditorValue::Array>() : nullptr;
    if (!schema || *schema != "eve.procgen.splineBinder" || !version || *version != 1)
        return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("editor.spline.binding-schema-unsupported"),
                                          "Unsupported spline binder snapshot schema");
    if (!document || *document != documentTarget_ || !values)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.spline.binding-snapshot-invalid"),
                                          "Spline binder snapshot targets another document or has invalid bindings");
    std::map<StableId, SplinePointBinding> candidate;
    for (const auto& value : *values) {
        const auto* pointValue  = field(value, "point");
        const auto* hostValue   = field(value, "host");
        const auto* objectValue = field(value, "object");
        const auto* point       = pointValue ? pointValue->getIf<std::string>() : nullptr;
        const auto* host        = hostValue ? hostValue->getIf<std::string>() : nullptr;
        const auto* object      = objectValue ? objectValue->getIf<std::string>() : nullptr;
        if (!point || point->empty() || !host || host->empty() || !object || object->empty() ||
            !candidate.emplace(StableId(*point), SplinePointBinding{StableId(*point), *host, *object}).second)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.spline.binding-snapshot-invalid"),
                                              "Spline binder snapshot contains invalid or duplicate links");
    }
    bindings_ = std::move(candidate);
    return eve::editing::applied<void>();
}

}  // namespace eve::procgen_editing

// SplinePathGizmo.cpp
namespace eve::procgen_editing {
namespace {

editing::GizmoSnapshot failedSnapshot(const SplinePathDocument& document, const char* rule, std::string message) {
    editing::GizmoSnapshot result;
    result.target         = document.targetId().value();
    result.targetRevision = document.revision();
    result.diagnostics.push_back(editing::ruleDiagnostic(DiagnosticCode::InvalidArgument, RuleId(rule),
                                                         DiagnosticSeverity::Error, std::move(message)));
    return result;
}

std::array<double, 3> position(const SplinePathControlPoint& point) { return {point.x, point.y, point.z}; }

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

}  // namespace

editing::GizmoSnapshot SplinePathGizmoBuilder::build(const SplinePathDocument& document, int sampleCount,
                                                     int maximumSamples, SplineGizmoStyle style) const {
    if (sampleCount < 2 || maximumSamples < 2 || sampleCount > maximumSamples)
        return failedSnapshot(document, "editor.spline.gizmo-budget",
                              "Spline gizmo sample count exceeds its bounded budget");
    const auto validColor = [](const auto& color) {
        return std::all_of(color.begin(), color.end(),
                           [](double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; });
    };
    if (!std::isfinite(style.nodeSize) || !std::isfinite(style.handleSize) || style.nodeSize <= 0.0 ||
        style.handleSize <= 0.0 || !validColor(style.lineColor) || !validColor(style.textColor))
        return failedSnapshot(document, "editor.spline.gizmo-style-invalid",
                              "Spline gizmo sizes must be positive and colors must be finite normalized values");
    editing::GizmoSnapshot result;
    result.target         = document.targetId().value();
    result.targetRevision = document.revision();
    for (const auto& point : document.points()) {
        const auto anchor = position(point);
        result.primitives.push_back(
            {"spline.anchor." + point.id.value(), "sphere", anchor, {}, {}, {1.0, 0.72, 0.18, 1.0}, style.nodeSize});
        if (style.showPointLabels) {
            editing::GizmoPrimitive label;
            label.id       = "spline.label." + point.id.value();
            label.kind     = "text";
            label.position = anchor;
            label.color    = style.textColor;
            label.text     = point.id.value();
            result.primitives.push_back(std::move(label));
        }
        if (document.settings().kind != "bezier") continue;
        const std::array<double, 3> incoming{point.x + point.inX, point.y + point.inY, point.z + point.inZ};
        const std::array<double, 3> outgoing{point.x + point.outX, point.y + point.outY, point.z + point.outZ};
        addLine(result, "spline.in-line." + point.id.value(), anchor, incoming, {0.95, 0.4, 0.3, 0.9}, true);
        addLine(result, "spline.out-line." + point.id.value(), anchor, outgoing, {0.35, 0.9, 0.45, 0.9}, true);
        result.primitives.push_back(
            {"spline.in." + point.id.value(), "sphere", incoming, {}, {}, {0.95, 0.4, 0.3, 1.0}, style.handleSize});
        result.primitives.push_back(
            {"spline.out." + point.id.value(), "sphere", outgoing, {}, {}, {0.35, 0.9, 0.45, 1.0}, style.handleSize});
    }
    auto path = document.compilePath();
    if (!path.ok()) {
        if (document.points().size() >= 2)
            return failedSnapshot(document, "editor.spline.gizmo-incomplete", path.status().describe());
        result.diagnostics.push_back(editing::ruleDiagnostic(
            DiagnosticCode::PreconditionViolation, RuleId("editor.spline.gizmo-incomplete"),
            DiagnosticSeverity::Warning, "Add at least two points to preview the spline curve"));
        result.status = EditorStatus::Applied;
        return result;
    }
    auto polyline = path.value().polylineResult(sampleCount, false);
    if (!polyline.ok()) return failedSnapshot(document, "editor.spline.gizmo-evaluate", polyline.status().describe());
    for (int chunk = 0; chunk < polyline.value().chunkCount(); ++chunk) {
        auto previous = polyline.value().chunkPointResult(chunk, 0);
        if (!previous.ok())
            return failedSnapshot(document, "editor.spline.gizmo-evaluate", previous.status().describe());
        for (int sample = 1; sample < polyline.value().chunkPointCount(chunk); ++sample) {
            auto current = polyline.value().chunkPointResult(chunk, sample);
            if (!current.ok())
                return failedSnapshot(document, "editor.spline.gizmo-evaluate", current.status().describe());
            addLine(result, "spline.chunk." + std::to_string(chunk) + ".segment." + std::to_string(sample - 1),
                    {previous.value().x, previous.value().y, previous.value().z},
                    {current.value().x, current.value().y, current.value().z}, style.lineColor);
            previous = std::move(current);
        }
    }
    result.status = EditorStatus::Applied;
    return result;
}

SplinePathDragSession::SplinePathDragSession(SplinePathDocument* document, int sampleCount)
    : document_(document), sampleCount_(sampleCount) {}

Result<SplinePathDragPreview> SplinePathDragSession::previewCurrent() const {
    if (!document_ || !dragging_)
        return eve::editing::failed<SplinePathDragPreview>(
            EditorStatus::Rejected, RuleId("editor.spline.drag-not-active"), "Spline drag has not begun");
    SplinePathDocument candidate = *document_;
    auto               operation = candidate.makeSetPoint(draft_);
    if (!operation.ok() || !candidate.applyDomainOperation(operation.value()).ok())
        return eve::editing::failed<SplinePathDragPreview>(EditorStatus::Rejected,
                                                           RuleId("editor.spline.drag-preview-invalid"),
                                                           "Spline draft could not be previewed");
    SplinePathDragPreview result;
    result.status = EditorStatus::Applied;
    result.point  = activePoint_;
    result.handle = activeHandle_;
    result.draft  = draft_;
    result.gizmo  = SplinePathGizmoBuilder().build(candidate, sampleCount_);
    if (result.gizmo.status != EditorStatus::Applied) result.status = result.gizmo.status;
    return eve::editing::applied<SplinePathDragPreview>(std::move(result));
}

Result<SplinePathDragPreview> SplinePathDragSession::beginDrag(double rayOriginX, double rayOriginY, double rayOriginZ,
                                                               double rayDirectionX, double rayDirectionY,
                                                               double rayDirectionZ) {
    cancelDrag();
    if (!document_)
        return eve::editing::failed<SplinePathDragPreview>(
            EditorStatus::Rejected, RuleId("editor.spline.drag-target-required"), "Spline drag target is unavailable");
    std::array<double, 3> direction;
    if (!normalizeRay(rayDirectionX, rayDirectionY, rayDirectionZ, direction))
        return eve::editing::failed<SplinePathDragPreview>(EditorStatus::Rejected,
                                                           RuleId("editor.spline.drag-ray-invalid"),
                                                           "Spline drag requires a finite non-zero pointer ray");
    const std::array<double, 3> origin{rayOriginX, rayOriginY, rayOriginZ};
    const auto                  gizmo = SplinePathGizmoBuilder().build(*document_, sampleCount_);
    if (gizmo.status != EditorStatus::Applied)
        return eve::editing::failed<SplinePathDragPreview>(EditorStatus::Rejected,
                                                           RuleId("editor.spline.drag-gizmo-invalid"),
                                                           "Spline gizmo is not available for picking");
    double      closest = std::numeric_limits<double>::infinity();
    std::string pickedId;
    for (const auto& primitive : gizmo.primitives) {
        if (primitive.kind != "sphere" || primitive.id.rfind("spline.", 0) != 0) continue;
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
        return eve::editing::failed<SplinePathDragPreview>(
            EditorStatus::NotFound, RuleId("editor.spline.handle-not-hit"), "Pointer ray did not hit a spline handle");
    const auto roleEnd = pickedId.find('.', 7);
    activeHandle_      = pickedId.substr(7, roleEnd - 7);
    activePoint_       = StableId(pickedId.substr(roleEnd + 1));
    const auto points  = document_->points();
    const auto found =
        std::find_if(points.begin(), points.end(), [&](const auto& point) { return point.id == activePoint_; });
    if (found == points.end())
        return eve::editing::failed<SplinePathDragPreview>(
            EditorStatus::NotFound, RuleId("editor.spline.point-not-found"), "Picked spline point no longer exists");
    draft_           = *found;
    dragPlaneNormal_ = direction;
    baseRevision_    = document_->revision();
    dragging_        = true;
    return previewCurrent();
}

Result<SplinePathDragPreview> SplinePathDragSession::updateDrag(double rayOriginX, double rayOriginY, double rayOriginZ,
                                                                double rayDirectionX, double rayDirectionY,
                                                                double rayDirectionZ) {
    if (!dragging_ || !document_)
        return eve::editing::failed<SplinePathDragPreview>(
            EditorStatus::Rejected, RuleId("editor.spline.drag-not-active"), "Spline drag has not begun");
    if (document_->revision() != baseRevision_) {
        cancelDrag();
        return eve::editing::failed<SplinePathDragPreview>(EditorStatus::Conflict, RuleId("editor.spline.drag-stale"),
                                                           "Spline document changed while dragging");
    }
    std::array<double, 3> direction;
    if (!normalizeRay(rayDirectionX, rayDirectionY, rayDirectionZ, direction))
        return eve::editing::failed<SplinePathDragPreview>(EditorStatus::Rejected,
                                                           RuleId("editor.spline.drag-ray-invalid"),
                                                           "Spline drag requires a finite non-zero pointer ray");
    const std::array<double, 3> origin{rayOriginX, rayOriginY, rayOriginZ};
    const double                denominator =
        direction[0] * dragPlaneNormal_[0] + direction[1] * dragPlaneNormal_[1] + direction[2] * dragPlaneNormal_[2];
    if (std::abs(denominator) < 1e-9)
        return eve::editing::failed<SplinePathDragPreview>(EditorStatus::Rejected,
                                                           RuleId("editor.spline.drag-ray-parallel"),
                                                           "Pointer ray is parallel to the spline drag plane");
    const double distance = ((dragPlanePoint_[0] - origin[0]) * dragPlaneNormal_[0] +
                             (dragPlanePoint_[1] - origin[1]) * dragPlaneNormal_[1] +
                             (dragPlanePoint_[2] - origin[2]) * dragPlaneNormal_[2]) /
                            denominator;
    if (distance < 0.0)
        return eve::editing::failed<SplinePathDragPreview>(EditorStatus::Rejected,
                                                           RuleId("editor.spline.drag-behind-camera"),
                                                           "Spline drag plane is behind the pointer ray origin");
    const std::array<double, 3> world{origin[0] + direction[0] * distance, origin[1] + direction[1] * distance,
                                      origin[2] + direction[2] * distance};
    if (activeHandle_ == "anchor") {
        draft_.x = world[0];
        draft_.y = world[1];
        draft_.z = world[2];
    } else if (activeHandle_ == "in") {
        draft_.inX = world[0] - draft_.x;
        draft_.inY = world[1] - draft_.y;
        draft_.inZ = world[2] - draft_.z;
    } else {
        draft_.outX = world[0] - draft_.x;
        draft_.outY = world[1] - draft_.y;
        draft_.outZ = world[2] - draft_.z;
    }
    return previewCurrent();
}

Result<DomainOperation> SplinePathDragSession::finishDrag() {
    if (!dragging_ || !document_)
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.spline.drag-not-active"),
                                                     "Spline drag has not begun");
    if (document_->revision() != baseRevision_) {
        cancelDrag();
        return eve::editing::failed<DomainOperation>(EditorStatus::Conflict, RuleId("editor.spline.drag-stale"),
                                                     "Spline document changed before drag commit");
    }
    auto operation = document_->makeSetPoint(draft_);
    if (operation.ok()) cancelDrag();
    return operation;
}

void SplinePathDragSession::cancelDrag() {
    dragging_    = false;
    activePoint_ = {};
    activeHandle_.clear();
}

}  // namespace eve::procgen_editing
