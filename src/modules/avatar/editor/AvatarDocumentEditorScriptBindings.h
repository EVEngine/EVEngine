#pragma once

/** @file AvatarDocumentEditorScriptBindings.h @brief Squirrel adapter for the avatar document editor. */

namespace ssq {
class Class;
class Table;
}  // namespace ssq

namespace eve::avatar_editor {

/**
 * @brief Expose the script-owned AvatarDocumentEditor factory on AvatarEditorModule.
 * @param table Engine root table that owns the registered proxy class.
 * @param moduleClass Existing AvatarEditorModule script class that receives create().
 * @remarks Created objects are owner-thread-only and destroyed by the VM release hook.
 */
void exposeAvatarDocumentEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass);

}  // namespace eve::avatar_editor
