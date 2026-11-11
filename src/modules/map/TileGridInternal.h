#pragma once
// Internal canonical spatial-index builder shared by live tile edits and detached restore attempts.
#include "map/TileLayer.h"
namespace eve::map::detail {
/** @brief Rebuild derived chunk counts and advance the supplied tile revision without retaining pointers.
 * @ownership Synchronous owning-buffer mutation; caller stages a copy when failure atomicity is required.
 * @cost Linear grid traversal and chunk allocation per explicit rebuild, no callbacks or ECS mutation.
 */
void rebuildTileChunks(TileLayer::Tiles& tiles, int width, int height);
}  // namespace eve::map::detail
