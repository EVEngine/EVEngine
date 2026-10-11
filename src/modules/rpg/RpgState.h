#pragma once
#include "common/Export.h"


#include "common/StateValue.h"

#include <string>

namespace eve::rpg {

/**
 * @brief Compatibility facade for RPGActor skill-state serialization during state hot reload.
 *
 * Captures per-actor known-skill cooldowns and the in-flight casting state.
 * Casting targets are raw pointers and are not serialized (restored as null).
 * The bool result is kept for the StateProvider compatibility surface; new
 * state systems should expose structured Result diagnostics at their boundary.
 */
EVENGINE_API_PLATFORM bool captureRpgState(StateValue& out);
/** @brief Restore rpg state. */
EVENGINE_API_PLATFORM bool restoreRpgState(const StateValue& in, std::string* err = nullptr);
/** @brief Resets rpg state. */
EVENGINE_API_PLATFORM bool resetRpgState();

}  // namespace eve::rpg
