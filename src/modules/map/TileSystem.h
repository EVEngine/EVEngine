#pragma once

#include "map/TileLayer.h"
#include "graphics/DrawItem2D.h"

#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::map {

/** Draws all visible TileLayer entities via Graphics batch path (UV atlas quads). */
class TileRenderSystem {
public:
    /** Append non-empty visible tiles into a shared 2D draw queue. */
    static void collect(std::vector<graphics::DrawItem2D> &out);

    /** Collect + draw tiles only (no present). Null gfx is a no-op. */
    static void render(graphics::Graphics *gfx);
};

/**
 * Polls bound config files (Resource.path) and hot-reloads when modtime changes.
 * Returns number of layers reloaded.
 */
class TileConfigSystem {
public:
    static int poll();
};

}  // namespace eve::map
