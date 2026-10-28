#pragma once

#include "editing/EditingCommandRegistry.h"

namespace eve::action_editor {

/** @brief Metadata for the Action property-set command (shared with EditorPackage). */
[[nodiscard]] editing::EditingCommandDescriptor actionPropertySetCommandDescriptor();

/** @brief Register Action-owned property commands with a generic editing host. */
[[nodiscard]] editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry);

}  // namespace eve::action_editor
