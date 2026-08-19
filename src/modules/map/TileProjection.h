#pragma once

#include "map/TileOrientation.h"
#include "map/TileLayer.h"

namespace eve::map {

/** @brief Converts a tile index to world pixels using the layer's projection. */
void tileToWorld(const TileLayer::Config &cfg, int tx, int ty, float &wx, float &wy);
/** @brief Painter's-algorithm depth for the tile under the layer's projection. */
float tileToDepthY(const TileLayer::Config &cfg, int tx, int ty);
/** @brief Converts world pixels back to the containing tile index. */
void worldToTile(const TileLayer::Config &cfg, float wx, float wy, int &tx, int &ty);

}  // namespace eve::map
