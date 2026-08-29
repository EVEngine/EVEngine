#pragma once

#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/**
 * @brief Screen-space reflections (SSR) as a fullscreen post pass.
 *
 * Reads the lit scene color, hardware depth (D32) and world normal, and for
 * each pixel ray-marches the reflected ray in screen space. Where the ray hits
 * geometry it emits the reflected scene color with A = hit validity, so a
 * surface (e.g. Water) can blend it over an env-cubemap backup.
 *
 * Mirrors GlobalIllumination and can be invoked manually with applyFromSceneTo.
 * RenderSystem3D also runs it automatically when `ssr` or `reflectionChain`
 * is enabled in RenderControl.
 */
class ScreenSpaceReflection {
public:
    explicit ScreenSpaceReflection(Graphics *gfx);
    ~ScreenSpaceReflection();

    ScreenSpaceReflection(const ScreenSpaceReflection &) = delete;
    ScreenSpaceReflection &operator=(const ScreenSpaceReflection &) = delete;

    /** @brief Camera for depth reconstruction (RH + ZO). Builds inv(viewProj) + near/far. */
    void setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY, float targetZ,
                   float upX, float upY, float upZ, float fovYDeg, float aspect, float nearZ,
                   float farZ);
    void setInvViewProj(const glm::mat4 &invViewProj);
    /** @brief Set packed linear-depth and motion texture used by temporal SSR resolve. */
    void setTemporalMotionTexture(Texture *motionDepth) { temporalMotionDepth_ = motionDepth; }
    /** @brief Set the shared horizontally packed RG min/max depth hierarchy. */
    void setDepthPyramid(Texture *atlas, int levels) {
        depthPyramid_ = atlas;
        depthPyramidLevels_ = levels;
    }
    /** @brief Discard temporal reflection history after a render-chain discontinuity. */
    void invalidateHistory() { historyValid_ = false; }

    /** @brief Enable/disable the pass. When disabled it emits transparent (0 hit). */
    void setEnabled(bool enabled);
    bool getEnabled() const { return enabled_; }

    /** @brief Set "low", "medium", "high" or "ultra" SSR quality preset. */
    void setQuality(const std::string &quality);
    /** @brief Return the active SSR quality preset. */
    std::string getQuality() const { return quality_; }

    void setMaxDistance(float meters);
    void setStepLength(float meters);
    void setMaxSteps(int steps);
    void setThickness(float meters);
    void setStrength(float strength);
    /** @brief Skip SSR above this material roughness and rely on reflection backup. */
    void setMaxRoughness(float roughness);
    /** @brief Set internal SSR resolution scale in the range 0.25 to 1.0. */
    void setResolutionScale(float scale);
    float getStrength() const { return strength_; }
    float getMaxRoughness() const { return maxRoughness_; }
    float getResolutionScale() const { return resolutionScale_; }

    bool hasParam(const std::string &name) const;
    void setFloat(const std::string &name, float value);
    float getFloat(const std::string &name) const;

    /** @brief Write the SSR result into the currently bound canvas / dest. */
    void applyFromSceneTo(Graphics *gfx, Texture *sceneColor, Texture *hwDepth,
                          Texture *worldNormal, Canvas *dest) {
        applyFromSceneTo(gfx, sceneColor, hwDepth, worldNormal, nullptr, dest);
    }
    /** @brief Write SSR using albedo to preserve diffuse energy during reflection replacement. */
    void applyFromSceneTo(Graphics *gfx, Texture *sceneColor, Texture *hwDepth,
                          Texture *worldNormal, Texture *albedo, Canvas *dest);
    /** @brief Write SSR into the currently bound canvas / screen. */
    void applyFromScene(Graphics *gfx, Texture *sceneColor, Texture *hwDepth,
                        Texture *worldNormal, Texture *albedo = nullptr);

    /** @brief Owned reflection canvas (created on first use at the current target size). */
    Canvas *getReflectionCanvas();
    /** @brief The reflection texture from the owned canvas, or nullptr before first apply. */
    Texture *getReflectionTexture();
    /** @brief True once a temporally resolved reflection frame is safe to sample. */
    bool hasValidHistory() const { return historyValid_ && historyRead_ != nullptr; }
    /** @brief Number of explicit A-trous prefilter passes for the active quality. */
    int getSpatialPassCount() const {
        return quality_ == "ultra" ? 3 : quality_ == "high" ? 2 : quality_ == "medium" ? 1 : 0;
    }

    Shader *getShader() const { return ssr_; }

private:
    void uploadUniforms(int width, int height);
    void ensureHistory(int width, int height);
    void resolveTemporal(Graphics *gfx, Texture *current, Texture *motionDepth);

    Graphics *gfx_ = nullptr;
    Shader *ssr_ = nullptr;
    Shader *temporal_ = nullptr;
    Canvas *reflection_ = nullptr;
    Canvas *historyA_ = nullptr;
    Canvas *historyB_ = nullptr;
    Canvas *historyRead_ = nullptr;
    Canvas *historyWrite_ = nullptr;
    Canvas *spatialA_ = nullptr;
    Canvas *spatialB_ = nullptr;
    Texture *temporalMotionDepth_ = nullptr;
    Texture *depthPyramid_ = nullptr;
    int depthPyramidLevels_ = 0;
    glm::mat4 invViewProj_{1.f};
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
    float nearZ_ = 0.1f;
    float farZ_ = 100.f;
    bool enabled_ = true;
    std::string quality_ = "medium";
    float maxDist_ = 60.f;
    float stepLen_ = 0.4f;
    float maxSteps_ = 96.f;
    float thickness_ = 0.5f;
    float strength_ = 0.9f;
    float bias_ = 0.0005f;
    float maxRoughness_ = 0.8f;
    float resolutionScale_ = 1.f;
    float temporalCurrentWeight_ = 0.16f;
    float spatialFilterStrength_ = 0.5f;
};

}  // namespace eve::graphics
