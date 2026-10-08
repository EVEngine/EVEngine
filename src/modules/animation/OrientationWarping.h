#pragma once
#include "common/Export.h"

#include "common/Result.h"

#include <memory>
#include <span>

namespace eve::animation {
class AnimPose;
class AnimSkeleton;

/**
 * @brief Planar orientation warp applied after motion matching, never fed back into search.
 * @details Aligns an authored clip's planar displacement with a locomotion direction by adding
 * world-Y rotation. Root receives `(1 - distributedAlpha) * angle`; remaining yaw is split
 * equally across the configured spine chain. Optional IK bones restore their pre-warp world
 * orientation so separate foot targets stay animation-owned. Magnitude above the angle
 * threshold, or either planar speed below the minimum, targets zero instead of twisting
 * through a pivot. Interpolation uses injected simulation time.
 * @thread Owner simulation thread only; not reentrant; no callbacks.
 * @ownership The warper owns interpolation state. The skeleton is borrowed and must outlive
 * this object after a successful configure. Apply copies the input pose internally and
 * publishes only on success; failed calls leave the caller's pose and prior warp unchanged.
 */
class EVENGINE_API_WORLD OrientationWarping {
public:
    /** @brief Construct an unconfigured warper with no borrowed skeleton. */
    OrientationWarping();
    /** @brief Release owned interpolation state; the borrowed skeleton is not accessed. */
    ~OrientationWarping();
    OrientationWarping(const OrientationWarping&)            = delete;
    OrientationWarping& operator=(const OrientationWarping&) = delete;

    /**
     * @brief Atomically bind a skeleton and bone lists used by later apply calls.
     * @param skeleton Borrowed hierarchy; must outlive this warper until the next configure.
     * @param rootBone Index of the bone that receives the root share of the yaw.
     * @param spineBones Ordered parent-to-child chain that shares the remaining yaw; copied.
     * @param ikBones Bones whose world orientation is restored after the body warp; copied.
     * @return Applied, or InvalidArgument leaving the previous configuration and warp state.
     * @details At most 32 spine bones and 16 IK bones. Indices must be valid, unique, and
     * distinct from the root. Empty spine or IK lists are valid. No input pointers are retained.
     */
    [[nodiscard]] eve::Result<void> configure(AnimSkeleton& skeleton, int rootBone,
                                              std::span<const int> spineBones = {}, std::span<const int> ikBones = {});

    /**
     * @brief Set how much of the warp is distributed onto the spine chain.
     * @param alpha Finite value in [0, 1]. Zero applies the full yaw to the root.
     * @return Applied, or InvalidArgument without changing the previous alpha.
     */
    [[nodiscard]] eve::Result<void> setDistributedAlpha(float alpha);
    float getDistributedAlpha() const;

    /**
     * @brief Set the inclusive planar-angle magnitude that disables warping.
     * @param radians Finite value in (0, pi]. Default is 135 degrees.
     * @return Applied, or InvalidArgument without changing the previous threshold.
     */
    [[nodiscard]] eve::Result<void> setAngleThreshold(float radians);
    float getAngleThreshold() const;

    /**
     * @brief Set how quickly the applied yaw approaches the target, matching Unreal FInterpTo.
     * @param speed Finite value in [0, 100] (1/seconds). Zero snaps every apply.
     * @return Applied, or InvalidArgument without changing the previous speed.
     */
    [[nodiscard]] eve::Result<void> setRotationInterpSpeed(float speed);
    float getRotationInterpSpeed() const;

    /**
     * @brief Set the planar speed below which either direction is treated as stationary.
     * @param metresPerSecond Finite value in [0, 100]. Default is 0.1 m/s.
     * @return Applied, or InvalidArgument without changing the previous minimum.
     */
    [[nodiscard]] eve::Result<void> setMinRootMotionSpeed(float metresPerSecond);
    float getMinRootMotionSpeed() const;

    /** @brief Enable or disable pose mutation. Disabled apply calls still recompute the target. */
    void setEnabled(bool enabled);
    bool isEnabled() const;

    /** @brief Clear smoothed yaw so the next apply starts from zero. */
    void reset();

    /**
     * @brief Warp a local pose so animated planar velocity aligns with locomotion velocity.
     * @param pose Local pose matching the configured skeleton; mutated only on success.
     * @param locomotionX Desired planar velocity X in the same space as the pose, metres/second.
     * @param locomotionZ Desired planar velocity Z in the same space as the pose, metres/second.
     * @param animatedX Authored root-motion velocity X from the unwarped pose, metres/second.
     * @param animatedZ Authored root-motion velocity Z from the unwarped pose, metres/second.
     * @param dt Finite nonnegative simulation step in seconds, at most one second.
     * @return Applied, NoOp when disabled, or InvalidArgument leaving pose and warp unchanged.
     * @details Values must be finite. Search must keep reading the unwarped matcher pose;
     * copy it into a display pose before calling this. Deterministic for the same pose,
     * directions, dt and prior smoothed angle within floating-point tolerance.
     */
    [[nodiscard]] eve::Result<void> apply(AnimPose& pose, float locomotionX, float locomotionZ, float animatedX,
                                          float animatedZ, float dt);

    /** @brief Smoothed yaw last applied to a pose, radians, positive from +Z toward +X. */
    float getAppliedAngle() const;
    /** @brief Unsmoothed target yaw from the last successful apply, radians. */
    float getTargetAngle() const;
    /**
     * @brief Borrowed skeleton from the last successful configure, or null.
     * @ownership Borrowed; ownership remains with the caller.
     * @lifetime Valid until the next successful configure or destruction.
     */
    AnimSkeleton* getSkeleton() const;
    int           getRootBone() const;
    int           getSpineBoneCount() const;
    int           getIkBoneCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::animation
