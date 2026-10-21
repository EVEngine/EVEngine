#include "physics/bake/BakePlaybackClock.h"
#include <algorithm>
#include <cmath>
namespace eve::physics {
namespace {
Diagnostic clockError(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "bakePlaybackClock");
}
}  // namespace
Result<void> BakePlaybackClock::advance(double elapsedSeconds, double duration) {
    if (!std::isfinite(duration) || duration < 0 || !std::isfinite(elapsedSeconds) || elapsedSeconds < 0)
        return Result<void>::failure(clockError("invalid playback elapsed time"));
    const double delta = elapsedSeconds * rate_;
    const double next  = time_ + delta;
    if (!std::isfinite(delta) || !std::isfinite(next))
        return Result<void>::failure(clockError("playback time overflow"));
    if (paused_ || delta == 0) return Result<void>::success();
    double candidate = std::clamp(next, 0.0, duration);
    if (wrap_ == BakePlaybackWrap::Loop && duration > 0) {
        candidate = std::fmod(next, duration);
        if (candidate < 0) candidate += duration;
        // Rounding of a tiny negative remainder can reach the excluded endpoint.
        if (candidate >= duration) candidate = 0;
    }
    time_ = candidate;
    return Result<void>::success();
}
Result<void> BakePlaybackClock::seek(double seconds, double duration) {
    if (!std::isfinite(duration) || duration < 0 || !std::isfinite(seconds))
        return Result<void>::failure(clockError("invalid playback seek time"));
    time_ = std::clamp(seconds, 0.0, duration);
    return Result<void>::success();
}
Result<void> BakePlaybackClock::setRate(double rate) {
    if (!std::isfinite(rate)) return Result<void>::failure(clockError("invalid playback rate"));
    rate_ = rate;
    return Result<void>::success();
}
void         BakePlaybackClock::setPaused(bool paused) noexcept { paused_ = paused; }
Result<void> BakePlaybackClock::setWrap(BakePlaybackWrap wrap) {
    if (wrap != BakePlaybackWrap::Clamp && wrap != BakePlaybackWrap::Loop)
        return Result<void>::failure(clockError("invalid playback wrap mode"));
    wrap_ = wrap;
    return Result<void>::success();
}
double BakePlaybackClock::time() const noexcept { return time_; }
double BakePlaybackClock::rate() const noexcept { return rate_; }
bool   BakePlaybackClock::paused() const noexcept { return paused_; }
}  // namespace eve::physics
