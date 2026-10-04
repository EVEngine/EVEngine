#include "animation/RootMotionPolicy.h"

#include "animation/AnimPose.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace eve::animation {
namespace {

bool isKnownApplySpace(RootMotionApplySpace space) noexcept {
    return space == RootMotionApplySpace::BoneLocal || space == RootMotionApplySpace::CharacterFacing;
}

bool isKnownLockMask(RootMotionLockAxes mask) noexcept {
    constexpr std::uint8_t kAll = static_cast<std::uint8_t>(RootMotionLockAxes::All);
    return (static_cast<std::uint8_t>(mask) & static_cast<std::uint8_t>(~kAll)) == 0;
}

TransformTRS sanitize(TransformTRS delta) {
    if (!std::isfinite(delta.px)) delta.px = 0.f;
    if (!std::isfinite(delta.py)) delta.py = 0.f;
    if (!std::isfinite(delta.pz)) delta.pz = 0.f;
    if (!std::isfinite(delta.qx) || !std::isfinite(delta.qy) || !std::isfinite(delta.qz) ||
        !std::isfinite(delta.qw)) {
        delta.qx = delta.qy = delta.qz = 0.f;
        delta.qw                        = 1.f;
    } else {
        delta.normalizeRotation();
    }
    return delta;
}

TransformTRS rotatePlanarByYaw(TransformTRS delta, float yaw) {
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    const float x = delta.px;
    const float z = delta.pz;
    // Ry(yaw) * (x,0,z) with forward (sin(yaw), 0, cos(yaw)) for +Z local.
    delta.px = x * c + z * s;
    delta.pz = -x * s + z * c;
    return delta;
}

}  // namespace

eve::Result<void> validateRootMotionPolicy(const RootMotionPolicy& policy) {
    if (!std::isfinite(policy.characterYaw))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "root motion policy characterYaw must be finite"));
    if (!isKnownApplySpace(policy.applySpace))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "root motion policy applySpace is unknown"));
    if (!isKnownLockMask(policy.lockAxes))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "root motion policy lockAxes has unknown bits"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

TransformTRS applyRootMotionPolicy(const TransformTRS& rawDelta, const RootMotionPolicy& policy) {
    TransformTRS delta = sanitize(rawDelta);
    if (policy.lockRotation) {
        delta.qx = delta.qy = delta.qz = 0.f;
        delta.qw                        = 1.f;
    }
    if (policy.applySpace == RootMotionApplySpace::CharacterFacing && std::isfinite(policy.characterYaw))
        delta = rotatePlanarByYaw(delta, policy.characterYaw);
    if (hasLock(policy.lockAxes, RootMotionLockAxes::X)) delta.px = 0.f;
    if (hasLock(policy.lockAxes, RootMotionLockAxes::Y)) delta.py = 0.f;
    if (hasLock(policy.lockAxes, RootMotionLockAxes::Z)) delta.pz = 0.f;
    return delta;
}

eve::Result<void> bakeRootMotionIntoPose(AnimPose& pose, int boneIndex, const TransformTRS& referenceRoot,
                                         const RootMotionPolicy& policy) {
    if (boneIndex < 0 || boneIndex >= pose.getBoneCount())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "bake root motion bone index out of range"));
    if (!policy.bakeTranslationIntoPose && !policy.bakeRotationIntoPose)
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));

    TransformTRS& root = pose.local(boneIndex);
    bool          changed = false;
    if (policy.bakeTranslationIntoPose) {
        // Locked pose-local axes stay animated (e.g. vertical bob); unlocked axes
        // plant on the stable reference so the capsule owns locomotion.
        if (!hasLock(policy.lockAxes, RootMotionLockAxes::X) && root.px != referenceRoot.px) {
            root.px = referenceRoot.px;
            changed = true;
        }
        if (!hasLock(policy.lockAxes, RootMotionLockAxes::Y) && root.py != referenceRoot.py) {
            root.py = referenceRoot.py;
            changed = true;
        }
        if (!hasLock(policy.lockAxes, RootMotionLockAxes::Z) && root.pz != referenceRoot.pz) {
            root.pz = referenceRoot.pz;
            changed = true;
        }
    }
    if (policy.bakeRotationIntoPose && !policy.lockRotation) {
        root.qx = referenceRoot.qx;
        root.qy = referenceRoot.qy;
        root.qz = referenceRoot.qz;
        root.qw = referenceRoot.qw;
        root.normalizeRotation();
        changed = true;
    }
    return eve::Result<void>::success(
        eve::Status::success(changed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

eve::Result<RootMotionLockAxes> parseRootMotionLockAxes(std::string_view name) {
    if (name == "none" || name.empty())
        return eve::Result<RootMotionLockAxes>::success(RootMotionLockAxes::None);
    if (name == "x") return eve::Result<RootMotionLockAxes>::success(RootMotionLockAxes::X);
    if (name == "y" || name == "vertical")
        return eve::Result<RootMotionLockAxes>::success(RootMotionLockAxes::VerticalY);
    if (name == "z") return eve::Result<RootMotionLockAxes>::success(RootMotionLockAxes::Z);
    if (name == "xz" || name == "horizontal")
        return eve::Result<RootMotionLockAxes>::success(RootMotionLockAxes::HorizontalXZ);
    if (name == "xy")
        return eve::Result<RootMotionLockAxes>::success(RootMotionLockAxes::X | RootMotionLockAxes::Y);
    if (name == "yz")
        return eve::Result<RootMotionLockAxes>::success(RootMotionLockAxes::Y | RootMotionLockAxes::Z);
    if (name == "xyz" || name == "all")
        return eve::Result<RootMotionLockAxes>::success(RootMotionLockAxes::All);
    return eve::Result<RootMotionLockAxes>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                               "unknown root motion lockAxes: " + std::string(name)));
}

eve::Result<RootMotionApplySpace> parseRootMotionApplySpace(std::string_view name) {
    if (name == "boneLocal" || name == "local")
        return eve::Result<RootMotionApplySpace>::success(RootMotionApplySpace::BoneLocal);
    if (name == "characterFacing" || name == "facing")
        return eve::Result<RootMotionApplySpace>::success(RootMotionApplySpace::CharacterFacing);
    return eve::Result<RootMotionApplySpace>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                               "unknown root motion applySpace: " + std::string(name)));
}

}  // namespace eve::animation
