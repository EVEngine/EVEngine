#pragma once
#include "common/Export.h"


#include "common/StateValue.h"

#include <string>

namespace eve::card {

/**
 * @brief CardData/Hand interaction-state serialization for state hot reload.
 *
 * Captures each card's structural phase (deck/hand/played/discarded/disabled/
 * returning). Transient hover/drag interaction is dropped on restore.
 */
EVENGINE_API_WORLD bool captureCardState(StateValue& out);
EVENGINE_API_WORLD bool restoreCardState(const StateValue& in, std::string* err = nullptr);
EVENGINE_API_WORLD bool resetCardState();

}  // namespace eve::card
