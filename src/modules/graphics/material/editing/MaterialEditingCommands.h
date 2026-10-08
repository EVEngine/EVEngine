#pragma once
#include "common/Export.h"


#include "editing/EditingCommandRegistry.h"

namespace eve::material_editing {

/** @brief Register Material-owned property commands with a generic editing host. */
[[nodiscard]] EVENGINE_API_BACKENDS editing::Result<void> registerEditingCommands(
    editing::IEditingCommandRegistry& registry);

}  // namespace eve::material_editing
