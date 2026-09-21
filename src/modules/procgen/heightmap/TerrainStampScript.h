#pragma once

namespace ssq {
class Table;
}
namespace eve::procgen {
/** @brief Register heightmap and stamp value bindings; VM-thread only, retains no table reference. */
void exposeHeightmap(ssq::Table& table);
/** @brief Register stamp settings and mesh-bake builder; VM-thread only, retains no borrowed input. */
void exposeTerrainStampSettings(ssq::Table& table);
}  // namespace eve::procgen
