#pragma once

#include "editing/EditingCommandRegistry.h"

namespace eve::archspace_editing {

/** @brief Register ArchSpace-owned planned commands with a generic editing host. */
[[nodiscard]] editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry);

}  // namespace eve::archspace_editing
