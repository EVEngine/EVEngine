#pragma once

#include <string>

namespace eve::graphics {

class Graphics;
class Texture;

/**
 * Screen-space buffers for mid/post effects (AO, fog, stylize outline, …).
 *
 * Layout after a successful G-buffer pass:
 *   depth  — RGBA8, R = linear depth 0..1 (near..far), same convention as
 *            Volumetric::newLinearDepthTexture / AmbientOcclusion
 *   normal — RGBA8, RGB = world normal * 0.5 + 0.5
 *   albedo — optional RGBA8 (feature "gbufferAlbedo"); may be null
 *
 * Shadow maps stay on the CSM path (Graphics shadow pass); query via
 * hasBuffer("shadow") on RenderControl rather than a Texture* here.
 *
 * Textures are owned by the Graphics backend for the active frame size.
 */
class GBuffer {
public:
    GBuffer() = default;
    ~GBuffer() = default;

    GBuffer(const GBuffer &) = delete;
    GBuffer &operator=(const GBuffer &) = delete;

    void attach(Graphics *gfx) { gfx_ = gfx; }
    Graphics *getGraphics() const { return gfx_; }

    bool isValid() const;
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    Texture *getDepthTexture() const { return depth_; }
    Texture *getNormalTexture() const { return normal_; }
    Texture *getAlbedoTexture() const { return albedo_; }

    /** "depth" | "normal" | "albedo" */
    bool hasBuffer(const std::string &name) const;
    Texture *getBuffer(const std::string &name) const;

    /** Called by Graphics after a G-buffer pass (or clear). */
    void setTargets(int width, int height, Texture *depth, Texture *normal, Texture *albedo);

    void clear();

private:
    Graphics *gfx_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    Texture *depth_ = nullptr;
    Texture *normal_ = nullptr;
    Texture *albedo_ = nullptr;
};

}  // namespace eve::graphics
