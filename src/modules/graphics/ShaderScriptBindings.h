#pragma once

namespace ssq {
class Table;
class Class;
}  // namespace ssq
namespace eve::graphics {
/** @brief Register shader loading and binary replacement on the VM owner thread.
 * Borrows both tables for this call only; invokes no script callbacks.
 */
void exposeShaderScriptBindings(ssq::Table& table, ssq::Class& cls);
}  // namespace eve::graphics
