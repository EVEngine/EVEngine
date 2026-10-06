#include "combat/CombatMotionWarp.h"

#include <cmath>

namespace eve::combat {
namespace {

Result<void> invalid(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

bool finite3(CombatVector3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

double lengthXZ(CombatVector3 value) { return std::hypot(value.x, value.z); }

CombatVector3 normalizedXZ(CombatVector3 value) {
    const double magnitude = lengthXZ(value);
    if (magnitude <= 0.0) return {};
    return {value.x / magnitude, 0.0, value.z / magnitude};
}

}  // namespace

Result<void> CombatWarpRequest::validate() const {
    if (!finite3(attacker) || !finite3(target) || !finite3(attackerFacing))
        return invalid("warp poses are invalid", "pose");
    if (!std::isfinite(desiredDistance) || desiredDistance < 0.0)
        return invalid("desired warp distance is invalid", "desiredDistance");
    if (!std::isfinite(maxTranslation) || maxTranslation < 0.0)
        return invalid("max warp translation is invalid", "maxTranslation");
    if (!std::isfinite(remainingBudget) || remainingBudget < 0.0)
        return invalid("warp budget is invalid", "remainingBudget");
    return Result<void>::success();
}

Result<CombatWarpResult> CombatMotionWarp::solve(const CombatWarpRequest& request) {
    auto valid = request.validate();
    if (!valid) return Result<CombatWarpResult>::failure(valid.status());

    CombatWarpResult result;
    result.facing = request.attackerFacing;
    CombatVector3 delta{request.target.x - request.attacker.x, 0.0, request.target.z - request.attacker.z};
    const double  distance = lengthXZ(delta);
    if (request.facing && distance > 0.0) result.facing = normalizedXZ(delta);
    if (!request.horizontal || distance <= 0.0)
        return Result<CombatWarpResult>::success(result, Status::success(StatusCode::NoOp));

    const double error = distance - request.desiredDistance;
    if (std::abs(error) <= 1e-6) return Result<CombatWarpResult>::success(result, Status::success(StatusCode::NoOp));

    CombatVector3 direction = normalizedXZ(delta);
    if (error < 0.0) direction = {-direction.x, 0.0, -direction.z};
    double step = std::min(std::abs(error), request.maxTranslation);
    if (step > request.remainingBudget) {
        result.budgetExceeded = true;
        return Result<CombatWarpResult>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "motion warp would exceed remaining budget", "budget"));
    }
    result.translation    = {direction.x * step, 0.0, direction.z * step};
    result.budgetConsumed = step;
    return Result<CombatWarpResult>::success(result, Status::success(StatusCode::Applied));
}

}  // namespace eve::combat
