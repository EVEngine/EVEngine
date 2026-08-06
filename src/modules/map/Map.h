#pragma once

#include "common/Module.h"
#include "map/TileLayer.h"

#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::map {

/**
 * Map module — factory + script binding for 2D tilemaps.
 * Per-frame: TileConfigSystem (hot reload) → TileRenderSystem.
 */
class Map : public Module {
public:
    Module_REG(Map);
    Map() = default;
    ~Map() override = default;

    TileLayer *newLayer(int mapW, int mapH, float tileW = 32.f, float tileH = 32.f);

    /**
     * Load map JSON (Tiled-compatible orthogonal subset or EVEngine simplified format).
     * Creates one TileLayer per tile layer; returns the first (nullptr on failure).
     */
    TileLayer *newLayerFromFile(const std::string &path);

    /** Same as newLayerFromFile but returns how many layers were created (0 on failure). */
    int loadFromFile(const std::string &path);

    void update(float dt);
    void render(graphics::Graphics *gfx);
    int pollConfigs();

    int getLayerCount() const;
};

}  // namespace eve::map
