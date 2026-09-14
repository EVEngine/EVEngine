#pragma once

#include "common/Result.h"

namespace eve::audio {
class Source;

/**
 * @brief Apply one Pcg underwater audio transition to caller-owned sources.
 * @param submergeDown Optional one-shot source played when playDown is true.
 * @param submergeUp Optional one-shot source played when playUp is true.
 * @param ambience Required looping underwater ambience source.
 * @param playDown Whether to play the downward transition once.
 * @param playUp Whether to play the upward transition once.
 * @param loopAmbience Whether ambience should be playing after this call.
 * @param volume Normalized transition and ambience gain in [0,1].
 * @return Success or InvalidArgument; validation completes before source mutation.
 * @ownership Sources remain caller-owned and are never retained.
 * @thread Audio owner thread only; callbacks are not invoked.
 */
[[nodiscard]] Result<void> applyUnderwaterAudio(Source* submergeDown, Source* submergeUp,
                                                 Source* ambience, bool playDown, bool playUp,
                                                 bool loopAmbience, float volume);
}
