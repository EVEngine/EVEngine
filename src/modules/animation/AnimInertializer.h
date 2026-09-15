#pragma once

#include <memory>
#include <span>
#include "common/Result.h"

namespace eve::animation {
class AnimPose;

/** @brief Finite-duration pose transition relative to a continuously sampled target.
 * @details Owns all history and output; retains no skeleton, clip or input pose pointers.
 * Local TRS inputs must share bone order and coordinate space. Unlike ControlPose,
 * this converges exactly at the requested deadline and does not continually lag its target.
 * @thread Owner thread only; no callbacks or reentrancy. Explicit elapsed simulation
 * time makes repeated evaluation deterministic within floating-point tolerance.
 */
class AnimInertializer {
public:
    /** @brief Construct an empty, independently owned transition. */
    AnimInertializer();
    /** @brief Release owned samples; no external objects are accessed. */
    ~AnimInertializer();
    AnimInertializer(const AnimInertializer&)            = delete;
    AnimInertializer& operator=(const AnimInertializer&) = delete;

    /** @brief Atomically begin or interrupt a transition using actual outgoing pose history.
     * @param source Current outgoing local pose, borrowed only for this call.
     * @param previousSource Outgoing pose one historySeconds earlier, copied on success.
     * @param target Incoming pose at transition start, copied on success.
     * @param previousTarget Incoming pose one historySeconds earlier, copied on success.
     * @param historySeconds Positive finite history interval, at most one second.
     * @param durationSeconds Finite nonnegative transition duration, at most ten seconds; zero snaps.
     * @param boneTimeFactors Empty for uniform duration, otherwise one finite [0,1] factor per bone;
     * copied. Zero snaps that bone immediately, one uses the full duration.
     * @return Applied or InvalidArgument leaving prior transition/output unchanged.
     */
    [[nodiscard]] eve::Result<void> begin(const AnimPose& source, const AnimPose& previousSource,
                                          const AnimPose& target, const AnimPose& previousTarget, float historySeconds,
                                          float durationSeconds, std::span<const float> boneTimeFactors = {});

    /** @brief Evaluate the moving target at an absolute elapsed simulation time since begin.
     * @param target Borrowed local pose at elapsedSeconds; copied, never retained.
     * @param elapsedSeconds Finite nonnegative time; evaluations may be replayed or partitioned freely.
     * @return Applied or InvalidArgument without changing output. Requires a successful begin.
     * @details At/after the deadline output equals target. No input pose is modified.
     */
    [[nodiscard]] eve::Result<void> evaluate(const AnimPose& target, float elapsedSeconds);

    /** @brief Borrow read-only output, valid until destruction; contents change after successful begin/evaluate.
     * Empty before the first successful begin. Copy it to retain a historical sample.
     */
    [[nodiscard]] const AnimPose& pose() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::animation
