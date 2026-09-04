#pragma once

#include "editing/EditingCommandRegistry.h"

namespace eve::avatar_editing {

/** @brief Register Avatar-owned property and structure commands with a generic editing host. */
[[nodiscard]] editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry);

}  // namespace eve::avatar_editing
