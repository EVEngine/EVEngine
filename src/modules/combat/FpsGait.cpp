#include "combat/FpsGait.h"

#include "common/Diagnostic.h"

#include <cmath>

namespace eve::combat {
namespace {

[[nodiscard]] bool finitePositive(double v) { return std::isfinite(v) && v > 0.0; }

}  // namespace

Result<void> FpsGaitProfile::validate() const {
    if (!finitePositive(walkSpeed) || !finitePositive(sprintSpeed) || !finitePositive(crouchSpeed))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "fps gait speeds must be finite and positive", "speed"));
    if (!finitePositive(standHeight) || !finitePositive(crouchHeight) || crouchHeight > standHeight)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "fps gait capsule heights must be positive with crouch <= stand",
                                                       "height"));
    if (!finitePositive(standEyeHeight) || !finitePositive(crouchEyeHeight) || crouchEyeHeight > standEyeHeight)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "fps gait eye heights must be positive with crouch <= stand",
                                                       "eye"));
    if (!finitePositive(capsuleRadius))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "fps gait capsule radius must be finite and positive", "radius"));
    if (standEyeHeight > standHeight || crouchEyeHeight > crouchHeight)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "fps gait eye height must not exceed capsule height", "eye"));
    return Result<void>::success();
}

Result<void> FpsGait::setProfile(const FpsGaitProfile& profile) {
    auto valid = profile.validate();
    if (!valid) return valid;
    profile_ = profile;
    return Result<void>::success();
}

Result<FpsGaitPose> FpsGait::evaluate(const FpsGaitInput& input) {
    auto valid = profile_.validate();
    if (!valid) return Result<FpsGaitPose>::failure(valid.status());

    FpsGaitMode next = FpsGaitMode::Walk;
    if (input.wantCrouch || (mode_ == FpsGaitMode::Crouch && !input.canStand))
        next = FpsGaitMode::Crouch;
    else if (input.wantSprint && input.grounded)
        next = FpsGaitMode::Sprint;

    mode_ = next;

    FpsGaitPose pose;
    pose.mode          = mode_;
    pose.capsuleRadius = profile_.capsuleRadius;
    switch (mode_) {
        case FpsGaitMode::Sprint:
            pose.moveSpeed     = profile_.sprintSpeed;
            pose.capsuleHeight = profile_.standHeight;
            pose.eyeHeight     = profile_.standEyeHeight;
            break;
        case FpsGaitMode::Crouch:
            pose.moveSpeed     = profile_.crouchSpeed;
            pose.capsuleHeight = profile_.crouchHeight;
            pose.eyeHeight     = profile_.crouchEyeHeight;
            break;
        case FpsGaitMode::Walk:
        default:
            pose.moveSpeed     = profile_.walkSpeed;
            pose.capsuleHeight = profile_.standHeight;
            pose.eyeHeight     = profile_.standEyeHeight;
            break;
    }
    return Result<FpsGaitPose>::success(pose);
}

Result<FpsHeadBobOffset> FpsHeadBob::solve(const FpsHeadBobRequest& request) {
    if (!std::isfinite(request.stridePhase) || !std::isfinite(request.moveSpeed) ||
        !std::isfinite(request.referenceSpeed) || !std::isfinite(request.amplitude) ||
        !std::isfinite(request.landDip))
        return Result<FpsHeadBobOffset>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "fps head bob request must be finite", "request"));
    if (request.referenceSpeed <= 0.0)
        return Result<FpsHeadBobOffset>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "fps head bob reference speed must be positive", "referenceSpeed"));
    if (request.amplitude < 0.0)
        return Result<FpsHeadBobOffset>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "fps head bob amplitude must be non-negative", "amplitude"));

    FpsHeadBobOffset offset;
    offset.eyeY = request.landDip;
    if (!request.enabled || !request.grounded) return Result<FpsHeadBobOffset>::success(offset);

    const double speed = std::max(0.0, request.moveSpeed);
    const double frac  = std::min(1.0, speed / request.referenceSpeed);
    if (frac <= 1e-4) return Result<FpsHeadBobOffset>::success(offset);

    offset.eyeY += std::sin(request.stridePhase) * request.amplitude * frac;
    return Result<FpsHeadBobOffset>::success(offset);
}

}  // namespace eve::combat
