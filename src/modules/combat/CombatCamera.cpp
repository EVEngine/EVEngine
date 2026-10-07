#include "combat/CombatCamera.h"

#include <cmath>

namespace eve::combat {
namespace {

bool finite3(CombatVector3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

double lengthXZ(CombatVector3 value) { return std::hypot(value.x, value.z); }

CombatVector3 normalizedXZ(CombatVector3 value) {
    const double magnitude = lengthXZ(value);
    if (magnitude <= 0.0) return {0.0, 0.0, 1.0};
    return {value.x / magnitude, 0.0, value.z / magnitude};
}

}  // namespace

Result<void> CombatCameraFramingRequest::validate() const {
    if (!finite3(player) || !finite3(playerFacing)) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("camera player pose is invalid"), std::move("player")));
    if (lockTarget && !finite3(*lockTarget)) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("camera lock target is invalid"), std::move("lockTarget")));
    if (!std::isfinite(distance) || distance <= 0.0) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("camera distance is invalid"), std::move("distance")));
    if (!std::isfinite(height) || !std::isfinite(lookHeight)) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("camera height is invalid"), std::move("height")));
    return Result<void>::success();
}

Result<CombatCameraView> CombatCameraFraming::solve(const CombatCameraFramingRequest& request) {
    auto valid = request.validate();
    if (!valid) return Result<CombatCameraView>::failure(valid.status());

    CombatVector3 back = normalizedXZ({-request.playerFacing.x, 0.0, -request.playerFacing.z});
    CombatVector3 look = {request.player.x, request.player.y + request.lookHeight, request.player.z};
    if (request.lockTarget) {
        look = {(request.player.x + request.lockTarget->x) * 0.5,
                (request.player.y + request.lockTarget->y) * 0.5 + request.lookHeight,
                (request.player.z + request.lockTarget->z) * 0.5};
        CombatVector3 toTarget{request.lockTarget->x - request.player.x, 0.0, request.lockTarget->z - request.player.z};
        if (lengthXZ(toTarget) > 1e-6) back = normalizedXZ({-toTarget.x, 0.0, -toTarget.z});
    }
    CombatCameraView view;
    view.lookAt = look;
    view.eye    = {request.player.x + back.x * request.distance, request.player.y + request.height,
                   request.player.z + back.z * request.distance};
    return Result<CombatCameraView>::success(view);
}

}  // namespace eve::combat
