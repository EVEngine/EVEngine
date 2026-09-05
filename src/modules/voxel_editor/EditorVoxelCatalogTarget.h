#pragma once

// Compatibility facade. Canonical implementation is owned by voxel_editing.
#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"
#include "voxel_editing/VoxelCatalog.h"

namespace eve::editor {
using voxel_editing::VoxelCatalogTarget;
using voxel_editing::VoxelCellFill;
using voxel_editing::VoxelModelValue;
using voxel_editing::VoxelPick;
using voxel_editing::VoxelSocket;
using voxel_editing::VoxelSocketKind;
}  // namespace eve::editor
