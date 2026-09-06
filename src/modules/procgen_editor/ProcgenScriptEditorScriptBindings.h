#pragma once

/** @file ProcgenScriptEditorScriptBindings.h @brief Squirrel adapter for the procgen script editor. */

namespace ssq {
class Class;
class Table;
}  // namespace ssq

namespace eve::procgen_editor {

/**
 * @brief Expose the script-owned ProcgenScriptEditor factory on ProcgenEditorModule.
 * @param table Engine root table that owns the registered proxy class.
 * @param moduleClass Existing ProcgenEditorModule script class that receives create().
 * @remarks Created objects are owner-thread-only and destroyed by the VM release hook.
 */
void exposeProcgenScriptEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass);

}  // namespace eve::procgen_editor
