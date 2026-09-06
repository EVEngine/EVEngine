#pragma once

#include "editing/EditingCommandRegistry.h"

namespace eve::voxel_editing {

/** @brief Register voxel-catalog planned commands with a generic editing host. */
[[nodiscard]] editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry);

}  // namespace eve::voxel_editing
