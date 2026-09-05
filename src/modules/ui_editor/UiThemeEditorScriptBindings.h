#pragma once

/** @file UiThemeEditorScriptBindings.h @brief Squirrel adapter for the UI theme editor. */

namespace ssq {
class Class;
class Table;
}  // namespace ssq

namespace eve::ui_editor {

/**
 * @brief Expose the script-owned UiThemeEditor factory on UiEditorModule.
 * @param table Engine root table that owns the registered proxy class.
 * @param moduleClass Existing UiEditorModule script class that receives create().
 * @remarks Created objects are owner-thread-only and destroyed by the VM release hook.
 */
void exposeUiThemeEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass);

}  // namespace eve::ui_editor
