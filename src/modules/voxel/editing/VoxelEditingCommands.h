#pragma once
#include "common/Export.h"

#include "editing/EditingCommandRegistry.h"

#include <vector>

namespace eve::voxel_editing {

/** @brief Metadata for every Voxel planned command (shared with EditorPackage). */
[[nodiscard]] EVENGINE_API_ORCHESTRATION std::vector<editing::EditingCommandDescriptor>
                                         voxelEditingCommandDescriptors();

/** @brief Register voxel-catalog planned commands with a generic editing host. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION editing::Result<void> registerEditingCommands(
    editing::IEditingCommandRegistry& registry);

}  // namespace eve::voxel_editing
