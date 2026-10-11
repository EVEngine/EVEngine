#pragma once
#include "common/Result.h"
namespace ssq {
class Table;
}
namespace eve::graphics {
class Shader;
struct VegetationWindState;
struct VegetationWindProfile;
/** @brief Atomically apply a validated wind snapshot to a grass shader's CPU uniform storage.
 * @param shader Borrowed grass shader with the canonical 32-float layout.
 * @param state Borrowed immutable globals.
 * @param profile Borrowed immutable material controls.
 * @param seconds Explicit finite clock for full and quarter-frequency sine values.
 * @return Success or InvalidArgument for invalid inputs/schema; failure preserves all uniform bytes.
 * @ownership Retains no references. Call on the shader's Graphics owner thread outside draw/capture
 * callbacks. No GPU allocation or callback occurs. Re-baking creates a new shader with disabled wind;
 * callers reapply their authoritative snapshot after baking. This function does not advance state.
 */
[[nodiscard]] Result<void> applyGrassWind(Shader& shader, const VegetationWindState& state,
                                          const VegetationWindProfile& profile, double seconds);
/** @brief Register wind state/profile and checked operations on the VM owner thread, retaining no table reference. */
void exposeGrassWindBindings(ssq::Table& table);
}  // namespace eve::graphics
