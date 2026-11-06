#include "audio/AudioFade.h"

#include "audio/Source.h"

#include <cmath>

namespace eve::audio {

Result<void> crossfadeSources(Source* from, Source* to, float toVolume, float durationSeconds, float now) {
    if (!from || !to)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "crossfade requires from and to sources", "audio.crossfade"));
    if (!std::isfinite(toVolume) || toVolume < 0.f || !std::isfinite(durationSeconds) || durationSeconds < 0.f ||
        !std::isfinite(now))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "crossfade requires finite now, non-negative duration and toVolume", "audio.crossfade"));

    to->setVolume(0.f);
    if (!to->isPlaying()) to->play();
    auto fadeIn = to->fadeTo(toVolume, durationSeconds, now);
    if (!fadeIn.ok()) return fadeIn;
    return from->fadeOutAndStop(durationSeconds, now);
}

}  // namespace eve::audio
