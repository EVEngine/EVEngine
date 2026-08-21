#pragma once

#include "common/StateValue.h"

#include <string>

namespace eve::rpg {

/**
 * @brief RPGActor skill-state serialization for state hot reload.
 *
 * Captures per-actor known-skill cooldowns and the in-flight casting state.
 * Casting targets are raw pointers and are not serialized (restored as null).
 */
bool captureRpgState(StateValue& out);
bool restoreRpgState(const StateValue& in, std::string* err = nullptr);
bool resetRpgState();

}  // namespace eve::rpg
