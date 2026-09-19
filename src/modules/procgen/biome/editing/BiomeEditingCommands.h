#pragma once
#include "common/Export.h"


#include "editing/EditingCommandRegistry.h"

namespace eve::biome_editing {

/** @brief Register Biome-owned planned commands with a generic editing host. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry);

}  // namespace eve::biome_editing
