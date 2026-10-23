#pragma once
namespace ssq {
class Table;
class Class;
}  // namespace ssq
namespace eve::daynight {
/** @brief Register independent sky bindings on the VM/graphics owner thread.
 * @details Borrows tables only during registration; callbacks are owned by that VM.
 * No script callback is invoked during registration. */
void exposeSkyScriptBindings(ssq::Table& table, ssq::Class& daynightClass);
}  // namespace eve::daynight
