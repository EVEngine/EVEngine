#pragma once

#include <string>
#include <glm/mat4x4.hpp>

namespace eve::graphics {

class Canvas;
class Drawable;
class Graphics;
class Mesh;
class Shader;
class Texture;

/**
 * Screen-space volumetric light (god rays / light shafts) with dust & fog.
 *
 * Pipeline (GPU Gems 3 style):
 *  1) Build an occlusion map (black occluders + bright light) via Drawable::drawOcclusion
 *     or use the lit scene itself as a soft occlusion estimate.
 *  2) Radial blur toward the light screen UV + particulate dust / haze.
 *  3) Composite onto the scene (single-pass from scene, or shafts overlay).
 *
 * Quality presets ("low" | "medium" | "high") control sample count and optional
 * internal downscale for the scatter pass.
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

    /** Light position in UV (0..1), origin top-left to match 2D UVs. */
    void setLightScreenUV(float u, float v);
    float getLightScreenU() const;
    float getLightScreenV() const;

    /** Pixel-space helper (converts with width/height). */
    void setLightScreenPos(float x, float y, float width, float height);

    void setShaftColor(float r, float g, float b);
    void setFogColor(float r, float g, float b);
    void setIntensity(float intensity);
    void setTime(float seconds);

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

private:
    void applyQualityDefaults();
    void uploadCommon(bool compositeFromScene);
    void drawFullscreen(Graphics *gfx, Texture *source);

    Graphics *gfx_ = nullptr;  // not owned
    Shader *shader_ = nullptr;  // owned by Graphics
    std::string quality_ = "medium";
    float downscale_ = 2.f;
};

}  // namespace eve::graphics
