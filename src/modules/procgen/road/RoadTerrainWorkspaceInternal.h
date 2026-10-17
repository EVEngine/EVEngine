#pragma once
#include "procgen/road/RoadTerrainWorkspace.h"

namespace eve::procgen::road {
struct RoadTerrainWorkspace::Impl {
    using Frame = std::vector<RoadTerrainTile>;
    std::vector<Frame> history;
    std::size_t        cursor = 0;
};
namespace detail {
/** @brief Validate bounded independent channel topologies, normalization and stable resource identities. */
[[nodiscard]] Result<void> validateTerrainTiles(const std::vector<RoadTerrainTile>& tiles);
}  // namespace detail
}  // namespace eve::procgen::road
