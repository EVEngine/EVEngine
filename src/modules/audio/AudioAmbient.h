#pragma once
#include "common/Export.h"

#include "common/Result.h"

namespace eve::audio {
class Source;

/**
 * @brief Configure a caller-owned Source as a non-spatial stereo/mono ambient bed.
 * @details Sets spatialBlend=0 (relative, rolloff 0) and balance=0. Keeps channel layout.
 * @ownership Source remains caller-owned and is never retained.
 * @thread Audio owner thread only; no callbacks.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> configureAmbientBed(Source* source);
}  // namespace eve::audio
