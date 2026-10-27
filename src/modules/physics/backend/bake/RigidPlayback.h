#pragma once
#include "physics/backend/bake/BakePlaybackClock.h"
#include "physics/backend/bake/BakeRigidTransform.h"
#include "physics/backend/bake/RigidPoseTrack.h"
namespace eve::physics {
/** @brief Independent clock over a frozen rigid track; no solver, world, renderer or wall clock.
 * @ownership Holds a cheap immutable track copy sharing sample storage, plus value-owned clock state.
 * @thread Mutate each player exclusively; copies may run independently. No callbacks or IO.
 */
class EVENGINE_API_BACKENDS RigidPlayback {
public:
    /** @brief Start at zero, unpaused at rate one; copying the frozen track is constant-time. */
    explicit RigidPlayback(RigidPoseTrack track);
    /** @brief Advance by explicit nonnegative elapsed seconds; failure preserves time. */
    [[nodiscard]] Result<void> advance(double elapsedSeconds);
    /** @brief Seek finite seconds, clamping to both clip endpoints. */
    [[nodiscard]] Result<void> seek(double seconds);
    /** @brief Set finite signed speed; zero freezes and negative reverses. */
    [[nodiscard]] Result<void> setRate(double rate);
    /** @brief Set clamp/loop policy, rejecting unknown values. */
    [[nodiscard]] Result<void> setWrap(BakePlaybackWrap wrap);
    /** @brief Set instance placement in track units; planar tracks reject out-of-plane translation/rotation.
     * @return Checked finite/quaternion admission; failures preserve placement.
     */
    [[nodiscard]] Result<void> setPlacement(BakeRigidPlacement placement);
    /** @brief Current normalized placement by value. */
    [[nodiscard]] BakeRigidPlacement placement() const noexcept;
    /** @brief Pause without modifying time or speed. */
    void setPaused(bool paused) noexcept;
    /** @brief Current playback time in seconds. */
    [[nodiscard]] double time() const noexcept;
    /** @brief Current signed speed. */
    [[nodiscard]] double rate() const noexcept;
    /** @brief Whether this player is paused. */
    [[nodiscard]] bool paused() const noexcept;
    /** @brief Duration from the immutable track, the sole duration authority. */
    [[nodiscard]] float duration() const noexcept;
    /** @brief Return a cheap owning copy of the immutable track, valid after player destruction. */
    [[nodiscard]] RigidPoseTrack track() const;
    /** @brief Sample one persistent identity at this player's current time. */
    [[nodiscard]] Result<RigidTrackPose> sample(PersistentId id) const;

private:
    RigidPoseTrack     track_;
    BakePlaybackClock  clock_;
    BakeRigidTransform transform_;
};
}  // namespace eve::physics
