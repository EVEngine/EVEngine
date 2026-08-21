#pragma once

#include <vector>
#include "map/TileLayer.h"

namespace eve::graphics {
struct DrawItem2D;
}

#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::map {

/** @brief Draws all visible TileLayer entities via Graphics batch path (UV atlas quads). */
class TileRenderSystem {
public:
    /** @brief Append non-empty visible tiles into a shared 2D draw queue. */
    static void collect(std::vector<graphics::DrawItem2D> &out);

    /** @brief Collect + draw tiles only (no present). Null gfx is a no-op. */
    static void render(graphics::Graphics *gfx);
};

/**
 * @brief Polls bound config files (Resource.path) and hot-reloads when modtime changes.
 * Returns number of layers reloaded.
 */
class TileConfigSystem {
public:
    static int poll();
};

}  // namespace eve::map
