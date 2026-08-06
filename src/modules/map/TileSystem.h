#pragma once

namespace eve::graphics {
class Graphics;
}

namespace eve::map {

/** Draws all visible TileLayer entities via Graphics batch path (UV atlas quads). */
class TileRenderSystem {
public:
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
