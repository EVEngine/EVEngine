#pragma once
#include "physics/backend/bake/BakePlaybackClock.h"
#include "physics/backend/bake/BakeRigidTransform.h"
#include "physics/backend/bake/collapse/CollapseAnimation.h"

namespace eve::physics {
/** @brief Endpoint policy for advancement; explicit seeks always clamp. */
using CollapsePlaybackWrap = BakePlaybackWrap;
/** @brief Rigid placement in metres; rotates baked world coordinates about their origin, then translates.
 * Quaternion is XYZW, normalized at admission. Scaling is not part of this rigid placement contract.
 */
using CollapsePlacement = BakeRigidPlacement;
/**
 * @brief Independent clock over a frozen, shared collapse animation.
 * @ownership Owns a shared immutable snapshot. Copies share samples but copy clock state.
 * @lifetime No world, renderer or caller-owned pointers are retained.
 * @thread Each instance requires exclusive access during mutation; separate copies may run concurrently.
 * No callbacks, IO, wall clock or physical simulation occur during playback.
 */
class EVENGINE_API_BACKENDS CollapsePlayback {
public:
    /** @brief Freeze an owning clip, rejecting empty recordings.
     * @param animation Value snapshot; move an existing clip to avoid copying its samples.
     * @cost Copying an lvalue is linear in recorded samples; prepare once and copy players cheaply.
     * @return Player at zero seconds, running at rate one, with clamped endpoints, or an internal
     * allocation failure diagnostic. Existing players are unchanged; a moved input is consumed even on failure.
     * @throws std::bad_alloc If copying the by-value argument or allocating the failure diagnostic fails.
     */
    [[nodiscard]] static Result<CollapsePlayback> prepare(CollapseAnimation animation);
    /** @brief Advance by injected nonnegative elapsed seconds. Failures preserve time.
     * @return Failure for nonfinite/negative elapsed time or arithmetic overflow, even while paused.
     * Loop advancement uses [0,duration); zero-duration clips remain at zero.
     */
    [[nodiscard]] Result<void> advance(double elapsedSeconds);
    /** @brief Seek to finite seconds, clamped to both inclusive endpoints; ignores pause and rate. */
    [[nodiscard]] Result<void> seek(double seconds);
    /** @brief Set finite signed speed; zero freezes, negative reverses. Failure preserves rate. */
    [[nodiscard]] Result<void> setRate(double rate);
    /** @brief Pause or resume without modifying time. */
    void setPaused(bool paused) noexcept;
    /** @brief Set endpoint behavior, rejecting invalid enum values without mutation. */
    [[nodiscard]] Result<void> setWrap(CollapsePlaybackWrap wrap);
    /** @brief Set finite rigid placement with a nonzero quaternion. Invalid input preserves placement.
     * @return Checked admission status; coordinates that overflow after placement fail during sampling.
     */
    [[nodiscard]] Result<void> setPlacement(CollapsePlacement placement);
    /** @brief Current normalized rigid placement, returned by value. */
    [[nodiscard]] CollapsePlacement placement() const noexcept;
    /** @brief Current time in seconds. */
    [[nodiscard]] double time() const noexcept;
    /** @brief Signed playback speed. */
    [[nodiscard]] double rate() const noexcept;
    /** @brief Whether the clock is paused. */
    [[nodiscard]] bool paused() const noexcept;
    /** @brief Sample at current time using the clip's interpolation and discrete-state contract. */
    [[nodiscard]] Result<BonePresentation> sample(int bone) const;
    /** @brief Shared immutable clip; returned ownership survives this player's destruction. */
    [[nodiscard]] std::shared_ptr<const CollapseAnimation> animation() const noexcept;

private:
    explicit CollapsePlayback(std::shared_ptr<const CollapseAnimation> animation);
    std::shared_ptr<const CollapseAnimation> animation_;
    BakePlaybackClock                        clock_;
    BakeRigidTransform                       transform_;
};
}  // namespace eve::physics
