#pragma once

#include "common/Export.h"

#include "common/Value.h"
#include "fluids/OceanSpectrum.h"

namespace eve::fluids {
/**
 * @brief Encode an owning eve.ocean.spectrum version-1 definition for scripts and persistence.
 * The uint64 seed is canonical decimal text, preserving all bits through a script VM.
 * @thread Const/reentrant, call-only input, no callbacks or retained references.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeOceanSpectrumSettings(const OceanSpectrumSettings& settings);
/**
 * @brief Decode a complete version-1 definition; reject unknown/missing fields and unsupported versions.
 * No earlier format exists: migration is explicitly unsupported. Structural decoding is bounded to
 * four bands; domain and spectral-energy validation remains in OceanSpectrumBuilder::build().
 * @return Owning settings or InvalidArgument; never changes an existing ocean.
 * @thread Const/reentrant, call-only input, no callbacks or retained references.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<OceanSpectrumSettings> decodeOceanSpectrumSettings(const Value& value);
}  // namespace eve::fluids
