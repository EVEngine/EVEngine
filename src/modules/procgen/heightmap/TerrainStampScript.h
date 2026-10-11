#pragma once
#include "common/Export.h"


namespace ssq {
class Table;
}
namespace eve::procgen {
/** @brief Register heightmap and stamp value bindings; VM-thread only, retains no table reference. */
EVENGINE_API_DOMAINS void exposeHeightmap(ssq::Table& table);
/** @brief Register stamp settings and mesh-bake builder; VM-thread only, retains no borrowed input. */
EVENGINE_API_DOMAINS void exposeTerrainStampSettings(ssq::Table& table);
}  // namespace eve::procgen
