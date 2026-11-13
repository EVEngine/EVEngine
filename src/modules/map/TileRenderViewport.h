#pragma once
/** @file TileRenderViewport.h @brief Explicit transient tile camera facts; no ECS camera mutation or persistence. */
namespace eve::map {
/** @brief Pixel viewport dimensions and map-pixel camera center/positive zoom for one collection pass. */
struct TileRenderViewport {
    int   width = 0, height = 0;
    float centerX = 0, centerY = 0, zoom = 1;
};
}  // namespace eve::map
