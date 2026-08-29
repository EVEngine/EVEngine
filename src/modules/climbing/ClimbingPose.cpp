#include "climbing/ClimbingPose.h"

#include "animation/AnimConstraintStack.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include <algorithm>
#include <cmath>

namespace eve::climbing {
namespace {

eve::Result<ClimbingPoseResult> poseFailure(std::string message, std::string path) {
    return eve::Result<ClimbingPoseResult>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path), {}, "climbing"));
}

bool validChain(const ClimbingLimbChain& chain, int boneCount) {
    return chain.root >= 0 && chain.mid >= 0 && chain.tip >= 0 && chain.root < boneCount && chain.mid < boneCount &&
           chain.tip < boneCount;
}

Vec3 toModelSpace(Vec3 world, const ClimbingPoseBinding& binding) {
    const float x      = world.x - binding.modelWorldOrigin.x;
    const float y      = world.y - binding.modelWorldOrigin.y;
    const float z      = world.z - binding.modelWorldOrigin.z;
    const float cosine = std::cos(binding.modelYawRadians);
    const float sine   = std::sin(binding.modelYawRadians);
    return {cosine * x - sine * z, y, sine * x + cosine * z};
}

}  // namespace

eve::Result<ClimbingPoseResult> applyClimbingPose(animation::AnimSkeleton& skeleton, animation::AnimPose& pose,
                                                  const ClimbingAdvance& advance, const ClimbingPoseBinding& binding) {
    if (!std::isfinite(binding.modelYawRadians))
        return poseFailure("model yaw must be finite", "binding.modelYawRadians");
    const int boneCount = skeleton.getBoneCount();
    if (binding.enableLeftHand && !validChain(binding.leftArm, boneCount))
        return poseFailure("left arm chain contains an invalid bone", "binding.leftArm");
    if (binding.enableRightHand && !validChain(binding.rightArm, boneCount))
        return poseFailure("right arm chain contains an invalid bone", "binding.rightArm");

    ClimbingPoseResult result;
    const float legacyWeight = std::clamp(advance.contactWeight, 0.f, 1.f);
    const float authoredLeft = std::clamp(advance.leftHandWeight, 0.f, 1.f);
    const float authoredRight = std::clamp(advance.rightHandWeight, 0.f, 1.f);
    const bool  hasPerHandWeights = authoredLeft > 0.f || authoredRight > 0.f;
    const float leftWeight        = hasPerHandWeights ? authoredLeft : legacyWeight;
    const float rightWeight       = hasPerHandWeights ? authoredRight : legacyWeight;
    result.weight                 = std::max(leftWeight, rightWeight);
    if (result.weight <= 0.f)
        return eve::Result<ClimbingPoseResult>::success(std::move(result), eve::Status::success(eve::StatusCode::NoOp));

    animation::AnimConstraintStack constraints(&skeleton);
    if (binding.enableLeftHand && leftWeight > 0.f) {
        const Vec3 target = toModelSpace(advance.leftHandAnchor, binding);
        constraints.addTwoBoneIK(binding.leftArm.root, binding.leftArm.mid, binding.leftArm.tip, target.x, target.y,
                                 target.z, leftWeight);
        result.leftHandApplied = true;
    }
    if (binding.enableRightHand && rightWeight > 0.f) {
        const Vec3 target = toModelSpace(advance.rightHandAnchor, binding);
        constraints.addTwoBoneIK(binding.rightArm.root, binding.rightArm.mid, binding.rightArm.tip, target.x, target.y,
                                 target.z, rightWeight);
        result.rightHandApplied = true;
    }
    constraints.apply(&pose);
    return eve::Result<ClimbingPoseResult>::success(std::move(result), eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::climbing
