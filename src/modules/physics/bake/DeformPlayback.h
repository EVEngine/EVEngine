#pragma once
#include "physics/bake/BakePlaybackClock.h"
#include "physics/bake/BakeRigidTransform.h"
#include "physics/bake/DeformTrack.h"
namespace eve::physics {
/** @brief Independent clock and placement over a shared immutable deformation clip.
 * @ownership Owns reusable scratch storage; shares only immutable clip samples. No source/world pointers.
 * @thread Each player, including sampling, requires exclusive access; separate players are independent.
 * @remarks Move-only to make buffer preparation explicit. Moved-from objects may only be destroyed or reassigned.
 */
class EVENGINE_API_BACKENDS DeformPlayback {
public:
    /** @brief Prepare one player's scratch buffer; use separate preparations for independently controlled instances.
     * @cost Linear in vertices for scratch allocation, amortized over all subsequent samples.
     * @return Ready player or allocation diagnostic, without modifying the input clip.
     */
    [[nodiscard]] static Result<DeformPlayback> prepare(DeformTrack track);
    DeformPlayback(DeformPlayback&&) noexcept            = default;
    DeformPlayback& operator=(DeformPlayback&&) noexcept = default;
    DeformPlayback(const DeformPlayback&)                = delete;
    DeformPlayback& operator=(const DeformPlayback&)     = delete;
    /** @brief Advance explicit elapsed seconds; errors preserve time. */
    [[nodiscard]] Result<void> advance(double seconds);
    /** @brief Seek finite time, clamping to both endpoints. */
    [[nodiscard]] Result<void> seek(double seconds);
    /** @brief Set signed finite playback rate. */
    [[nodiscard]] Result<void> setRate(double rate);
    /** @brief Set clamp or loop endpoint behavior. */
    [[nodiscard]] Result<void> setWrap(BakePlaybackWrap wrap);
    /** @brief Pause without changing rate or time. */
    void setPaused(bool paused) noexcept;
    /** @brief Set finite rigid placement; planar coordinates reject out-of-plane placement. */
    [[nodiscard]] Result<void> setPlacement(BakeRigidPlacement placement);
    /** @brief Current normalized instance placement. */
    [[nodiscard]] BakeRigidPlacement placement() const noexcept;
    /** @brief Current playback time in seconds. */
    [[nodiscard]] double time() const noexcept;
    /** @brief Current signed speed. */
    [[nodiscard]] double rate() const noexcept;
    /** @brief Current paused state. */
    [[nodiscard]] bool paused() const noexcept;
    /** @brief Return a cheap owning immutable clip copy, independent of player lifetime. */
    [[nodiscard]] DeformTrack track() const;
    /** @brief Sample and transform into exact-sized output; all failures preserve the caller's entire buffer.
     * @ownership Borrows output only for this call. Retains only private scratch storage, never output addresses.
     * @cost Linear in vertices, reusing preparation storage; no allocations, IO or solver calls on success.
     * @return Success or output extent/sample/placement overflow diagnostic; clock is never advanced implicitly.
     */
    [[nodiscard]] Result<void> sample(std::span<DeformPoint> output);

private:
    DeformPlayback(DeformTrack track, std::vector<DeformPoint> scratch);
    DeformTrack              track_;
    std::vector<DeformPoint> scratch_;
    BakePlaybackClock        clock_;
    BakeRigidTransform       transform_;
};
}  // namespace eve::physics
