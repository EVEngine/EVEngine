#pragma once

#include "map/TileLayer.h"

#include <array>
#include <cstdint>
#include <string>

namespace eve::map {

/**
 * Dual-grid autotile (Oskar Stålberg style).
 *
 * Logic cells are painted on the world grid; display tiles sit on a second grid
 * offset by half a step in the active projection, so each display cell samples
 * exactly four logic cells (corners). That yields a 4-bit mask → 15 tiles (+ empty).
 *
 * Works with Orthogonal / Isometric / Staggered / Hexagonal: the corner mask is
 * computed in tile-index space; the display layer inherits orientation params and
 * receives a projection-correct half-step origin offset.
 *
 * Tiled does not provide this offset dual-grid workflow natively. Recommended
 * authoring path: paint a binary logic tile layer in Tiled (or at runtime), then
 * call resolveDualGrid() to fill a display TileLayer. Tiled's Terrain / Wang
 * Corner Set is a related but different same-grid matching system.
 */

struct DualGridOptions {
    /** 0 = any non-zero logic GID counts as filled; else only this GID. */
    int filledGid = 0;
    /**
     * GID of atlas local id 0 on the display tileset.
     * 0 = use display->getTilesetFirstGid() (fallback 1).
     */
    int firstDisplayGid = 0;
    /** Apply projection-correct half-step origin offset on the display layer. */
    bool applyHalfOffset = true;
    /** Hide the logic layer after resolve (logic stays for gameplay queries). */
    bool hideLogic = true;
    /**
     * When true, use the default SpriteCook-style 4x4 atlas frame table.
     * When false, GID = firstDisplayGid + mask (mask 0 → empty).
     */
    bool useDefaultFrameTable = true;
};

/** Pack four corner occupancy bits: TL=1, TR=2, BL=4, BR=8. */
inline int dualGridMaskFromCorners(bool tl, bool tr, bool bl, bool br) {
    return (tl ? 1 : 0) | (tr ? 2 : 0) | (bl ? 4 : 0) | (br ? 8 : 0);
}

/**
 * Default 4x4 atlas frame index for mask 0..15.
 * -1 means draw nothing. Matches the SpriteCook / common dual-grid sheet layout.
 */
int dualGridDefaultFrame(int mask);

/** Copy of the default frame table (16 entries). */
const std::array<int, 16> &dualGridDefaultFrameTable();

/**
 * Half-step origin delta for the display layer (added to logic origin).
 * Orthogonal: (-tileW/2, -tileH/2)
 * Isometric:  (0, -tileH/2)  — equivalent to logic coords (tx-0.5, ty-0.5)
 * Staggered/Hex Y: (-tileW/2, -pitchY/2)
 * Staggered/Hex X: (-pitchX/2, -tileH/2)
 */
void dualGridHalfOffset(const TileLayer::Config &cfg, float &offX, float &offY);

/** Whether logic cell (tx,ty) is filled under options. Out of bounds → false. */
bool dualGridLogicFilled(TileLayer &logic, int tx, int ty, int filledGid);

/**
 * 4-bit corner mask for display cell (dx,dy) on a (logicW+1)×(logicH+1) grid.
 * Samples logic (dx-1,dy-1), (dx,dy-1), (dx-1,dy), (dx,dy) in index space
 * (orientation-independent topology).
 */
int dualGridMaskAt(TileLayer &logic, int dx, int dy, int filledGid = 0);

/**
 * Resolve logic → display dual-grid tiles.
 * Resizes display to (logicW+1)×(logicH+1), copies tile size / orientation /
 * stagger / hex from logic, optionally applies projection-correct half-step
 * origin offset, writes GIDs.
 * Returns false and sets error on invalid args.
 */
bool resolveDualGrid(TileLayer *logic, TileLayer *display, const DualGridOptions &opts,
                     std::string *error = nullptr);

/** Convenience: any non-zero filled, default frame table, half offset, hide logic. */
bool resolveDualGrid(TileLayer *logic, TileLayer *display, std::string *error = nullptr);

}  // namespace eve::map
