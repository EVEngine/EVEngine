#pragma once

/** @file UpperBodyOrientationCompensator.h @brief Root-relative upper-body orientation correction. */

#include "animation/AnimPose.h"
#include "common/Result.h"

#include <string>

namespace eve::animation {

class AnimSkeleton;

/** @brief Stable bone-name profile used to prepare upper-body orientation compensation. */
struct UpperBodyOrientationProfile {
    std::string rootBone;
    std::string hipsBone;
    std::string spineBone;
};

/**
 * @brief Resolves a bone profile once and corrects an upper-body pose against locomotion orientation.
 *
 * The compensator borrows one immutable skeleton for its whole lifetime. configure() resolves names into
 * coordinator-local indices and publishes them only after every invariant succeeds; it never retains strings,
 * masks, poses, or provider objects. apply() is owner-thread-only, deterministic for identical inputs, and
 * retains no pose references after the synchronous call.
 */
class UpperBodyOrientationCompensator {
public:
    /** @brief Construct over a skeleton that must outlive this object. */
    explicit UpperBodyOrientationCompensator(const AnimSkeleton& skeleton) noexcept;

    /**
     * @brief Transactionally replace the resolved profile.
     * @return Applied on success; InvalidArgument when a named bone is missing or the hierarchy is invalid.
     */
    [[nodiscard]] Result<void> configure(const UpperBodyOrientationProfile& profile);
    /** @brief Clear the resolved profile. */
    void clear() noexcept;
    /** @brief Whether a validated profile is currently published. */
    [[nodiscard]] bool isConfigured() const noexcept { return configured_; }

    /**
     * @brief Blend the sampled upper-body spine orientation into an accumulated pose.
     * @param pose Accumulated pose mutated synchronously; world transforms are refreshed before return.
     * @param upperBodyPose Sampled upper-body pose whose spine orientation is interpreted relative to its root.
     * @param weight Finite blend influence in [0,1].
     * @return Applied/NoOp, or InvalidArgument without mutation for incompatible poses or weight.
     */
    [[nodiscard]] Result<void> apply(AnimPose& pose, AnimPose& upperBodyPose, float weight) const;

private:
    const AnimSkeleton& skeleton_;
    int                 rootBone_  = -1;
    int                 hipsBone_  = -1;
    int                 spineBone_ = -1;
    bool                configured_ = false;
};

}  // namespace eve::animation
