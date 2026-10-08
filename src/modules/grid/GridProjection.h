#pragma once
#include "common/Export.h"

/**
 * @file GridProjection.h
 * @brief Pure cell/world projection helpers for GridConfig.
 */

#include "grid/GridConfig.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace eve::grid {

/**
 * @brief Maps a cell to planar coordinates (px, py).
 * On XY plane these are world (x, y); on XZ plane they are world (x, z).
 */
EVENGINE_API_FOUNDATION void cellToWorld(const GridConfig &cfg, int cx, int cy, float &px, float &py);

/**
 * @brief 2D sort key (cell foot Y), matching map::tileToDepthY semantics.
 * Unused on the XZ plane.
 */
EVENGINE_API_FOUNDATION float cellToDepthY(const GridConfig &cfg, int cx, int cy);

/**
 * @brief Maps planar coordinates to the nearest cell.
 * Staggered/hex layouts use a bounded nearest-neighbor search; mapW/mapH
 * bound the search range (Tiled-compatible pick semantics).
 */
EVENGINE_API_FOUNDATION void worldToCell(const GridConfig &cfg, float px, float py, int &cx, int &cy, int mapW = 1,
                                         int mapH = 1);

/**
 * @brief Enumerates local cells of a rotated footprint.
 * @param mask Row-major w*h occupancy (empty = solid rectangle).
 * @param steps Cardinal: 90° steps (0..3); hexMode: 60° steps (0..5).
 * Callback receives normalized local coords (bbox min corner at origin, anchor at origin cell).
 */
EVENGINE_API_FOUNDATION void foreachRotatedFootprint(int w, int h, const std::vector<uint8_t> &mask, int steps,
                                                     bool hexMode, const std::function<void(int lx, int ly)> &fn);

/** @brief Axis-aligned size of a footprint after rotation. */
EVENGINE_API_FOUNDATION void rotatedFootprintSize(int w, int h, const std::vector<uint8_t> &mask, int steps,
                                                  bool hexMode, int &outW, int &outH);

}  // namespace eve::grid
