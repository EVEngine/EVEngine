#pragma once

#include "common/Module.h"
#include "decal/DecalManager.h"

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::decal {

/**
 * @brief Runtime decal module — project surface effects (blood, dirt, scorch,
 * dents via normal maps, ...) onto any world geometry.
 *
 * Script: `decal <- eve.Decal();`
 *
 * Rendering: screen-space box-projected decals. The module registers a
 * RenderSystem3D decal drawer; the "decal" RenderControl feature gates the
 * pass (see setEnabled). `decal.update(dt)` must be called each frame to
 * advance fades and evict expired instances.
 */
class Decal : public Module {
public:
    Module_REG(Decal);
    Decal();
    ~Decal() override = default;

    /** @brief Spawn a decal at (x,y,z) facing along (nx,ny,nz); returns id. */
    int project(float x, float y, float z, float nx, float ny, float nz,
                graphics::Texture *albedo, const std::string &kind, float size, float depth,
                bool randomYaw, int seed, float fadeIn, float lifetime, float fadeOut);

    /** @brief Per-channel blend strengths for a live decal (0 disables). */
    bool setStrength(int id, float normalStrength, float roughnessStrength, float metalStrength,
                     float emissiveStrength);
    bool remove(int id);
    void clearAll();
    int count();
    void setLimit(const std::string &kind, int limit);
    /** @brief Advance ages / evict expired; call once per frame. */
    void update(float dt);
    /** @brief Toggle the graphics "decal" feature on the given backend. */
    void setEnabled(graphics::Graphics *gfx, bool enabled);
};

}  // namespace eve::decal
