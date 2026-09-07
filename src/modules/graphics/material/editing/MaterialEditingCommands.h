#pragma once

#include "editing/EditingCommandRegistry.h"

namespace eve::material_editing {

/** @brief Register Material-owned property commands with a generic editing host. */
[[nodiscard]] editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry);

}  // namespace eve::material_editing
