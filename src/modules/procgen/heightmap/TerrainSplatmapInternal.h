#pragma once
#include "procgen/heightmap/TerrainSplatmap.h"
namespace eve::procgen {
struct TerrainSplatmap::Impl {
    int                width = 0, height = 0, layers = 0, lastChanged = 0;
    std::vector<float> weights;
    std::size_t        index(int layer, int x, int y) const {
        return (static_cast<std::size_t>(layer) * height + y) * width + x;
    }
};
}  // namespace eve::procgen
