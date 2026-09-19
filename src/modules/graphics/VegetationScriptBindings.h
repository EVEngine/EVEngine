#pragma once
namespace ssq {
class Table;
class Class;
}  // namespace ssq
namespace eve::graphics {
/** @brief Register vegetation field bindings on the VM owner thread.
 * Borrows tables for this call; no script callbacks or retained table references.
 */
void exposeVegetationScriptBindings(ssq::Table& table, ssq::Class& graphicsClass);
}  // namespace eve::graphics
