#pragma once
namespace ssq {
class Table;
}
namespace eve::graphics {
/** @brief Register mesh script bindings; table is borrowed synchronously. */
void exposeGraphicsMeshes(ssq::Table& table);
}  // namespace eve::graphics
