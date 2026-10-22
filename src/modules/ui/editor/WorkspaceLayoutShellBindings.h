#pragma once

/** @file WorkspaceLayoutShellBindings.h @brief Squirrel adapters for WorkspaceLayoutShell. */

namespace ssq {
class Class;
class Table;
}  // namespace ssq

namespace eve::ui_editor {

/**
 * @brief Expose WorkspaceLayoutShell helpers on UiEditorModule.
 * @param table Engine root table that owns registered proxy classes.
 * @param moduleClass Existing UiEditorModule script class.
 */
void exposeWorkspaceLayoutShellBindings(ssq::Table& table, ssq::Class& moduleClass);

}  // namespace eve::ui_editor
