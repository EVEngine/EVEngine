#include "animation/RootMotionPolicy.h"

#include "animation/AnimPose.h"

#include <cmath>
#include <cstdint>

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
    if (hasLock(policy.lockAxes, RootMotionLockAxes::X)) delta.px = 0.f;
    if (hasLock(policy.lockAxes, RootMotionLockAxes::Y)) delta.py = 0.f;
    if (hasLock(policy.lockAxes, RootMotionLockAxes::Z)) delta.pz = 0.f;
    if (policy.lockRotation) {
        delta.qx = delta.qy = delta.qz = 0.f;
        delta.qw                        = 1.f;
    }
    if (policy.applySpace == RootMotionApplySpace::CharacterFacing && std::isfinite(policy.characterYaw))
        delta = rotatePlanarByYaw(delta, policy.characterYaw);
    return delta;
}

eve::Result<void> bakeRootMotionIntoPose(AnimPose& pose, int boneIndex, const TransformTRS& previousRoot,
                                         const RootMotionPolicy& policy) {
    if (boneIndex < 0 || boneIndex >= pose.getBoneCount())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "bake root motion bone index out of range"));
    if (!policy.bakeTranslationIntoPose && !policy.bakeRotationIntoPose)
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));

    TransformTRS& root = pose.local(boneIndex);
    bool          changed = false;
    if (policy.bakeTranslationIntoPose) {
        // Locked axes stay in the pose (e.g. vertical bob); unlocked axes were
        // published to the controller and must not also slide the mesh.
        if (!hasLock(policy.lockAxes, RootMotionLockAxes::X) && root.px != previousRoot.px) {
            root.px = previousRoot.px;
            changed = true;
        }
        if (!hasLock(policy.lockAxes, RootMotionLockAxes::Y) && root.py != previousRoot.py) {
            root.py = previousRoot.py;
            changed = true;
        }
        if (!hasLock(policy.lockAxes, RootMotionLockAxes::Z) && root.pz != previousRoot.pz) {
            root.pz = previousRoot.pz;
            changed = true;
        }
    }
    if (policy.bakeRotationIntoPose && !policy.lockRotation) {
        root.qx = previousRoot.qx;
        root.qy = previousRoot.qy;
        root.qz = previousRoot.qz;
        root.qw = previousRoot.qw;
        root.normalizeRotation();
        changed = true;
    }
    return eve::Result<void>::success(
        eve::Status::success(changed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

}  // namespace eve::animation
