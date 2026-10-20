#pragma once
#include <map>
#include "procgen/heightmap/TerrainDetailLayer.h"
namespace eve::procgen {
struct TerrainDetailLayer::Impl {
    int                                       width = 0, height = 0;
    std::vector<int>                          counts;
    std::map<std::uint64_t, std::vector<int>> resources;
    Result<int>                               rebuildTotals();
};
}  // namespace eve::procgen
