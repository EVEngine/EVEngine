#pragma once

#include "editing/EditingCommandRegistry.h"

namespace eve::audio_editing {

/**
 * @brief Register audio-owned planned commands with a generic editing host.
 * @param registry Host-owned command registry that must outlive registered planners.
 * @return Applied when every command is installed; otherwise a structured registration failure.
 * @thread Main-thread composition only.
 * @reentrancy Must not re-enter module registration.
 */
[[nodiscard]] editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry);

}  // namespace eve::audio_editing
