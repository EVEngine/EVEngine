#pragma once

/** @file ActionTimelineScriptBindings.h @brief Squirrel adapter for the canonical action timeline editor. */

namespace ssq {
class Class;
class Table;
}  // namespace ssq

namespace eve::editor {

/**
 * @brief Expose the script-owned action timeline editor adapter.
 * @param table Engine root table that owns the registered proxy class.
 * @param editorClass Existing eve.Editor script class that receives the factory.
 * @remarks Objects created by the factory are owner-thread-only and are destroyed by the VM release hook.
 */
void exposeActionTimelineScriptBindings(ssq::Table& table, ssq::Class& editorClass);

}  // namespace eve::editor
