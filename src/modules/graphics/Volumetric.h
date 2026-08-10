#pragma once

#include <string>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace eve::graphics {

class Canvas;
class Drawable;
class Graphics;
class Mesh;
class Shader;
class Texture;

/**
 * Volumetric light: screen-space god rays (phase 1) + depth ray march (phase 2).
 *
 * Phase 1 — Mitchell radial blur (+ dust/fog) on an occlusion map or scene.
 * Phase 2 — Ray march through a linear-depth texture; screen-space marches
 *           toward the light UV approximate directional CSM occlusion
 *           (single-sampler post path; no extra G-buffer required).
 *
 * Quality presets ("low" | "medium" | "high") control sample count and
 * suggested downscale via resolutionFor().
 */
class Volumetric {
public:
    explicit Volumetric(Graphics *gfx);
    ~Volumetric();

    Volumetric(const Volumetric &) = delete;
    Volumetric &operator=(const Volumetric &) = delete;

    /** "low" | "medium" | "high" (unknown → medium). */
    void setQuality(const std::string &quality);
    std::string getQuality() const { return quality_; }

    /** "screenspace" | "raymarch" — selects which shader params quality tweaks. */
    void setMode(const std::string &mode);
    std::string getMode() const { return mode_; }

    /** Light position in UV (0..1), origin top-left to match 2D UVs. */
    void setLightScreenUV(float u, float v);
    float getLightScreenU() const;
    float getLightScreenV() const;

    /** Pixel-space helper (converts with width/height). */
    void setLightScreenPos(float x, float y, float width, float height);

    /** World-space direction toward the lit surface (ray march / phase). */
    void setLightDirection(float dx, float dy, float dz);

    /**
     * Camera for ray march reconstruction (RH + ZO).
     * Builds inv(viewProj) and near/far used by rayMarch*.
     */
    void setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY, float targetZ,
                   float upX, float upY, float upZ, float fovYDeg, float aspect, float nearZ,
                   float farZ);

    /** Raw inverse view-projection (column-major, matching glm). */
    void setInvViewProj(const glm::mat4 &invViewProj);

    void setShaftColor(float r, float g, float b);
    void setFogColor(float r, float g, float b);
    void setIntensity(float intensity);
    void setTime(float seconds);
    void setDensity(float density);

    bool hasParam(const std::string &name) const;
    void setFloat(const std::string &name, float value);
    float getFloat(const std::string &name) const;

    int getSampleCount() const;
    float getDownscale() const { return downscale_; }

    /**
     * Clear the current canvas to black and draw a bright light disc
     * (start of an occlusion map). Call drawOcclusion* afterward.
     */
    void beginOcclusionMap(Graphics *gfx, float lightPixelX, float lightPixelY,
                           float lightRadiusPixels = 24.f);

    /** Drawable occlusion (respects Drawable::getCastOcclusion). */
    void drawOccluder(Graphics *gfx, Drawable *drawable, const glm::mat4 &matrix);

    /** Convenience 2D black silhouette (solid or textured alpha). */
    void drawOccluderSolid(Graphics *gfx, float x, float y, float w, float h);
    void drawOccluderTexture(Graphics *gfx, Texture *texture, float x, float y, float w, float h);

    /**
     * Scatter-only: occlusion → shafts with alpha (draw over a prior scene).
     * Writes to the currently bound canvas / screen.
     */
    void scatter(Graphics *gfx, Texture *occlusion);

    /** Same as scatter but into an explicit destination. */
    void scatterTo(Graphics *gfx, Texture *occlusion, Canvas *dest);

    /**
     * Single-pass: treat source as the scene (bright regions ≈ light),
     * add shafts + dust/fog onto it. Writes to current canvas.
     */
    void applyFromScene(Graphics *gfx, Texture *scene);
    void applyFromSceneTo(Graphics *gfx, Texture *scene, Canvas *dest);

    /**
     * Ray march participating media using a linear-depth texture (R channel,
     * 0=near .. 1=far). Screen-space steps toward light UV approximate CSM
     * occlusion. Call setCamera / setLightDirection / setLightScreenUV first.
     */
    void rayMarch(Graphics *gfx, Texture *linearDepth);
    void rayMarchTo(Graphics *gfx, Texture *linearDepth, Canvas *dest);

    /**
     * Build an RGBA8 texture with linear depth in R (G=B=R, A=255).
     * depth01(x,y) should return values in [0,1]. Owned by Graphics.
     */
    Texture *newLinearDepthTexture(Graphics *gfx, int width, int height,
                                   float (*depth01)(int x, int y, void *userdata), void *userdata);

    /**
     * Downscale helper: returns floor(dim / downscale), at least 1.
     * Callers create occlusion / scatter canvases at this size for the
     * active quality tier.
     */
    int resolutionFor(int fullSize) const;

    /**
     * Draw all visible Renderable2D with castOcclusion into the current canvas
     * as black silhouettes (shadow-analogue occlusion pass).
     */
    void drawOccluders2D(Graphics *gfx);

    Shader *getShader() const { return shader_; }
    Shader *getRayMarchShader() const { return rayShader_; }

private:
    void applyQualityDefaults();
    void uploadCommon(bool compositeFromScene);
    void uploadRayMarchCommon();
    void drawFullscreen(Graphics *gfx, Texture *source, Shader *shader);

    Graphics *gfx_ = nullptr;     // not owned
    Shader *shader_ = nullptr;    // owned by Graphics (screenspace)
    Shader *rayShader_ = nullptr; // owned by Graphics (ray march)
    std::string quality_ = "medium";
    std::string mode_ = "screenspace";
    float downscale_ = 2.f;
    glm::mat4 invViewProj_{1.f};
    float nearZ_ = 0.1f;
    float farZ_ = 100.f;
    glm::vec3 lightDir_{0.4f, 1.f, 0.3f};
};

}  // namespace eve::graphics
