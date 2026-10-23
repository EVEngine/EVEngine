#pragma once
#include "common/Export.h"


#include <string>

namespace eve::graphics {

class Graphics;
class Texture;

/**
 * @brief Screen-space buffers for mid/post effects and Hybrid deferred lighting.
 *
 * Layout after a successful G-buffer pass (Phase B compat-preserving MRT):
 *   depth       — RGBA8 linear copy (R = linear 0..1; G/B = velocity) for Canvas / volumetric
 *   hwDepth     — D32 hardware depth (sample .r = Vulkan NDC z); 3D AO/GI use this
 *   normal      — RGBA8, RGB = world normal * 0.5 + 0.5; A = legacy 3-bit metal/rough pack
 *   albedo      — RGBA8, RGB = albedo×tint; A = linear depth (SSGI compat)
 *   pbrParams   — RGBA8, R = metallic, G = roughness, B = occlusion, A = specularFactor
 *   emissive    — RGBA8, RGB = emissive×strength (HDR later); A unused
 *
 * Shadow maps stay on the CSM path (Graphics shadow pass); query via
 * hasBuffer("shadow") on RenderControl rather than a Texture* here.
 *
 * Textures are owned by the Graphics backend for the active frame size.
 */
class EVENGINE_API_BACKENDS GBuffer {
public:
    /** @brief G buffer. */
    GBuffer() = default;
    /** @brief G buffer. */
    ~GBuffer() = default;

    GBuffer(const GBuffer &) = delete;
    GBuffer &operator=(const GBuffer &) = delete;

    /** @brief Attaches . */
    void attach(Graphics *gfx) { gfx_ = gfx; }
    /** @brief Returns the graphics. */
    Graphics *getGraphics() const { return gfx_; }

    /** @brief True when valid. */
    bool isValid() const;
    /** @brief Returns the width. */
    int getWidth() const { return width_; }
    /** @brief Returns the height. */
    int getHeight() const { return height_; }

    /** @brief Returns the depth texture. */
    Texture *getDepthTexture() const { return depth_; }
    /** @brief Returns the hw depth texture. */
    Texture *getHwDepthTexture() const { return hwDepth_; }
    /** @brief Returns the normal texture. */
    Texture *getNormalTexture() const { return normal_; }
    /** @brief Returns the albedo texture. */
    Texture *getAlbedoTexture() const { return albedo_; }
    /**
     * @brief Metallic/roughness/occlusion/specularFactor (may be null before Phase B backends wire).
     * @lifetime The returned borrowed texture remains valid while this GBuffer owns its attachments.
     */
    Texture* getPbrParamsTexture() const { return pbrParams_; }
    /**
     * @brief Emissive RGB (may be null before Phase B backends wire).
     * @lifetime The returned borrowed texture remains valid while this GBuffer owns its attachments.
     */
    Texture* getEmissiveTexture() const { return emissive_; }
    /**
     * @brief Packed rigid-object velocity in depth texture G/B (0.5 = zero motion).
     * @lifetime The returned borrowed texture remains valid while this GBuffer owns its attachments.
     */
    Texture *getVelocityTexture() const { return depth_; }

    /** @brief "depth" | "hwDepth" | "normal" | "albedo" | "pbrParams" | "emissive" | "velocity" */
    bool hasBuffer(const std::string &name) const;
    /** @brief Returns the buffer. */
    Texture *getBuffer(const std::string &name) const;

    /**
     * @brief Called by Graphics after a G-buffer pass (or clear).
     * @param pbrParams Optional metallic/roughness/occlusion/specular target.
     * @param emissive Optional emissive target.
     */
    void setTargets(int width, int height, Texture* depth, Texture* normal, Texture* albedo, Texture* hwDepth = nullptr,
                    Texture* pbrParams = nullptr, Texture* emissive = nullptr);

    /** @brief Clears . */
    void clear();

private:
    Graphics *gfx_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    Texture *depth_ = nullptr;
    Texture *hwDepth_ = nullptr;
    Texture *normal_ = nullptr;
    Texture *albedo_ = nullptr;
    Texture*  pbrParams_ = nullptr;
    Texture*  emissive_  = nullptr;
};

}  // namespace eve::graphics
