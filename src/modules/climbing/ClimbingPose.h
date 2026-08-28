#pragma once

/**
 * @file ClimbingPose.h
 * @brief Synchronous Animation TwoBoneIK adapter for climbing contact anchors.
 */

#include "climbing/Climbing.h"

namespace eve::animation {
class AnimPose;
class AnimSkeleton;
}  // namespace eve::animation

namespace eve::climbing {

/** @brief Three-bone limb chain used by Animation's built-in TwoBoneIK solver. */
struct ClimbingLimbChain {
    int root = -1;
    int mid  = -1;
    int tip  = -1;
};

/** @brief Borrowed-pose adapter inputs; no skeleton or pose pointer is retained. */
struct ClimbingPoseBinding {
    ClimbingLimbChain leftArm;
    ClimbingLimbChain rightArm;
    Vec3              modelWorldOrigin;
    float             modelYawRadians = 0.f;
    bool              enableLeftHand  = true;
    bool              enableRightHand = true;
};

/** @brief Observable result of applying built-in hand constraints. */
struct ClimbingPoseResult {
    bool        leftHandApplied  = false;
    bool        rightHandApplied = false;
    float       weight           = 0.f;
    std::string provider         = "animation.two_bone";
};

/**
 * @brief Applies contact anchors to an already evaluated animation pose.
 * @param skeleton Borrowed synchronously; must outlive pose application only.
 * @param pose Borrowed mutable pose; the adapter refreshes world transforms.
 * @param advance Owning output from the authoritative climbing motion phase.
 * @param binding Bone chains and model-to-world placement for this frame.
 * @return Which constraints were applied, or a structured validation failure.
 * @thread PostPhysics owner thread.
 * @reentrancy Does not invoke callbacks or scripts.
 */
[[nodiscard]] eve::Result<ClimbingPoseResult> applyClimbingPose(animation::AnimSkeleton&   skeleton,
                                                                animation::AnimPose&       pose,
                                                                const ClimbingAdvance&     advance,
                                                                const ClimbingPoseBinding& binding);

}  // namespace eve::climbing
