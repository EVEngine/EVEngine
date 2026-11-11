#pragma once
#include "common/Export.h"

#include "common/Result.h"

namespace eve::audio {
class Source;

/**
 * @brief Cross-fade two caller-owned sources over an injected simulation interval.
 * @param from Source that fades out and stops.
 * @param to Source that starts (or continues) and fades in to toVolume.
 * @param toVolume Destination user volume for `to` (>= 0).
 * @param durationSeconds Non-negative ramp length shared by both sides.
 * @param now Absolute simulation time in seconds.
 * @return Success after both fades are armed; callers must advanceFade(now) each frame.
 * @ownership Sources remain caller-owned and are never retained.
 * @cost Arms two fades and may start playback on `to`; reuse sources across transitions.
 * @thread Audio owner thread only; no callbacks.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> crossfadeSources(Source* from, Source* to, float toVolume,
                                                                  float durationSeconds, float now);
}  // namespace eve::audio
