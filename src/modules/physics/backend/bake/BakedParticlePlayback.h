#pragma once
#include "physics/backend/bake/BakePlaybackClock.h"
#include "physics/backend/bake/BakeRigidTransform.h"
#include "physics/backend/bake/ParticleTrack.h"
namespace eve::physics {
/** @brief Independent clock and placement over a shared immutable particle clip.
 * @ownership Owns reusable scratch storage; shares only immutable clip samples. No source/world pointers.
 * @thread Each player, including sampling, requires exclusive access; separate players are independent.
 * @remarks Move-only to make buffer preparation explicit. Moved-from objects may only be destroyed or reassigned.
 */
class EVENGINE_API_BACKENDS BakedParticlePlayback {
public:
    /** @brief Prepare one player's scratch buffer; use separate preparations for independently controlled instances.
     * @cost Linear in births for scratch allocation, amortized over all subsequent samples.
     * @return Ready player or allocation diagnostic, without modifying the input clip.
     */
    [[nodiscard]] static Result<BakedParticlePlayback> prepare(ParticleTrack track);
    BakedParticlePlayback(BakedParticlePlayback&&) noexcept            = default;
    BakedParticlePlayback& operator=(BakedParticlePlayback&&) noexcept = default;
    BakedParticlePlayback(const BakedParticlePlayback&)                = delete;
    BakedParticlePlayback& operator=(const BakedParticlePlayback&)     = delete;
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
    /** @brief Set finite rigid placement around the baking origin, rotating sprite centers and degree angles; planar
     * coordinates reject out-of-plane placement. */
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
    [[nodiscard]] ParticleTrack track() const;
    /** @brief Sample and transform into capacity-sized output; all failures preserve the caller's entire buffer.
     * @ownership Borrows output only for this call. Retains only private scratch storage, never output addresses.
     * @cost Linear in births, reusing preparation storage; no allocations, IO or solver calls on success.
     * @return Visible count or output extent/sample/placement overflow diagnostic; clock is never advanced implicitly.
     */
    [[nodiscard]] Result<std::size_t> sample(std::span<BakedParticleSample> output);

private:
    BakedParticlePlayback(ParticleTrack track, std::vector<BakedParticleSample> scratch);
    ParticleTrack                    track_;
    std::vector<BakedParticleSample> scratch_;
    BakePlaybackClock                clock_;
    BakeRigidTransform               transform_;
};
}  // namespace eve::physics
