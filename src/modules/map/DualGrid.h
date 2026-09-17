#pragma once

#include "common/Result.h"
#include "map/TileLayer.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::map {

/**
 * @brief Dual-grid autotile (Oskar Stålberg style).
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
    /** @brief 0 = any non-zero logic GID counts as filled; else only this GID. */
    int filledGid = 0;
    /**
     * @brief GID of atlas local id 0 on the display tileset.
     * 0 = use display->getTilesetFirstGid() (fallback 1).
     */
    int firstDisplayGid = 0;
    /** @brief Apply projection-correct half-step origin offset on the display layer. */
    bool applyHalfOffset = true;
    /** @brief Hide the logic layer after resolve (logic stays for gameplay queries). */
    bool hideLogic = true;
    /**
     * @brief When true, use the default SpriteCook-style 4x4 atlas frame table.
     * When false, GID = firstDisplayGid + mask (mask 0 → empty).
     */
    bool useDefaultFrameTable = true;
};

/** @brief Deterministic settings for the reusable 16-frame procedural transition-mask atlas. */
struct DualGridMaskConfig {
    int      width         = 128;
    int      height        = 64;
    float    edgeWidth     = 0.08f;
    float    noiseScale    = 5.f;
    float    noiseStrength = 0.08f;
    uint32_t seed          = 0;
};

/**
 * @brief Owning CPU result containing all 16 dual-grid masks and their signed-distance fields.
 *
 * Frames are
 * mask-indexed (0..15), row-major, with normalized alpha in @c coverage and
 * normalized signed distance in @c
 * signedDistance. Positive distance belongs to terrain B.
 * This is renderer-neutral: callers may upload the fields,
 * bake a sprite atlas, or derive
 * coast bands without changing logical terrain state.
 */
struct DualGridMaskAtlas {
    int                width  = 0;
    int                height = 0;
    std::vector<float> coverage;
    std::vector<float> signedDistance;

    /** @brief Read normalized coverage. Invalid coordinates return zero. */
    [[nodiscard]] float coverageAt(int mask, int x, int y) const;
    /** @brief Read normalized signed distance. Invalid coordinates return zero. */
    [[nodiscard]] float signedDistanceAt(int mask, int x, int y) const;
    /** @brief Select a band such as wet sand or foam around the generated boundary. */
    [[nodiscard]] float bandAt(int mask, int x, int y, float center, float halfWidth, float softness = 0.01f) const;
};

/** @brief Owning RGBA8 image used by the renderer-neutral transition-atlas baker. */
struct DualGridRgbaImage {
    int                  width  = 0;
    int                  height = 0;
    std::vector<uint8_t> pixels;
};

/**
 * @brief Generate the complete reusable 16-frame dual-grid mask atlas.
 * @return Owning atlas, or a structured
 * InvalidArgument diagnostic.
 * @cost O(16 * width * height), intended for import/load time and cacheable by config.

 * * @thread Pure CPU work; safe on worker threads. No callbacks are invoked.
 * @determinism Bit-stable for the same
 * config on IEEE-754 implementations.
 */
[[nodiscard]] eve::Result<DualGridMaskAtlas> generateDualGridMaskAtlas(const DualGridMaskConfig& config);

/**
 * @brief Bake two ordinary same-size RGBA8 tiles into a row-major 4x4 transition atlas.
 *
 * Frame @c mask
 * contains @c mix(terrainA, terrainB, generatedCoverage(mask)); mask 0 is
 * terrain A and mask 15 is terrain B. The
 * result can be uploaded as one normal tile atlas,
 * so no authored autotile sprites or runtime custom shader are
 * required.
 * @param terrainA Owning/view snapshot whose byte count must be width*height*4.
 * @param terrainB Same
 * dimensions and color space as terrainA.
 * @param config Mask dimensions; these must match both input tiles.
 *
 * @return Owning 4*width by 4*height RGBA8 atlas, or a structured diagnostic.
 * @cost O(16 * width * height), intended
 * for import/load time and cacheable by content hash.
 */
[[nodiscard]] eve::Result<DualGridRgbaImage> bakeDualGridTransitionAtlas(const DualGridRgbaImage&  terrainA,
                                                                         const DualGridRgbaImage&  terrainB,
                                                                         const DualGridMaskConfig& config);

/** @brief Pack four corner occupancy bits: TL=1, TR=2, BL=4, BR=8. */
inline int dualGridMaskFromCorners(bool tl, bool tr, bool bl, bool br) {
    return (tl ? 1 : 0) | (tr ? 2 : 0) | (bl ? 4 : 0) | (br ? 8 : 0);
}

/**
 * @brief Default 4x4 atlas frame index for mask 0..15.
 * -1 means draw nothing. Matches the SpriteCook / common dual-grid sheet layout.
 */
int dualGridDefaultFrame(int mask);

/** @brief Copy of the default frame table (16 entries). */
const std::array<int, 16>& dualGridDefaultFrameTable();

/**
 * @brief Half-step origin delta for the display layer (added to logic origin).
 * Orthogonal: (-tileW/2, -tileH/2)
 * Isometric:  (0, -tileH/2)  — equivalent to logic coords (tx-0.5, ty-0.5)
 * Staggered/Hex Y: (-tileW/2, -pitchY/2)
 * Staggered/Hex X: (-pitchX/2, -tileH/2)
 */
void dualGridHalfOffset(const TileLayer::Config& cfg, float& offX, float& offY);

/** @brief Whether logic cell (tx,ty) is filled under options. Out of bounds → false. */
bool dualGridLogicFilled(TileLayer& logic, int tx, int ty, int filledGid);

/**
 * @brief 4-bit corner mask for display cell (dx,dy) on a (logicW+1)×(logicH+1) grid.
 * Samples logic (dx-1,dy-1), (dx,dy-1), (dx-1,dy), (dx,dy) in index space
 * (orientation-independent topology).
 */
int dualGridMaskAt(TileLayer& logic, int dx, int dy, int filledGid = 0);

/**
 * @brief Resolve logic → display dual-grid tiles.
 * Resizes display to (logicW+1)×(logicH+1), copies tile size / orientation /
 * stagger / hex from logic, optionally applies projection-correct half-step
 * origin offset, writes GIDs.
 * Returns false and sets error on invalid args.
 */
bool resolveDualGrid(TileLayer* logic, TileLayer* display, const DualGridOptions& opts, std::string* error = nullptr);

/** @brief Convenience: any non-zero filled, default frame table, half offset, hide logic. */
bool resolveDualGrid(TileLayer* logic, TileLayer* display, std::string* error = nullptr);

}  // namespace eve::map
