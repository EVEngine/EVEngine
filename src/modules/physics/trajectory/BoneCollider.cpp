#include "physics/trajectory/BoneCollider.h"

#include <cmath>

namespace eve::physics::trajectory {

Result<void> BoneColliderDefinition::validate() const {
    if (colliderId.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "bone collider id is empty", "colliderId"));
    if (boneName.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "bone collider bone name is empty", "boneName"));
    if (!std::isfinite(radius) || radius < 0.f)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "bone collider radius is invalid", "radius"));
    if (!std::isfinite(localOffset.x) || !std::isfinite(localOffset.y) || !std::isfinite(localOffset.z))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "bone collider local offset is invalid", "localOffset"));
    if (!std::isfinite(endLocalOffset.x) || !std::isfinite(endLocalOffset.y) ||
        !std::isfinite(endLocalOffset.z))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "bone collider end local offset is invalid", "endLocalOffset"));
    if (maxHits < 1)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "bone collider maxHits must be >= 1", "maxHits"));
    if (ignoredBodyId < -1)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "bone collider ignoredBodyId is invalid", "ignoredBodyId"));

    if (kind == BoneColliderShapeKind::Sphere) {
        if (!endBoneName.empty())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "sphere collider cannot declare an end bone", "endBoneName"));
        if (halfHeight != 0.f)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "sphere collider cannot declare halfHeight", "halfHeight"));
        return Result<void>::success();
    }

    if (kind != BoneColliderShapeKind::Capsule)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "bone collider shape kind is invalid", "kind"));

    if (endBoneName.empty()) {
        if (!std::isfinite(halfHeight) || halfHeight < 0.f)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "capsule halfHeight is invalid", "halfHeight"));
    } else if (halfHeight != 0.f) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "dual-socket capsule cannot declare halfHeight", "halfHeight"));
    }
    return Result<void>::success();
}

}  // namespace eve::physics::trajectory
