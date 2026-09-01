#pragma once

#include "graphics/Shader.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace eve::graphics {

class Graphics;
class Mesh;
class Texture;

/**
 * @brief Dynamic water surface with sky reflection and animated ripples.
 *
 * A custom Mesh3D fragment shader renders a flat plane as water:
 *   - Reflects the sky via the environment cubemap (binding 3), Fresnel-weighted
 *     so grazing angles reflect the sky more strongly.
 *   - Ripples near the water's edge (waves lapping the shore), strongest at the
 *     boundary and fading inward.
 *   - Occasional expanding ripple rings in the middle (rain drops / fish / boat),
 *     appearing at staggered intervals.
 *   - A sun glint highlight from the primary directional light.
 *
 * Parameters are packed into the shader push-constant block (data[0..31]); see
 * Water::bindDefaults for the layout. Caller owns Water*; its Mesh / Shader are
 * owned by Graphics.
 */
class Water {
public:
    explicit Water(Graphics *gfx);
    ~Water();

    Water(const Water &) = delete;
    Water &operator=(const Water &) = delete;

    /** @brief Build a flat XZ plane (Y-up) sized sizeX × sizeZ with UVs in [0,1]². */
    void createPlane(float sizeX, float sizeZ, int segX, int segZ);

    /** @brief Advance the animation clock by dt seconds. */
    void update(float dt);
    void setTime(float seconds);
    float getTime() const { return time_; }

    // --- Animation / material knobs ---
    void setWaveSpeed(float speed);
    float getWaveSpeed() const { return waveSpeed_; }

    /** @brief Amplitude of the shore-edge waves. */
    void setWaveAmplitude(float amp);
    float getWaveAmplitude() const { return waveAmplitude_; }

    /** @brief Amplitude of the occasional middle drop ripples. */
    void setRippleAmplitude(float amp);
    float getRippleAmplitude() const { return rippleAmplitude_; }

    /** @brief Width (in UV, 0..1) of the edge wave band. */
    void setEdgeFalloff(float edge);
    float getEdgeFalloff() const { return edgeFalloff_; }

    /** @brief How many expanding drop ripples exist. */
    void setRippleCount(int count);
    int getRippleCount() const { return rippleCount_; }

    /** @brief Seconds between drop ripples. */
    void setRippleInterval(float seconds);
    float getRippleInterval() const { return rippleInterval_; }

    void setWaveScale(float scale);
    float getWaveScale() const { return waveScale_; }

    void setWaterColor(float r, float g, float b);
    void setReflectionTint(float r, float g, float b);
    void setReflectionIntensity(float intensity);
    float getReflectionIntensity() const { return reflectionIntensity_; }

    void setSunIntensity(float intensity);
    float getSunIntensity() const { return sunIntensity_; }

    /**
     * @brief Optional screen-space reflection overlay. When enabled, the shader
     * samples the temporally resolved SSR-chain result (bound through mesh
     * binding 6) at the fragment's screen UV and blends it over the environment
     * cubemap backup. The drawable viewport is inferred when setViewport was
     * not called. SSR must also be enabled on RenderControl.
     */
    void setScreenSpaceReflection(bool enabled, float strength = 0.85f);
    bool getScreenSpaceReflection() const { return ssrEnabled_; }
    float getScreenSpaceReflectionStrength() const { return ssrStrength_; }

    /** @brief Window / target size in pixels, used to compute screen-space UVs. */
    void setViewport(float width, float height);
    float getViewportWidth() const { return viewportW_; }
    float getViewportHeight() const { return viewportH_; }

    /** @brief Upload current params to the shader push constants. */
    void bindParams();

    /** @brief Draw the water plane (uses default mesh3d camera / lighting state). */
    void draw();

    /** @brief Enable or disable inclusion in reflection-probe captures. */
    void setReflectionCaptureEnabled(bool enabled) { reflectionCaptureEnabled_ = enabled; }
    /** @brief Return whether this water surface is included in reflection-probe captures. */
    bool getReflectionCaptureEnabled() const { return reflectionCaptureEnabled_; }
    /** @brief Set the reflection-capture visibility layer mask. */
    void setReflectionCaptureMask(uint32_t mask) { reflectionCaptureMask_ = mask; }
    /** @brief Return the reflection-capture visibility layer mask. */
    uint32_t getReflectionCaptureMask() const { return reflectionCaptureMask_; }

    Shader *getShader() const { return shader_; }
    Mesh *getMesh() const { return mesh_; }

    /** @brief Names of the push-constant parameters (for UI / inspection). */
    static int paramCount();
    static std::string paramName(int index);

private:
    void drawReflectionCapture();

    Graphics *gfx_ = nullptr;
    Shader *shader_ = nullptr;
    Mesh *mesh_ = nullptr;

    float time_ = 0.f;
    float waveSpeed_ = 1.2f;
    float waveAmplitude_ = 0.35f;
    float rippleAmplitude_ = 0.6f;
    float edgeFalloff_ = 0.18f;
    int rippleCount_ = 6;
    float rippleInterval_ = 1.6f;
    float waveScale_ = 14.f;
    float waterColor_[3] = {0.02f, 0.16f, 0.24f};
    float reflectionTint_[3] = {0.7f, 0.85f, 1.0f};
    float reflectionIntensity_ = 0.6f;
    float sunIntensity_ = 0.9f;
    bool ssrEnabled_ = false;
    float ssrStrength_ = 0.85f;
    float viewportW_ = 0.f;
    float viewportH_ = 0.f;
    uint64_t captureDrawerToken_ = 0;
    uint32_t reflectionCaptureMask_ = 0xffffffffu;
    bool reflectionCaptureEnabled_ = true;
};

/** @brief Create the embedded water fragment shader (owned by Graphics). */
Shader *newWaterShader(Graphics *gfx);

}  // namespace eve::graphics
