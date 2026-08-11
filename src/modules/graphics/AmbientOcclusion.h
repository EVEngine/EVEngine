#pragma once

#include <string>
#include <glm/mat4x4.hpp>

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/**
 * Screen-space ambient occlusion.
 *
 * Modes:
 *  - "ssao" — Crytek/Mittring hemisphere sampling
 *  - "hbao" — horizon-based AO (NVIDIA-inspired)
 *  - "gtao" — ground-truth AO (Jimenez/Frostbite-inspired, single-frame)
 *
 * Quality presets ("low" | "medium" | "high") control sample/dir counts and
 * suggested downscale via resolutionFor().
 *
 * Pipeline: compute(depth) → optional blur(ao) → applyOverlay(ao) over scene.
 * Depth input matches Volumetric::newLinearDepthTexture (R = linear 0..1).
 */
class AmbientOcclusion {
public:
    explicit AmbientOcclusion(Graphics *gfx);
    ~AmbientOcclusion();

    AmbientOcclusion(const AmbientOcclusion &) = delete;
    AmbientOcclusion &operator=(const AmbientOcclusion &) = delete;

    /** "low" | "medium" | "high" (unknown → medium). */
    void setQuality(const std::string &quality);
    std::string getQuality() const { return quality_; }

    /** "ssao" | "hbao" | "gtao". */
    void setMode(const std::string &mode);
    std::string getMode() const { return mode_; }

    /**
     * Camera for depth reconstruction (RH + ZO).
     * Builds inv(viewProj) and near/far used by compute*.
     */
    void setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY, float targetZ,
                   float upX, float upY, float upZ, float fovYDeg, float aspect, float nearZ,
                   float farZ);

    void setInvViewProj(const glm::mat4 &invViewProj);

    void setRadius(float radius);
    void setBias(float bias);
    void setIntensity(float intensity);
    void setPower(float power);
    void setThickness(float thickness);

    float getRadius() const;
    float getBias() const;
    float getIntensity() const;
    float getPower() const;

    bool hasParam(const std::string &name) const;
    void setFloat(const std::string &name, float value);
    float getFloat(const std::string &name) const;

    /** Effective sample count (ssao) or dirCount*stepCount (hbao/gtao). */
    int getSampleCount() const;
    float getDownscale() const { return downscale_; }

    /**
     * Downscale helper: returns floor(dim / downscale), at least 1.
     * Callers create AO canvases at this size for the active quality tier.
     */
    int resolutionFor(int fullSize) const;

    /**
     * Compute AO into the currently bound canvas / screen.
     * Output RGB = AO (1=open), A = depth01.
     */
    void compute(Graphics *gfx, Texture *linearDepth);
    void computeTo(Graphics *gfx, Texture *linearDepth, Canvas *dest);

    /** Bilateral blur of an AO map (RGB=AO, A=depth). */
    void blur(Graphics *gfx, Texture *aoMap);
    void blurTo(Graphics *gfx, Texture *aoMap, Canvas *dest);

    /**
     * Darken the current target with AO: black + alpha=(1-ao)*intensity.
     * Draw over an already-rendered scene (SrcAlpha blend).
     */
    void applyOverlay(Graphics *gfx, Texture *aoMap);
    void applyOverlayTo(Graphics *gfx, Texture *aoMap, Canvas *dest);

    /**
     * Build an RGBA8 texture with linear depth in R (G=B=R, A=255).
     * Owned by Graphics (same convention as Volumetric).
     */
    Texture *newLinearDepthTexture(Graphics *gfx, int width, int height,
                                   float (*depth01)(int x, int y, void *userdata), void *userdata);

    Shader *getShader() const;
    Shader *getSsaoShader() const { return ssaoShader_; }
    Shader *getHbaoShader() const { return hbaoShader_; }
    Shader *getGtaoShader() const { return gtaoShader_; }
    Shader *getBlurShader() const { return blurShader_; }
    Shader *getOverlayShader() const { return overlayShader_; }

private:
    void applyQualityDefaults();
    void uploadComputeCommon(Shader *shader, int width, int height);
    void drawFullscreen(Graphics *gfx, Texture *source, Shader *shader);
    Shader *activeComputeShader() const;

    Graphics *gfx_ = nullptr;
    Shader *ssaoShader_ = nullptr;
    Shader *hbaoShader_ = nullptr;
    Shader *gtaoShader_ = nullptr;
    Shader *blurShader_ = nullptr;
    Shader *overlayShader_ = nullptr;
    std::string quality_ = "medium";
    std::string mode_ = "ssao";
    float downscale_ = 2.f;
    glm::mat4 invViewProj_{1.f};
    float nearZ_ = 0.1f;
    float farZ_ = 100.f;
    float radius_ = 0.75f;
    float bias_ = 0.025f;
    float intensity_ = 1.f;
    float power_ = 1.5f;
    float thickness_ = 0.5f;
};

}  // namespace eve::graphics
