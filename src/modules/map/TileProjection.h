#pragma once

#include "map/TileOrientation.h"
#include "map/TileLayer.h"

namespace eve::map {

void tileToWorld(const TileLayer::Config &cfg, int tx, int ty, float &wx, float &wy);
float tileToDepthY(const TileLayer::Config &cfg, int tx, int ty);
void worldToTile(const TileLayer::Config &cfg, float wx, float wy, int &tx, int &ty);

}  // namespace eve::map
