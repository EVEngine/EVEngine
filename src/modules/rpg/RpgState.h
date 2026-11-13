#pragma once
#include "common/Export.h"


#include "common/Result.h"
#include "common/StateValue.h"

#include <string>

namespace eve::rpg {

/** @brief Capture cooldowns and self-cast and retired-target hot-reload facts on the current RPG simulation thread.
 * @return Owning values or invalid clocks / Unsupported for active linked target context.
 * @remarks Not a persistent world archive: the same current-world actor roster and execution keys must be supplied on
 * restore. The casting projection is eve.rpg.casting-reload v2; no callbacks, services or mutations are invoked.
 * @cost Copy each actor's known skill clocks once per explicit hot reload.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<Value> captureRpgState();

/** @brief Validate every actor snapshot before publishing any RPG hot-reload state.
 * @param state Complete actor clock participants borrowed only for this call.
 * @return Success or clock/schema/version/context failure without changing any actor.
 * @remarks Strict casting v2 fields; legacy passive projections import as inactive. Ambiguous legacy active
 * projections require explicit migration and are rejected; linked target restore belongs to the world coordinator.
 * @ownership Borrows input only for this synchronous owner-thread call; no callbacks or locks.
 * @cost Copy and validate all saved actor clocks before one publication per explicit reload.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<void> restoreRpgState(const Value& state);

/** @brief Cancel all in-flight RPG casts. */
[[nodiscard]] EVENGINE_API_PLATFORM Result<void> resetRpgStateResult();

/**
 * @brief Compatibility facade for RPGActor skill-state serialization during state hot reload.
 *
 * Captures per-actor known-skill cooldowns and the in-flight casting state.
 * The v2 casting projection admits default-self and retired intent. Linked active targets and ambiguous
 * legacy active snapshots are rejected until a target-rebinding participant is supplied; no silent self retargeting.
 * The bool result is kept for the StateProvider compatibility surface; new
 * state systems should expose structured Result diagnostics at their boundary.
 */
EVENGINE_API_PLATFORM bool captureRpgState(StateValue& out);
/** @brief Restore rpg state. */
EVENGINE_API_PLATFORM bool restoreRpgState(const StateValue& in, std::string* err = nullptr);
/** @brief Resets rpg state. */
EVENGINE_API_PLATFORM bool resetRpgState();

}  // namespace eve::rpg
