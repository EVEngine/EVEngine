#pragma once
#include "common/Export.h"


#include "editing/EditingCommandRegistry.h"

namespace eve::animation_editing {

/** @brief Register animation-clip commands with a generic editing host. */
[[nodiscard]] EVENGINE_API_DOMAINS editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry);

}  // namespace eve::animation_editing
