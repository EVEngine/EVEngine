#pragma once

#include <string>

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/**
 * Classic image-space anti-aliasing.
 *
 * Modes:
 *  - "fxaa" — FXAA 3.11-style luminance edge search + subpixel blend
 *  - "smaa" — SMAA-inspired single-pass morphological AA (fits 1×MainTex post path)
 *  - "ssaa" — supersample resolve (box / tent / Gaussian); render into a larger Canvas first
 *  - "nfaa" — Normal Filter AA (blur along luma-gradient tangent)
 *
 * Quality presets ("low" | "medium" | "high") tune thresholds / search / kernel radius.
 * Workflow: render scene → Canvas → aa.apply / aa.applyTo → screen or next post.
 */
class AntiAliasing {
public:
    explicit AntiAliasing(Graphics *gfx);
    ~AntiAliasing();

    AntiAliasing(const AntiAliasing &) = delete;
    AntiAliasing &operator=(const AntiAliasing &) = delete;

    /** "low" | "medium" | "high" (unknown → medium). */
    void setQuality(const std::string &quality);
    std::string getQuality() const { return quality_; }

    /** "fxaa" | "smaa" | "ssaa" | "nfaa" (unknown → fxaa). */
    void setMode(const std::string &mode);
    std::string getMode() const { return mode_; }

    bool hasParam(const std::string &name) const;
    void setFloat(const std::string &name, float value);
    float getFloat(const std::string &name) const;

    /**
     * Suggested supersample scale for the active quality when using "ssaa"
     * (2 for low/medium, 4 for high). Callers create the source Canvas at
     * destSize * suggestScale().
     */
    float suggestScale() const;

    /** Source pixel count helper: floor(dest * suggestScale()), at least 1. */
    int resolutionFor(int destSize) const;

    /**
     * Apply current mode to `source`, writing into the currently bound canvas / screen.
     * Uploads texelW/texelH from the source size.
     */
    void apply(Graphics *gfx, Texture *source);
    void applyTo(Graphics *gfx, Texture *source, Canvas *dest);

    void applyCanvas(Graphics *gfx, Canvas *source);
    void applyCanvasTo(Graphics *gfx, Canvas *source, Canvas *dest);

    Shader *getFxaaShader() const { return fxaa_; }
    Shader *getSmaaShader() const { return smaa_; }
    Shader *getSsaaShader() const { return ssaa_; }
    Shader *getNfaaShader() const { return nfaa_; }
    Shader *getShader() const;

private:
    void applyQualityDefaults();
    void uploadScreenUniforms(Texture *source);
    void drawFullscreen(Graphics *gfx, Texture *source, Shader *shader);
    Shader *shaderForMode() const;

    Graphics *gfx_ = nullptr;  // not owned
    Shader *fxaa_ = nullptr;   // owned by Graphics
    Shader *smaa_ = nullptr;
    Shader *ssaa_ = nullptr;
    Shader *nfaa_ = nullptr;
    std::string quality_ = "medium";
    std::string mode_ = "fxaa";
};

}  // namespace eve::graphics
