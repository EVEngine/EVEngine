#include "graphics/editing/VegetationFieldGizmo.h"

#include "graphics/VegetationField.h"

#include <algorithm>
#include <new>

namespace eve::graphics_editing {
namespace {

Result<editing::GizmoSnapshot> failure(StatusCode code, DiagnosticCode diagnostic, std::string message,
                                       std::string path = {}) {
    return Result<editing::GizmoSnapshot>::failure(
        Status(code, {Diagnostic::error(diagnostic, std::move(message), std::move(path), {},
                                        "graphics_editing.vegetation_field_gizmo")}));
}

std::array<double, 4> channelColor(graphics::VegetationChannel channel, float opacity) {
    const double alpha = std::clamp(double(opacity), 0.2, 1.0);
    switch (channel) {
        case graphics::VegetationChannel::Color: return {0.2, 0.85, 0.35, alpha};
        case graphics::VegetationChannel::Extras: return {1.0, 0.55, 0.1, alpha};
        case graphics::VegetationChannel::Motion: return {0.2, 0.65, 1.0, alpha};
        case graphics::VegetationChannel::Vertex: return {0.75, 0.35, 1.0, alpha};
    }
    return {1.0, 1.0, 1.0, alpha};
}

}  // namespace

Result<editing::GizmoSnapshot> VegetationFieldGizmoBuilder::build(
    std::string target, std::uint64_t expectedRevision, const graphics::VegetationField& field,
    std::size_t maximumElements) const {
    if (target.empty() || maximumElements == 0 || maximumElements > 100000)
        return failure(StatusCode::Rejected, DiagnosticCode::InvalidArgument,
                       "Vegetation field gizmo requires a target and a bounded element budget", "request");
    if (field.revision() != expectedRevision)
        return failure(StatusCode::Conflict, DiagnosticCode::StaleHandle,
                       "Vegetation field changed before gizmo generation", "revision");
    auto elements = field.snapshotElements();
    if (!elements) return Result<editing::GizmoSnapshot>::failure(elements.status());
    if (field.revision() != expectedRevision)
        return failure(StatusCode::Conflict, DiagnosticCode::StaleHandle,
                       "Vegetation field changed while gizmo generation was reading it", "revision");
    if (elements.value().size() > maximumElements)
        return failure(StatusCode::Rejected, DiagnosticCode::PreconditionViolation,
                       "Vegetation field exceeds the gizmo element budget", "elements");
    try {
        editing::GizmoSnapshot result;
        result.status         = StatusCode::Applied;
        result.target         = std::move(target);
        result.targetRevision = expectedRevision;
        result.primitives.reserve(elements.value().size());
        for (std::size_t i = 0; i < elements.value().size(); ++i) {
            const auto& element = elements.value()[i];
            editing::GizmoPrimitive primitive;
            primitive.id       = "vegetation-field-" + std::to_string(i);
            primitive.kind     = element.shape == graphics::VegetationShape::Box ? "obb" : "ellipsoid";
            primitive.position = {element.center.x, element.center.y, element.center.z};
            primitive.size     = {element.extents.x * 2.0, element.extents.y * 2.0, element.extents.z * 2.0};
            primitive.color    = channelColor(element.channel, element.opacity);
            primitive.yaw      = element.yaw;
            primitive.dashed   = !element.mask.pixels.empty();
            result.primitives.push_back(std::move(primitive));
        }
        return Result<editing::GizmoSnapshot>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return failure(StatusCode::Failed, DiagnosticCode::Failed,
                       "Vegetation field gizmo allocation failed");
    }
}

}  // namespace eve::graphics_editing
