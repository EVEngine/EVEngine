#pragma once

#include <string>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/**
 * @brief Screen-space single-bounce GI (SSGI).
 * @lifetime All texture arguments are borrowed for the duration of each call.
 *
 * Samples scene color (RGB = lit radiance) plus hardware D32 depth, and adds
 * bounced light from nearby occluders. It can be used as a manual fullscreen
 * overlay (`applyFromScene` / `applyFromDepth`), and RenderSystem3D runs it
 * automatically when `rtgi` or `reflectionChain` is enabled. Tests may also
 * pass a packed RGBA8 texture (RGB = albedo, A = linear depth) through
 * applyFromDepth.
 *
 * Quality presets ("low" | "medium" | "high") tune sample count and radius.
 */
class GlobalIllumination {
public:
    explicit GlobalIllumination(Graphics *gfx);
    ~GlobalIllumination();

    GlobalIllumination(const GlobalIllumination &) = delete;
    GlobalIllumination &operator=(const GlobalIllumination &) = delete;

    /** @brief "low" | "medium" | "high" | "ultra" (unknown becomes medium). */
    void setQuality(const std::string &quality);
    std::string getQuality() const { return quality_; }

    /**
     * @brief Camera for depth reconstruction (RH + ZO).
     * Builds inv(viewProj) and near/far used by applyFromDepth.
     */
    void setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY, float targetZ,
                   float upX, float upY, float upZ, float fovYDeg, float aspect, float nearZ,
                   float farZ);

    void setInvViewProj(const glm::mat4 &invViewProj);
    /** @brief Set the GBuffer world-normal texture used for guided GI sampling. */
    void setWorldNormalTexture(Texture *worldNormal) { worldNormal_ = worldNormal; }
    /** @brief Set receiver albedo used for energy-conserving diffuse GI response. */
    void setAlbedoTexture(Texture *albedo) { albedo_ = albedo; }
    /** @brief Set packed linear-depth and motion texture used by temporal GI resolve. */
    void setTemporalMotionTexture(Texture *motionDepth) { temporalMotionDepth_ = motionDepth; }
    /** @brief Set the shared horizontally packed RG min/max depth hierarchy. */
    void setDepthPyramid(Texture *atlas, int levels) {
        depthPyramid_ = atlas;
        depthPyramidLevels_ = levels;
    }
    /** @brief Discard temporal GI history after a render-chain discontinuity. */
    void invalidateHistory() { historyValid_ = false; }

    void setRadius(float radius);
    void setIntensity(float intensity);
    /** @brief Set world-space hit thickness used to reject screen-space GI leaks. */
    void setThickness(float thickness);
    /** @brief Set internal GI resolution scale in the range 0.25 to 1.0. */
    void setResolutionScale(float scale);
    void setLightDirection(float dx, float dy, float dz);
    void setLightColor(float r, float g, float b);

    float getRadius() const { return radius_; }
    float getIntensity() const { return intensity_; }
    /** @brief Return world-space GI hit thickness. */
    float getThickness() const { return thickness_; }
    float getResolutionScale() const { return resolutionScale_; }

    bool hasParam(const std::string &name) const;
    void setFloat(const std::string &name, float value);
    float getFloat(const std::string &name) const;

    int getSampleCount() const;
    /** @brief Number of explicit A-trous prefilter passes for the active quality. */
    int getSpatialPassCount() const {
        return quality_ == "ultra" ? 3 : quality_ == "high" ? 2 : quality_ == "medium" ? 1 : 0;
    }

    /**
     * @brief Overlay bounced light onto the currently bound canvas / screen.
     * Packed path (tests): RGB=albedo/lit, A=linear depth 0..1.
     * 3D path: applyFromScene(color, hwDepth) with D32 NDC z.
     */
    void applyFromDepth(Graphics *gfx, Texture *packedAlbedo);
    void applyFromDepthTo(Graphics *gfx, Texture *packedAlbedo, Canvas *dest);
    void applyFromSceneTo(Graphics *gfx, Texture *color, Texture *hwDepth, Canvas *dest);
    void applyFromSceneTo(Graphics *gfx, Texture *color, Texture *hwDepth);
    /** @lifetime Returned canvas is borrowed from this effect until its targets are recreated. */
    Canvas *getWorkingCanvas();
    /**
     * @brief Temporally resolved GI texture, or the raw working texture before history exists.
     * @lifetime Returned texture is borrowed from this effect until its targets are recreated.
     */
    Texture *getWorkingTexture();
    void applyFromScene(Graphics *gfx, Texture *color, Texture *hwDepth);

    /** @lifetime Returned shader is borrowed and owned by Graphics. */
    Shader *getShader() const { return ssgi_; }

private:
    void applyQualityDefaults();
    void uploadUniforms(int width, int height);
    void drawFullscreen(Graphics *gfx, Texture *source, Shader *shader, Texture *hwDepth = nullptr);
    Canvas *getGiCanvas();
    void ensureHistory(int width, int height);
    void resolveTemporal(Graphics *gfx, Texture *current);

    Graphics *gfx_ = nullptr;
    Shader *ssgi_ = nullptr;
    Shader *temporal_ = nullptr;
    std::string quality_ = "medium";
    glm::mat4 invViewProj_{1.f};
    float nearZ_ = 0.1f;
    float farZ_ = 100.f;
    float radius_ = 1.25f;
    float intensity_ = 0.45f;
    float thickness_ = 0.2f;
    float temporalCurrentWeight_ = 0.16f;
    float spatialFilterStrength_ = 0.5f;
    float resolutionScale_ = 1.f;
    glm::vec3 lightDir_{0.4f, 1.f, 0.3f};
    glm::vec3 lightColor_{1.f, 1.f, 1.f};
    Canvas *giCanvas_ = nullptr;
    Texture *worldNormal_ = nullptr;
    Texture *albedo_ = nullptr;
    Texture *temporalMotionDepth_ = nullptr;
    Texture *depthPyramid_ = nullptr;
    int depthPyramidLevels_ = 0;
    Canvas *historyA_ = nullptr;
    Canvas *historyB_ = nullptr;
    Canvas *historyRead_ = nullptr;
    Canvas *historyWrite_ = nullptr;
    Canvas *spatialA_ = nullptr;
    Canvas *spatialB_ = nullptr;
    glm::mat4 previousViewProj_{1.f};
    glm::vec3 previousEye_{0.f};
    glm::vec3 previousForward_{0.f, 0.f, -1.f};
    float previousFovY_ = 0.f;
    float previousAspect_ = 1.f;
    bool historyValid_ = false;
    bool viewProjValid_ = false;
    bool cameraValid_ = false;
    int historyWidth_ = 0;
    int historyHeight_ = 0;
    uint32_t samplingFrame_ = 0;
};

}  // namespace eve::graphics
