#pragma once

#include "common/StateValue.h"

#include <string>

namespace eve::card {

/**
 * @brief CardData/Hand interaction-state serialization for state hot reload.
 *
 * Captures each card's structural phase (deck/hand/played/discarded/disabled/
 * returning). Transient hover/drag interaction is dropped on restore.
 */
bool captureCardState(StateValue& out);
bool restoreCardState(const StateValue& in, std::string* err = nullptr);
bool resetCardState();

}  // namespace eve::card
