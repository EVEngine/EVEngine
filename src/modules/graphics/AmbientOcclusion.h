#pragma once
#include "common/Export.h"


#include <string>
#include <glm/mat4x4.hpp>

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/**
 * @brief Screen-space ambient occlusion.
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
class EVENGINE_API_BACKENDS AmbientOcclusion {
public:
    /** @brief Ambient occlusion. */
    explicit AmbientOcclusion(Graphics *gfx);
    /** @brief Ambient occlusion. */
    ~AmbientOcclusion();

    AmbientOcclusion(const AmbientOcclusion &) = delete;
    AmbientOcclusion &operator=(const AmbientOcclusion &) = delete;

    /** @brief "low" | "medium" | "high" (unknown → medium). */
    void setQuality(const std::string &quality);
    /** @brief Returns the quality. */
    std::string getQuality() const { return quality_; }

    /** @brief "ssao" | "hbao" | "gtao". */
    void setMode(const std::string &mode);
    /** @brief Returns the mode. */
    std::string getMode() const { return mode_; }

    /**
     * @brief Camera for depth reconstruction (RH + ZO).
     * Builds inv(viewProj) and near/far used by compute*.
     */
    void setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY, float targetZ,
                   float upX, float upY, float upZ, float fovYDeg, float aspect, float nearZ,
                   float farZ);

    /** @brief Sets the inv view proj. */
    void setInvViewProj(const glm::mat4 &invViewProj);

    /** @brief Sets the radius. */
    void setRadius(float radius);
    /** @brief Sets the bias. */
    void setBias(float bias);
    /** @brief Sets the intensity. */
    void setIntensity(float intensity);
    /** @brief Sets the power. */
    void setPower(float power);
    /** @brief Sets the thickness. */
    void setThickness(float thickness);

    /** @brief Returns the radius. */
    float getRadius() const;
    /** @brief Returns the bias. */
    float getBias() const;
    /** @brief Returns the intensity. */
    float getIntensity() const;
    /** @brief Returns the power. */
    float getPower() const;

    /** @brief True when param. */
    bool hasParam(const std::string &name) const;
    /** @brief Sets the float. */
    void setFloat(const std::string &name, float value);
    /** @brief Returns the float. */
    float getFloat(const std::string &name) const;

    /** Effective sample count (ssao) or dirCount*stepCount (hbao/gtao). */
    /** @brief Returns the sample count. */
    int getSampleCount() const;
    /** @brief Returns the downscale. */
    float getDownscale() const { return downscale_; }

    /**
     * @brief Downscale helper: returns floor(dim / downscale), at least 1.
     * Callers create AO canvases at this size for the active quality tier.
     */
    int resolutionFor(int fullSize) const;

    /**
     * @brief Compute AO into the currently bound canvas / screen.
     * Output RGB = AO (1=open), A = depth01.
     */
    void compute(Graphics *gfx, Texture *linearDepth);
    /** @brief Computes to. */
    void computeTo(Graphics *gfx, Texture *linearDepth, Canvas *dest);

    /** @brief Bilateral blur of an AO map (RGB=AO, A=depth). */
    void blur(Graphics *gfx, Texture *aoMap);
    /** @brief Blur to. */
    void blurTo(Graphics *gfx, Texture *aoMap, Canvas *dest);

    /**
     * @brief Darken the current target with AO: black + alpha=(1-ao)*intensity.
     * Draw over an already-rendered scene (SrcAlpha blend).
     */
    void applyOverlay(Graphics *gfx, Texture *aoMap);
    /** @brief Applies overlay to. */
    void applyOverlayTo(Graphics *gfx, Texture *aoMap, Canvas *dest);

    /**
     * @brief One-pass SSAO overlay for the 3D swapchain path.
     * Samples hardware D32 (Vulkan NDC z in .r) and darkens the already-drawn
     * scene. Optional worldNormal is GBuffer RGB = n*0.5+0.5; null reconstructs
     * from depth. Call after forward draws while the swapchain pass is still open.
     * Canvas compute() still uses 8-bit linear depth in R.
     */
    void applyFromDepth(Graphics *gfx, Texture *hwDepth);
    /** @brief Applies from depth to. */
    void applyFromDepthTo(Graphics *gfx, Texture *linearDepth, Canvas *dest);
    /** @brief Applies from g buffer. */
    void applyFromGBuffer(Graphics *gfx, Texture *hwDepth, Texture *worldNormal);

    /**
     * @brief Build an RGBA8 texture with linear depth in R (G=B=R, A=255).
     * Owned by Graphics (same convention as Volumetric).
     */
    Texture *newLinearDepthTexture(Graphics *gfx, int width, int height,
                                   /** @brief Float. */
                                   float (*depth01)(int x, int y, void *userdata), void *userdata);

    /** @brief Returns the shader. */
    Shader *getShader() const;
    /** @brief Returns the ssao shader. */
    Shader *getSsaoShader() const { return ssaoShader_; }
    /** @brief Returns the hbao shader. */
    Shader *getHbaoShader() const { return hbaoShader_; }
    /** @brief Returns the gtao shader. */
    Shader *getGtaoShader() const { return gtaoShader_; }
    /** @brief Returns the blur shader. */
    Shader *getBlurShader() const { return blurShader_; }
    /** @brief Returns the overlay shader. */
    Shader *getOverlayShader() const { return overlayShader_; }
    /** @brief Returns the from depth shader. */
    Shader *getFromDepthShader() const { return fromDepthShader_; }

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
    Shader *fromDepthShader_ = nullptr;
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
