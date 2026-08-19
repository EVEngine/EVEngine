#pragma once

#include <string>

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/**
 * @brief Screen-space model outline (t3ssel8r-style), computed from the GBuffer
 * hardware depth + world-normal buffers.
 *
 * Technique: for every pixel sample the depth (Vulkan NDC z, binding 0) and
 * world normal (RGB = n * 0.5 + 0.5, binding 1) of an 8-neighbourhood at a
 * configurable width. A pixel becomes an outline when the view-space depth
 * discontinuity (silhouette / occlusion edge) or the normal discontinuity
 * (crease) exceeds a threshold. The pass emits `vec4(outlineColor, edgeMask)`
 * which is blended (SrcAlpha) over the already-rendered scene, so applying
 * it on top of a 3D frame yields crisp ink outlines without a separate
 * composite target.
 *
 * Pipeline: single screen-space draw reading depth + normal. Feed it the
 * GBuffer textures from RenderControl::getGBuffer() (getHwDepthTexture() /
 * getNormalTexture()) after the 3D geometry pass, with the scene still
 * bound as the current canvas. See AmbientOcclusion::applyFromGBuffer for
 * the same binding convention.
 */
class Outline {
public:
    explicit Outline(Graphics *gfx);
    ~Outline();

    Outline(const Outline &) = delete;
    Outline &operator=(const Outline &) = delete;

    void setColor(float r, float g, float b);
    float getColorR() const;
    float getColorG() const;
    float getColorB() const;

    /** @brief Outline thickness in screen pixels (>= 0.5). */
    void setWidth(float width);
    float getWidth() const;

    /** @brief View-space depth discontinuity that starts a depth edge. */
    void setDepthThreshold(float threshold);
    float getDepthThreshold() const;

    /** @brief Extra per-unit-distance depth tolerance (keeps outlines distance-consistent). */
    void setDepthSensitivity(float sensitivity);
    float getDepthSensitivity() const;

    /** @brief Normal discontinuity (1 - dot(n, nN)) that starts a crease edge. */
    void setNormalThreshold(float threshold);
    float getNormalThreshold() const;

    /** @brief Smoothstep band used to fade edges (0 = hard, 1 = soft). */
    void setSoftness(float softness);
    float getSoftness() const;

    /** @brief Near/far used to linearize the hardware depth. */
    void setClip(float nearZ, float farZ);

    bool hasParam(const std::string &name) const;
    void setFloat(const std::string &name, float value);
    float getFloat(const std::string &name) const;

    /**
     * @brief Draw the outline over the currently bound canvas / screen.
     * `hwDepth` is the D32 GBuffer (Vulkan NDC z), `worldNormal` the GBuffer
     * world normal (RGBA8, n * 0.5 + 0.5). Automatically uploads texel size
     * and clip uniforms. Returns false if either input is missing.
     */
    bool apply(Graphics *gfx, Texture *hwDepth, Texture *worldNormal);
    bool applyTo(Graphics *gfx, Texture *hwDepth, Texture *worldNormal, Canvas *dest);

    Shader *getShader() const { return shader_; }

private:
    void uploadCommon(Graphics *gfx, Texture *hwDepth);

    Graphics *gfx_ = nullptr;
    Shader *shader_ = nullptr;
    float nearZ_ = 0.1f;
    float farZ_ = 100.f;
    float colorR_ = 0.05f;
    float colorG_ = 0.04f;
    float colorB_ = 0.07f;
    float width_ = 1.f;
    float depthThreshold_ = 0.3f;
    float depthSensitivity_ = 0.f;
    float normalThreshold_ = 0.35f;
    float softness_ = 0.15f;
};

}  // namespace eve::graphics
