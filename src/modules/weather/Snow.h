#pragma once
#include "common/Export.h"


#include "common/Module.h"
#include "weather/SnowField.h"

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::procgen {
class Heightmap;
}  // namespace eve::procgen

namespace eve::weather {

/**
 * @brief out(x, y) = terrain(x, y) + field(x, y) * heightScale.
 *
 * Combines the base terrain heightmap with the snow depth layer, producing the
 * final displaced surface used for mesh rebuilds. `out` is resized to match
 * `terrain`; values are not clamped (the terrain mesh scales them).
 */
EVENGINE_API_ORCHESTRATION void applySnowToHeightmap(const SnowField &field, const procgen::Heightmap &terrain,
                                                     procgen::Heightmap &out, float heightScale);

/**
 * @brief Interactive snow service owned by the weather module.
 *
 * The SnowField grid is the single source of truth: it drives the real
 * terrain-surface displacement (footprints / craters as actual geometry via
 * applySnowToHeightmap + heightmapTargets.updateMesh) and the POM height map
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

    /**
     * @brief New empty snow field.
     * @ownership Ownership transfers to the script/native caller.
     * @lifetime Valid until the caller releases the field; the weather module does not retain it.
     * @thread Main/script thread only.
     */
    SnowField *newField(int width, int height);

    /**
     * @brief Compatibility script wrapper around applySnowToHeightmap().
     * @ownership All pointers are borrowed; no argument is retained.
     * @lifetime Arguments must remain valid for this synchronous call.
     * @thread Main/script thread only.
     */
    bool applyToHeightmap(SnowField *field, procgen::Heightmap *terrain,
                          procgen::Heightmap *out, float heightScale);

    /**
     * @brief Upload the field as an RGBA8 texture.
     * @param kind "height" (R = snow depth, POM height map), "albedo"
     * (snow/ground color) or "normal" (tangent-space from the depth gradient).
     * @ownership `field` and `gfx` are borrowed; the returned texture is owned by Graphics.
     * @lifetime Arguments must remain valid for this synchronous call; the result remains valid
     * until its Graphics owner releases resources.
     * @thread Render/main thread only.
     */
    graphics::Texture *uploadTexture(SnowField *field, graphics::Graphics *gfx,
                                     const std::string &kind);

    /**
     * @brief Replace an uploaded snow texture's pixels in place.
     * @param kind one of "height" | "albedo" | "normal" (must match the upload).
     * @return Compatibility scalar; false when dimensions or pixels are invalid.
     * @ownership All pointers are borrowed and are not retained.
     * @lifetime Arguments must remain valid for this synchronous call.
     * @thread Render/main thread only.
     */
    bool updateTexture(SnowField *field, graphics::Texture *texture,
                       graphics::Graphics *gfx, const std::string &kind);
};

}  // namespace eve::weather
