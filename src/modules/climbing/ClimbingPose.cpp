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

bool validChain(const ClimbingLimbChain& chain, const animation::AnimSkeleton& skeleton) {
    const int boneCount = skeleton.getBoneCount();
    return chain.root >= 0 && chain.mid >= 0 && chain.tip >= 0 && chain.root < boneCount && chain.mid < boneCount &&
           chain.tip < boneCount && skeleton.getParent(chain.mid) == chain.root &&
           skeleton.getParent(chain.tip) == chain.mid;
}

bool finite(Vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

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
    if (!finite(binding.modelWorldOrigin))
        return poseFailure("model origin must be finite", "binding.modelWorldOrigin");
    if (pose.getBoneCount() != skeleton.getBoneCount())
        return poseFailure("pose and skeleton bone counts must match", "pose");
    if (binding.enableLeftHand && !validChain(binding.leftArm, skeleton))
        return poseFailure("left arm chain contains an invalid bone", "binding.leftArm");
    if (binding.enableRightHand && !validChain(binding.rightArm, skeleton))
        return poseFailure("right arm chain contains an invalid bone", "binding.rightArm");
    if (binding.enableLeftFoot && !validChain(binding.leftLeg, skeleton))
        return poseFailure("left leg chain contains an invalid bone", "binding.leftLeg");
    if (binding.enableRightFoot && !validChain(binding.rightLeg, skeleton))
        return poseFailure("right leg chain contains an invalid bone", "binding.rightLeg");
    if (!std::isfinite(advance.contactWeight) || !std::isfinite(advance.leftHandWeight) ||
        !std::isfinite(advance.rightHandWeight) || !std::isfinite(advance.leftFootWeight) ||
        !std::isfinite(advance.rightFootWeight) || !std::isfinite(advance.pelvisWeight))
        return poseFailure("contact weights must be finite", "advance.contactWeights");

    ClimbingPoseResult result;
    const float legacyWeight = std::clamp(advance.contactWeight, 0.f, 1.f);
    const float authoredLeft = std::clamp(advance.leftHandWeight, 0.f, 1.f);
    const float authoredRight = std::clamp(advance.rightHandWeight, 0.f, 1.f);
    // contactWeight summarizes all limbs, including feet and pelvis. Only old
    // aggregate-only callers use it as a hand weight.
    const bool hasLimbWeights = advance.leftHandWeight != 0.f || advance.rightHandWeight != 0.f ||
                                advance.leftFootWeight != 0.f || advance.rightFootWeight != 0.f ||
                                advance.pelvisWeight != 0.f;
    const float leftWeight  = binding.enableLeftHand ? (hasLimbWeights ? authoredLeft : legacyWeight) : 0.f;
    const float rightWeight = binding.enableRightHand ? (hasLimbWeights ? authoredRight : legacyWeight) : 0.f;
    const float leftFootWeight =
        binding.enableLeftFoot && advance.hasLeftFootAnchor ? std::clamp(advance.leftFootWeight, 0.f, 1.f) : 0.f;
    const float rightFootWeight =
        binding.enableRightFoot && advance.hasRightFootAnchor ? std::clamp(advance.rightFootWeight, 0.f, 1.f) : 0.f;
    result.weight = std::max({leftWeight, rightWeight, leftFootWeight, rightFootWeight});
    if (result.weight <= 0.f)
        return eve::Result<ClimbingPoseResult>::success(std::move(result), eve::Status::success(eve::StatusCode::NoOp));

    // Validate every active target before applying any constraint.
    const Vec3 leftTarget  = toModelSpace(advance.leftHandAnchor, binding);
    const Vec3 rightTarget = toModelSpace(advance.rightHandAnchor, binding);
    if (leftWeight > 0.f && !finite(leftTarget))
        return poseFailure("left hand target must be finite in model space", "advance.leftHandAnchor");
    if (rightWeight > 0.f && !finite(rightTarget))
        return poseFailure("right hand target must be finite in model space", "advance.rightHandAnchor");

    const Vec3 leftFootTarget  = toModelSpace(advance.leftFootAnchor, binding);
    const Vec3 rightFootTarget = toModelSpace(advance.rightFootAnchor, binding);
    if (leftFootWeight > 0.f && !finite(leftFootTarget))
        return poseFailure("left foot target must be finite in model space", "advance.leftFootAnchor");
    if (rightFootWeight > 0.f && !finite(rightFootTarget))
        return poseFailure("right foot target must be finite in model space", "advance.rightFootAnchor");

    animation::AnimConstraintStack constraints(&skeleton);
    if (binding.enableLeftHand && leftWeight > 0.f) {
        const Vec3 target = leftTarget;
        constraints.addTwoBoneIK(binding.leftArm.root, binding.leftArm.mid, binding.leftArm.tip, target.x, target.y,
                                 target.z, leftWeight);
        result.leftHandApplied = true;
    }
    if (binding.enableRightHand && rightWeight > 0.f) {
        const Vec3 target = rightTarget;
        constraints.addTwoBoneIK(binding.rightArm.root, binding.rightArm.mid, binding.rightArm.tip, target.x, target.y,
                                 target.z, rightWeight);
        result.rightHandApplied = true;
    }
    if (leftFootWeight > 0.f) {
        constraints.addTwoBoneIK(binding.leftLeg.root, binding.leftLeg.mid, binding.leftLeg.tip, leftFootTarget.x,
                                 leftFootTarget.y, leftFootTarget.z, leftFootWeight);
        result.leftFootApplied = true;
    }
    if (rightFootWeight > 0.f) {
        constraints.addTwoBoneIK(binding.rightLeg.root, binding.rightLeg.mid, binding.rightLeg.tip, rightFootTarget.x,
                                 rightFootTarget.y, rightFootTarget.z, rightFootWeight);
        result.rightFootApplied = true;
    }
    constraints.apply(&pose);
    return eve::Result<ClimbingPoseResult>::success(std::move(result), eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::climbing
