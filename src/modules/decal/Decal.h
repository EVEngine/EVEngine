#pragma once

#include "common/Module.h"
#include "decal/DecalManager.h"

#include <cstdint>
#include <string>

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
    /** @brief Atlas region [x, y, w, h] (normalized) for a live decal. */
    bool setUvRect(int id, float x, float y, float w, float h);
    /** @brief Optional normal / params (roughness, metallic, emissive) textures. */
    bool setTextures(int id, graphics::Texture *normal, graphics::Texture *params);
    /** @brief Blend mode: "over" (default) or "add" (emissive). */
    bool setBlend(int id, const std::string &mode);
    /**
     * @brief Projection: "planar", "triplanar", "spherical", or world-aligned "world".
     * @param blendSharpness Triplanar blend exponent (typical 2–10; default 4).
     * @return DecalProjectionStatus::Applied on success.
     */
    [[nodiscard]] DecalProjectionStatus setProjection(int id, const std::string &mode,
                                                      float blendSharpness);
    /** @brief Configure params-alpha POM; scale zero disables it. */
    [[nodiscard]] DecalParallaxStatus setParallax(int id, float scale, float minLayers,
                                                 float maxLayers);
    /** @brief Configure normalized edge-mask feather width in [0, 0.49]. */
    [[nodiscard]] DecalEdgeFadeStatus setEdgeFade(int id, float width);
    bool remove(int id);
    void clearAll();
    int count();
    void setLimit(const std::string &kind, int limit);
    /** @brief Advance ages / evict expired; call once per frame. */
    void update(float dt);
    /** @brief Toggle the graphics "decal" feature on the given backend. */
    void setEnabled(graphics::Graphics *gfx, bool enabled);

    /**
     * @brief Return the stable comma-separated Procedural Decal preset names.
     * @return Value-owned UTF-8 text suitable for UI menus and EveScript discovery.
     */
    [[nodiscard]] std::string proceduralPresets() const;

    /**
     * @brief Bake one channel of a named Procedural Decal preset and upload it.
     * @param gfx Graphics service that owns the returned texture.
     * @param preset Preset listed by proceduralPresets().
     * @param seed Deterministic authoring seed.
     * @param resolution Square output size in the inclusive range 1..4096.
     * @param channel One of "albedo", "normal", or "params".
     * @return Borrowed texture owned by gfx; it remains valid according to the graphics resource lifetime.
     * @cost Linear in pixel count and blur radius; bake during load or authoring, never per frame.
     * @throws eve::Exception when arguments or the recipe are invalid.
     * @thread Must run on the graphics service's resource-creation thread.
     * @reentrancy Does not invoke scripts or callbacks.
     */
    [[nodiscard]] graphics::Texture *bakePresetTexture(graphics::Graphics *gfx,
                                                       const std::string &preset,
                                                       std::uint32_t seed, int resolution,
                                                       const std::string &channel);

    /**
     * @brief Import a Procedural Decal `.sbsprs` document, bake it, and upload one channel.
     * @param gfx Graphics service that owns the returned texture.
     * @param xml UTF-8 Substance preset document.
     * @param resolution Square output size in the inclusive range 1..4096.
     * @param channel One of "albedo", "normal", or "params".
     * @return Borrowed texture owned by gfx.
     * @cost Linear in XML size, pixel count, and blur radius; admission/loading only.
     * @throws eve::Exception when import, bake, channel selection, or upload fails.
     * @thread Must run on the graphics service's resource-creation thread.
     * @reentrancy Does not invoke scripts or callbacks.
     */
    [[nodiscard]] graphics::Texture *bakeSbsprsTexture(graphics::Graphics *gfx,
                                                       const std::string &xml, int resolution,
                                                       const std::string &channel);
};

/** @brief Register the IDecalQuery capability (implemented in DecalCapabilities.cpp). */
void registerDecalCapabilities();

}  // namespace eve::decal
