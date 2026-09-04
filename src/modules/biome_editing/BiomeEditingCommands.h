#pragma once

#include "editing/EditingCommandRegistry.h"

namespace eve::biome_editing {

/** @brief Register Biome-owned planned commands with a generic editing host. */
[[nodiscard]] editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry);

}  // namespace eve::biome_editing
