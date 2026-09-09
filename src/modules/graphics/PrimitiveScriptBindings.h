#pragma once

namespace ssq { class Table; class Class; }
namespace eve::graphics {
/** @brief Registers owning primitive proxies on the VM owner thread; retains no table references. */
void exposePrimitiveScriptBindings(ssq::Table& table, ssq::Class& cls);
}
