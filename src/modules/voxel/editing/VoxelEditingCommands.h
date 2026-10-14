#pragma once
#include "common/Export.h"


#include "editing/EditingCommandRegistry.h"

namespace eve::voxel_editing {

/** @brief Register voxel-catalog planned commands with a generic editing host. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION editing::Result<void> registerEditingCommands(
    editing::IEditingCommandRegistry& registry);

}  // namespace eve::voxel_editing
