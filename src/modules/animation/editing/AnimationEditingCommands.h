#pragma once

#include "editing/EditingCommandRegistry.h"

namespace eve::animation_editing {

/** @brief Register animation-clip commands with a generic editing host. */
[[nodiscard]] editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry);

}  // namespace eve::animation_editing
