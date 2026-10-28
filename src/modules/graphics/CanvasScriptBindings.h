#pragma once
namespace ssq { class Table; }
namespace eve::graphics {
/** @brief Register canvas bindings on the VM owner thread; retains no table reference. */
void exposeCanvasScriptBindings(ssq::Table& table);
}
