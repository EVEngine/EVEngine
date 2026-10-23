#pragma once
#include "common/Export.h"
namespace ssq {
class Table;
class Class;
}  // namespace ssq
namespace eve::graphics {
/** @brief Register font and popup script types on the VM owner thread; retain no table references. */
EVENGINE_API_BACKENDS void exposeFontAndDamageNumberBindings(ssq::Table& table, ssq::Class& graphicsClass);
}  // namespace eve::graphics
