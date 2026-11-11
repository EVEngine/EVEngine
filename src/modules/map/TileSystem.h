#pragma once

#include <span>
#include <vector>
#include "common/Result.h"
#include "map/TileLayer.h"

namespace eve::graphics {
struct DrawItem2D;
}

#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::map {
struct TileRenderViewport;

/** @brief Draws all visible TileLayer entities via Graphics batch path (UV atlas quads). */
class EVENGINE_API_WORLD TileRenderSystem {
public:
    /** @brief Advance TileSet animation clocks once per map update. */
    static void update(float dt);
    /** @brief Append non-empty visible tiles into a shared 2D draw queue. */
    static void collect(std::vector<graphics::DrawItem2D> &out);
    /** @brief Viewport-aware collection that rejects off-screen chunks before scanning cells. */
    static void collect(std::vector<graphics::DrawItem2D> &out, int viewWidth, int viewHeight);

    /** @brief Append selected live layers using an explicit camera for both chunk culling and final draw items.
     * @return Success or invalid camera/stale, foreign-world or duplicate layer error before changing output/counters.
     * @ownership Borrows input handles/view and appends frame-local draw items. Texture/canvas pointers remain
     * borrowed from live renderer resources; consume before map/graphics mutation, never retain across frames.
     * Caller retains the original current Table owner and reacquires input handles after a world reset.
     * @thread Current map ECS owner thread outside structural mutation; invokes no external callbacks.
     * @remarks Scope is selected TileLayer entities in the current Table; View reads Config/Tiles/Tileset/Draw,
     * writes only frame-local output and existing last-pass counters. No structural mutation, events or services;
     * runs in the presentation phase and never advances animation/simulation time.
     * @remarks Empty selection draws nothing. Does not read/mutate layer cameras; legacy collect uses its existing
     * per-layer cameras. Allocation failure may leave an appended prefix, but changes no layer or source facts.
     * @cost Scan current layer metadata plus visible selected chunk cells per collection; output allocation scales
     * with emitted draw items. Selection membership is linear in selected layer count.
     * @throws std::bad_alloc Frame-local output/work allocation failed without source mutation.
     */
    [[nodiscard]] static Result<void> collectViewport(std::vector<graphics::DrawItem2D> &out,
                                                      std::span<const ecs::EntityHandle> layers,
                                                      const TileRenderViewport          &viewport);

    /** @brief Collect + draw tiles only (no present). Null gfx is a no-op. */
    static void render(graphics::Graphics *gfx);

    /** @brief Number of non-empty visible tiles collected by the last pass. */
    static int lastVisibleTileCount();
    /** @brief Number of collected tiles using irregular per-GID metadata. */
    static int lastCustomVisualCount();
    /** @brief Number of distinct atlas textures referenced by the last pass. */
    static int lastAtlasCount();
    /** @brief Spatial-index work counters for deterministic performance regressions. */
    static int lastVisitedChunkCount();
    /** @brief Last visited cell count. */
    static int lastVisitedCellCount();
};

/**
 * @brief Polls bound config files (Resource.path) and hot-reloads when modtime changes.
 * Returns number of layers reloaded.
 */
class TileConfigSystem {
public:
    /** @brief Polls . */
    static int poll();
};

}  // namespace eve::map
