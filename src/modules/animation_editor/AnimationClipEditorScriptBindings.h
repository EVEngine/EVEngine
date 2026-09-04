#pragma once

/** @file AnimationClipEditorScriptBindings.h @brief Squirrel adapter for the animation clip editor. */

namespace ssq {
class Class;
class Table;
}  // namespace ssq

namespace eve::animation_editor {

/**
 * @brief Expose the script-owned AnimationClipEditor factory on AnimationEditorModule.
 * @param table Engine root table that owns the registered proxy class.
 * @param moduleClass Existing AnimationEditorModule script class that receives create().
 * @remarks Created objects are owner-thread-only and destroyed by the VM release hook.
 */
void exposeAnimationClipEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass);

}  // namespace eve::animation_editor
