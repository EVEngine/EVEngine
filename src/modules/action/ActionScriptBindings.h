#pragma once

/** @file ActionScriptBindings.h @brief Squirrel bindings for eve.Action / ActionRuntime. */

namespace ssq {
class Class;
class Table;
}  // namespace ssq

namespace eve::action {

/**
 * @brief Register `eve.Action` and the script-owned `ActionRuntime` class.
 * @param table Engine root table that owns the registered classes.
 * @remarks Runtime instances are created only through `Action.newRuntime()`;
 *          the default script constructor returns null and bindings reject it.
 */
void exposeActionScriptBindings(ssq::Table& table);

/**
 * @brief Register module methods on an already-created `Action` script class.
 * @param cls Existing Action module class handle.
 */
void exposeActionScriptBindings(ssq::Class& cls);

}  // namespace eve::action
