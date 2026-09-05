#pragma once

/** @file VoxelCatalogEditorScriptBindings.h @brief Squirrel adapter for the MagicaVoxel-style sculpt editor. */

namespace ssq {
class Class;
class Table;
}  // namespace ssq

namespace eve::voxel_editor {

/**
 * @brief Expose the script-owned VoxelCatalogEditor factory on VoxelEditorModule.
 * @param table Engine root table that owns the registered proxy class.
 * @param moduleClass Existing VoxelEditorModule script class that receives create().
 * @remarks Created objects are owner-thread-only and destroyed by the VM release hook.
 */
void exposeVoxelCatalogEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass);

}  // namespace eve::voxel_editor
