#pragma once
#include "common/Export.h"
#include "common/Result.h"
namespace eve::physics {
/** @brief Endpoint policy: advances clamp or wrap; explicit seeks always clamp inclusively. */
enum class BakePlaybackWrap { Clamp, Loop };
/** @brief Value-owned playback time state shared by baked channel implementations.
 * @ownership Owns time/rate/pause/wrap only; the immutable clip remains the duration authority.
 * @thread Each instance requires exclusive access during mutation; copies are independent. No callbacks or IO.
 * @lifetime Retains no clip or simulation references. Every operation receives explicit elapsed time/duration.
 */
class EVENGINE_API_BACKENDS BakePlaybackClock {
public:
    /** @brief Advance using nonnegative finite elapsed time and duration; errors preserve time even when paused.
     * @return Checked arithmetic/argument status. Loop uses [0,duration), zero-duration remains zero.
     */
    [[nodiscard]] Result<void> advance(double elapsedSeconds, double duration);
    /** @brief Seek finite seconds into inclusive [0,duration], ignoring rate and pause; errors preserve time. */
    [[nodiscard]] Result<void> seek(double seconds, double duration);
    /** @brief Set finite signed speed; zero freezes and negative reverses. */
    [[nodiscard]] Result<void> setRate(double rate);
    /** @brief Set a valid wrap policy, preserving state on invalid enum input. */
    [[nodiscard]] Result<void> setWrap(BakePlaybackWrap wrap);
    /** @brief Pause without changing time or rate. */
    void setPaused(bool paused) noexcept;
    /** @brief Current injected playback time. */
    [[nodiscard]] double time() const noexcept;
    /** @brief Current signed speed. */
    [[nodiscard]] double rate() const noexcept;
    /** @brief Whether advances leave time unchanged. */
    [[nodiscard]] bool paused() const noexcept;

private:
    double           time_ = 0, rate_ = 1;
    bool             paused_ = false;
    BakePlaybackWrap wrap_   = BakePlaybackWrap::Clamp;
};
}  // namespace eve::physics
