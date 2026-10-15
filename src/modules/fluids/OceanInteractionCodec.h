#pragma once

#include "common/Export.h"

#include "common/Value.h"
#include "fluids/OceanBoat.h"
#include "fluids/OceanWake.h"

namespace eve::fluids {
/** @brief Encode owning transient wake construction arguments; not a persistent checkpoint.
 * @thread Const/reentrant, call-only input, no callbacks or retained references.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeOceanWakeSettings(const OceanWakeSettings& settings);
/** @brief Strict bounded structural decode; native OceanWake::create owns domain validation.
 * @return Owning candidate or InvalidArgument; unknown/missing fields fail without mutation.
 * @thread Const/reentrant, call-only input, no callbacks or retained references.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<OceanWakeSettings> decodeOceanWakeSettings(const Value& value);
/** @brief Encode owning transient hull arguments; physics links are deliberately absent.
 * @thread Const/reentrant, call-only input, no callbacks or retained references.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeOceanHullSettings(const OceanHullSettings& settings);
/** @brief Strict structural decode of at most 64 cells; OceanBoat::create validates all physical domains.
 * @return Owning candidate or InvalidArgument; unknown/missing fields fail without mutation.
 * @thread Const/reentrant, call-only input, no callbacks or retained references.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<OceanHullSettings> decodeOceanHullSettings(const Value& value);
}  // namespace eve::fluids
