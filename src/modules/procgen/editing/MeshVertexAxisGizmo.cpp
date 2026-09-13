#include "procgen/editing/MeshVertexAxisGizmo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace eve::procgen_editing {

editing::GizmoSnapshot MeshVertexAxisGizmoBuilder::build(const procgen::MeshDeformationSession& session,
                                                         double                                 axisLength) const {
    editing::GizmoSnapshot result;
    result.target         = "procgen.meshVertexSelection";
    result.targetRevision = session.revision();
    auto center           = session.selectedVertexCenterResult();
    if (!center.ok() || !std::isfinite(axisLength) || axisLength <= 0.0) {
        result.diagnostics.push_back(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "axis gizmo requires a selected vertex and positive length",
                                                       "selection", {}, "procgen_editing.meshVertexAxisGizmo"));
        return result;
    }
    const std::array<double, 3> position{center.value().x, center.value().y, center.value().z};
    result.primitives.push_back(
        {"mesh.axis.x", "arrow", position, {}, {1.0, 0.0, 0.0}, {1.0, 0.16, 0.12, 1.0}, 0.0, axisLength});
    result.primitives.push_back(
        {"mesh.axis.y", "arrow", position, {}, {0.0, 1.0, 0.0}, {0.18, 1.0, 0.28, 1.0}, 0.0, axisLength});
    result.primitives.push_back(
        {"mesh.axis.z", "arrow", position, {}, {0.0, 0.0, 1.0}, {0.18, 0.48, 1.0, 1.0}, 0.0, axisLength});
    result.status = editing::Status::Applied;
    return result;
}

Result<std::string> MeshVertexAxisGizmoBuilder::pickAxisResult(const editing::GizmoSnapshot& snapshot, double originX,
                                                               double originY, double originZ, double directionX,
                                                               double directionY, double directionZ) const {
    const double rayLength = std::sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ);
    if (!std::isfinite(rayLength) || rayLength <= 1e-9)
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "axis pick requires a finite non-zero ray", "ray", {},
                                                              "procgen_editing.meshVertexAxisGizmo"));
    const std::array<double, 3> ray{directionX / rayLength, directionY / rayLength, directionZ / rayLength};
    double                      bestDistance = std::numeric_limits<double>::infinity();
    std::string                 best;
    for (const auto& primitive : snapshot.primitives) {
        if (primitive.kind != "arrow" || primitive.id.rfind("mesh.axis.", 0) != 0) continue;
        const std::array<double, 3> a = primitive.position;
        const std::array<double, 3> b{a[0] + primitive.direction[0] * primitive.length,
                                      a[1] + primitive.direction[1] * primitive.length,
                                      a[2] + primitive.direction[2] * primitive.length};
        const std::array<double, 3> segment{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const std::array<double, 3> offset{originX - a[0], originY - a[1], originZ - a[2]};
        const double                rr = 1.0;
        const double                ss = segment[0] * segment[0] + segment[1] * segment[1] + segment[2] * segment[2];
        const double                rs = ray[0] * segment[0] + ray[1] * segment[1] + ray[2] * segment[2];
        const double                ro = ray[0] * offset[0] + ray[1] * offset[1] + ray[2] * offset[2];
        const double                so = segment[0] * offset[0] + segment[1] * offset[1] + segment[2] * offset[2];
        const double                denominator = rr * ss - rs * rs;
        double                      rayT        = denominator > 1e-12 ? (rs * so - ss * ro) / denominator : -ro;
        double                      segmentT    = denominator > 1e-12 ? (rr * so - rs * ro) / denominator : 0.0;
        rayT                                    = std::max(0.0, rayT);
        segmentT                                = std::clamp(segmentT, 0.0, 1.0);
        const double dx                         = originX + ray[0] * rayT - (a[0] + segment[0] * segmentT);
        const double dy                         = originY + ray[1] * rayT - (a[1] + segment[1] * segmentT);
        const double dz                         = originZ + ray[2] * rayT - (a[2] + segment[2] * segmentT);
        const double distance                   = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance <= primitive.length * 0.12 && rayT < bestDistance) {
            bestDistance = rayT;
            best         = primitive.id.substr(10);
        }
    }
    if (best.empty())
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::NotFound, "ray did not hit an axis",
                                                              "ray", {}, "procgen_editing.meshVertexAxisGizmo"));
    return Result<std::string>::success(std::move(best));
}

}  // namespace eve::procgen_editing
