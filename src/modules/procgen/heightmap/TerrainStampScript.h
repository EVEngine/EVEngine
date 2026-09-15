#pragma once

namespace ssq {
class Table;
}
namespace eve::procgen {
/** @brief Register heightmap and stamp value bindings; VM-thread only, retains no table reference. */
void exposeHeightmap(ssq::Table& table);
}  // namespace eve::procgen
