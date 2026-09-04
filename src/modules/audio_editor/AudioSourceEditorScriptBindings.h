#pragma once

/** @file AudioSourceEditorScriptBindings.h @brief Squirrel adapter for the audio source editor. */

namespace ssq {
class Class;
class Table;
}  // namespace ssq

namespace eve::audio_editor {

/**
 * @brief Expose the script-owned AudioSourceEditor factory on AudioEditorModule.
 * @param table Engine root table that owns the registered proxy class.
 * @param moduleClass Existing AudioEditorModule script class that receives create().
 * @remarks Created objects are owner-thread-only and destroyed by the VM release hook.
 */
void exposeAudioSourceEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass);

}  // namespace eve::audio_editor
