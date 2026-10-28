#pragma once
#include "common/Export.h"


#include "common/StateValue.h"

#include <string>

namespace eve::card {

/**
 * @brief Compatibility facade for CardData/Hand interaction-state serialization during state hot reload.
 *
 * Captures each card's structural phase (deck/hand/played/discarded/disabled/
 * returning). Transient hover/drag interaction is dropped on restore. The bool
 * result is kept for the StateProvider compatibility surface; new state systems
 * should expose structured Result diagnostics at their module boundary.
 */
EVENGINE_API_WORLD bool captureCardState(StateValue& out);
/** @brief Restore card state. */
EVENGINE_API_WORLD bool restoreCardState(const StateValue& in, std::string* err = nullptr);
/** @brief Resets card state. */
EVENGINE_API_WORLD bool resetCardState();

}  // namespace eve::card
