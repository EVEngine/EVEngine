#pragma once

#include <string>

namespace eve::graphics {

class Graphics;
class Texture;

/**
 * @brief Screen-space buffers for mid/post effects (AO, fog, stylize outline, …).
 *
 * Layout after a successful G-buffer pass:
 *   depth    — RGBA8 linear copy (R = linear 0..1) for Canvas / volumetric
 *   hwDepth  — D32 hardware depth (sample .r = Vulkan NDC z); 3D AO/GI use this
 *   normal   — RGBA8, RGB = world normal * 0.5 + 0.5
 *   albedo   — optional RGBA8 (feature "gbufferAlbedo"); may be null
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
    Texture *getHwDepthTexture() const { return hwDepth_; }
    Texture *getNormalTexture() const { return normal_; }
    Texture *getAlbedoTexture() const { return albedo_; }
    /**
     * @brief Packed rigid-object velocity in depth texture G/B (0.5 = zero motion).
     * @lifetime The returned borrowed texture remains valid while this GBuffer owns its attachments.
     */
    Texture *getVelocityTexture() const { return depth_; }

    /** @brief "depth" | "hwDepth" | "normal" | "albedo" | "velocity" */
    bool hasBuffer(const std::string &name) const;
    Texture *getBuffer(const std::string &name) const;

    /** @brief Called by Graphics after a G-buffer pass (or clear). */
    void setTargets(int width, int height, Texture *depth, Texture *normal, Texture *albedo,
                    Texture *hwDepth = nullptr);

    void clear();

private:
    Graphics *gfx_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    Texture *depth_ = nullptr;
    Texture *hwDepth_ = nullptr;
    Texture *normal_ = nullptr;
    Texture *albedo_ = nullptr;
};

}  // namespace eve::graphics
