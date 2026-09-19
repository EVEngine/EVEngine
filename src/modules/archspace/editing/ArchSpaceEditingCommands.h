#pragma once
#include "common/Export.h"


#include "editing/EditingCommandRegistry.h"

namespace eve::archspace_editing {

/** @brief Register ArchSpace-owned planned commands with a generic editing host. */
[[nodiscard]] EVENGINE_API_DOMAINS editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry);

}  // namespace eve::archspace_editing
