#pragma once

/** @file BiomeRulesEditorScriptBindings.h @brief Squirrel adapter for the biome rules editor. */

namespace ssq {
class Class;
class Table;
}  // namespace ssq

namespace eve::biome_editor {

/**
 * @brief Expose the script-owned BiomeRulesEditor factory on BiomeEditorModule.
 * @param table Engine root table that owns the registered proxy class.
 * @param moduleClass Existing BiomeEditorModule script class that receives create().
 * @remarks Created objects are owner-thread-only and destroyed by the VM release hook.
 */
void exposeBiomeRulesEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass);

}  // namespace eve::biome_editor
