#include "audio/UnderwaterAudio.h"

#include "audio/Source.h"

#include <cmath>

namespace eve::audio {

Result<void> applyUnderwaterAudio(Source* submergeDown, Source* submergeUp, Source* ambience, bool playDown,
                                  bool playUp, bool loopAmbience, float volume, float fadeSeconds, float now) {
    if (!ambience || !std::isfinite(volume) || volume < 0.f || volume > 1.f || !std::isfinite(fadeSeconds) ||
        fadeSeconds < 0.f || !std::isfinite(now))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "audio.underwater: ambience, normalized volume, non-negative fadeSeconds and finite now "
                              "required"));

    if (submergeDown) submergeDown->setVolume(volume);
    if (submergeUp) submergeUp->setVolume(volume);
    ambience->setLooping(true);
    if (playDown && submergeDown) submergeDown->play();
    if (playUp && submergeUp) submergeUp->play();

    if (loopAmbience) {
        if (!ambience->isPlaying()) {
            ambience->setVolume(fadeSeconds > 0.f ? 0.f : volume);
            ambience->play();
            if (fadeSeconds > 0.f) {
                auto fade = ambience->fadeTo(volume, fadeSeconds, now);
                if (!fade.ok()) return fade;
            }
        } else {
            auto fade = ambience->fadeTo(volume, fadeSeconds, now);
            if (!fade.ok()) return fade;
        }
    } else if (fadeSeconds > 0.f) {
        auto fade = ambience->fadeOutAndStop(fadeSeconds, now);
        if (!fade.ok()) return fade;
    } else if (ambience->isPlaying()) {
        ambience->setVolume(volume);
        ambience->stop();
    } else {
        ambience->setVolume(volume);
    }
    return Result<void>::success();
}

}  // namespace eve::audio
