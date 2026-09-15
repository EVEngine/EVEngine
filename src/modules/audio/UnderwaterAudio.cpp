#include "audio/UnderwaterAudio.h"

#include "audio/Source.h"

#include <cmath>

namespace eve::audio {

Result<void> applyUnderwaterAudio(Source* submergeDown, Source* submergeUp, Source* ambience,
                                  bool playDown, bool playUp, bool loopAmbience, float volume) {
    if (!ambience || !std::isfinite(volume) || volume < 0.f || volume > 1.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "audio.underwater: ambience and normalized finite volume required"));

    if (submergeDown) submergeDown->setVolume(volume);
    if (submergeUp) submergeUp->setVolume(volume);
    ambience->setVolume(volume);
    ambience->setLooping(true);
    if (playDown && submergeDown) submergeDown->play();
    if (playUp && submergeUp) submergeUp->play();
    if (loopAmbience) {
        if (!ambience->isPlaying()) ambience->play();
    } else if (ambience->isPlaying()) {
        ambience->stop();
    }
    return Result<void>::success();
}

}  // namespace eve::audio
