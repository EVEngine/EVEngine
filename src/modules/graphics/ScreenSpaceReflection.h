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
 * Screen-space reflections (SSR) as a fullscreen post pass.
 *
 * Reads the lit scene color, hardware depth (D32) and world normal, and for
 * each pixel ray-marches the reflected ray in screen space. Where the ray hits
 * geometry it emits the reflected scene color with A = hit validity, so a
 * surface (e.g. Water) can blend it over an env-cubemap fallback.
 *
 * Mirrors GlobalIllumination: manual fullscreen overlay
 * (applyFromSceneTo). It does not auto-run in the default 3D path.
 */
class ScreenSpaceReflection {
public:
    explicit ScreenSpaceReflection(Graphics *gfx);
    ~ScreenSpaceReflection();

    ScreenSpaceReflection(const ScreenSpaceReflection &) = delete;
    ScreenSpaceReflection &operator=(const ScreenSpaceReflection &) = delete;

    /** Camera for depth reconstruction (RH + ZO). Builds inv(viewProj) + near/far. */
    void setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY, float targetZ,
                   float upX, float upY, float upZ, float fovYDeg, float aspect, float nearZ,
                   float farZ);
    void setInvViewProj(const glm::mat4 &invViewProj);

    /** Enable/disable the pass. When disabled it emits transparent (0 hit). */
    void setEnabled(bool enabled);
    bool getEnabled() const { return enabled_; }

    void setMaxDistance(float meters);
    void setStepLength(float meters);
    void setMaxSteps(int steps);
    void setThickness(float meters);
    void setStrength(float strength);
    float getStrength() const { return strength_; }

    bool hasParam(const std::string &name) const;
    void setFloat(const std::string &name, float value);
    float getFloat(const std::string &name) const;

    /** Write the SSR result into the currently bound canvas / dest. */
    void applyFromSceneTo(Graphics *gfx, Texture *sceneColor, Texture *hwDepth,
                          Texture *worldNormal, Canvas *dest);
    /** Write SSR into the currently bound canvas / screen. */
    void applyFromScene(Graphics *gfx, Texture *sceneColor, Texture *hwDepth,
                        Texture *worldNormal);

    /** Owned reflection canvas (created on first use at the current target size). */
    Canvas *getReflectionCanvas();
    /** The reflection texture from the owned canvas, or nullptr before first apply. */
    Texture *getReflectionTexture();

    Shader *getShader() const { return ssr_; }

private:
    void uploadUniforms(int width, int height);

    Graphics *gfx_ = nullptr;
    Shader *ssr_ = nullptr;
    Canvas *reflection_ = nullptr;
    glm::mat4 invViewProj_{1.f};
    float nearZ_ = 0.1f;
    float farZ_ = 100.f;
    bool enabled_ = true;
    float maxDist_ = 60.f;
    float stepLen_ = 0.4f;
    float maxSteps_ = 96.f;
    float thickness_ = 0.5f;
    float strength_ = 0.9f;
    float bias_ = 0.0005f;
};

}  // namespace eve::graphics
