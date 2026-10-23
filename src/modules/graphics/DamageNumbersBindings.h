#pragma once
namespace ssq {
class Table;
class Class;
}  // namespace ssq
namespace eve::graphics {
/** @brief Register font and popup script types on the VM owner thread; retain no table references. */
void exposeFontAndDamageNumberBindings(ssq::Table& table, ssq::Class& graphicsClass);
}  // namespace eve::graphics
