#pragma once

#include "common/Module.h"
#include "snow/SnowField.h"

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::procgen {
class Heightmap;
}  // namespace eve::procgen

namespace eve::snow {

/**
 * @brief out(x, y) = terrain(x, y) + field(x, y) * heightScale.
 *
 * Combines the base terrain heightmap with the snow depth layer, producing the
 * final displaced surface used for mesh rebuilds. `out` is resized to match
 * `terrain`; values are not clamped (the terrain mesh scales them).
 */
void applySnowToHeightmap(const SnowField &field, const procgen::Heightmap &terrain,
                          procgen::Heightmap &out, float heightScale);

/**
 * @brief Interactive snow module — depth-field snow on a heightmap terrain.
 *
 * The SnowField grid is the single source of truth: it drives the real
 * terrain-surface displacement (footprints / craters as actual geometry via
 * applySnowToHeightmap + editor.updateHeightmapMesh) and the POM height map
 * (uploadTexture / updateTexture + Renderable3D.setHeightTexture/setParallax),
 * plus snowfall recovery (addSnowfall).
 *
 * Script: `snow <- eve.Snow();`
 */
class Snow : public Module {
public:
    Module_REG(Snow);
    Snow();
    ~Snow() override = default;

    /** @brief New empty snow field (caller owns). */
    SnowField *newField(int width, int height);

    /** @brief Script wrapper around applySnowToHeightmap(). */
    bool applyToHeightmap(SnowField *field, procgen::Heightmap *terrain,
                          procgen::Heightmap *out, float heightScale);

    /**
     * @brief Upload the field as an RGBA8 texture.
     * @param kind "height" (R = snow depth, POM height map), "albedo"
     * (snow/ground color) or "normal" (tangent-space from the depth gradient).
     * Borrowed handle owned by Graphics; call once, then updateTexture().
     */
    graphics::Texture *uploadTexture(SnowField *field, graphics::Graphics *gfx,
                                     const std::string &kind);

    /**
     * @brief Replace an uploaded snow texture's pixels in place.
     * @param kind one of "height" | "albedo" | "normal" (must match the upload).
     * Returns false when unsupported (e.g. WebGPU backend).
     */
    bool updateTexture(SnowField *field, graphics::Texture *texture,
                       graphics::Graphics *gfx, const std::string &kind);
};

}  // namespace eve::snow
