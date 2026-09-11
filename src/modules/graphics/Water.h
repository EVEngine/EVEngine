#pragma once

#include "graphics/Shader.h"
#include "graphics/WaterStyleConfig.h"

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
    float getWaveSpeed() const { return config_.waveSpeed; }

    /** @brief Amplitude of the shore-edge waves. */
    void setWaveAmplitude(float amp);
    float getWaveAmplitude() const { return config_.waveAmplitude; }

    /** @brief Amplitude of the occasional middle drop ripples. */
    void setRippleAmplitude(float amp);
    float getRippleAmplitude() const { return config_.rippleAmplitude; }

    /** @brief Compatibility setter for the world-space shoreline foam width. */
    void setEdgeFalloff(float edge);
    float getEdgeFalloff() const { return config_.foamWidth; }

    /** @brief How many expanding drop ripples exist. */
    void setRippleCount(int count);
    int getRippleCount() const { return config_.rippleCount; }

    /** @brief Seconds between drop ripples. */
    void setRippleInterval(float seconds);
    float getRippleInterval() const { return config_.rippleInterval; }

    void setWaveScale(float scale);
    float getWaveScale() const { return config_.waveScale; }

    void setWaterColor(float r, float g, float b);
    void setReflectionTint(float r, float g, float b);
    void setReflectionIntensity(float intensity);
    float getReflectionIntensity() const { return config_.reflectionIntensity; }

    void setSunIntensity(float intensity);
    float getSunIntensity() const { return config_.sunIntensity; }

    /**
     * @brief Optional screen-space reflection overlay. When enabled, the shader
     * samples the temporally resolved SSR-chain result (bound through mesh
     * binding 6) at the fragment's screen UV and blends it over the environment
     * cubemap backup. The drawable viewport is inferred when setViewport was
     * not called. SSR must also be enabled on RenderControl.
     */
    void setScreenSpaceReflection(bool enabled, float strength = 0.85f);
    bool getScreenSpaceReflection() const { return config_.screenSpaceReflection; }
    float getScreenSpaceReflectionStrength() const { return config_.screenSpaceReflectionStrength; }

    /**
     * @brief Atomically replace all authored parameters from `eve.graphics.stylized-water/1` JSON.
     * @param json Borrowed UTF-8 JSON used only for this call.
     * @return Success after validation and publication; failure preserves the previous configuration.
     * @thread Main/render thread only. Does not invoke callbacks or allocate GPU resources.
     */
    [[nodiscard]] Result<void> applyConfigJson(const std::string& json);
    /** @brief Return the canonical current `eve.graphics.stylized-water/1` JSON snapshot. */
    [[nodiscard]] Result<std::string> configJson() const;
    /** @brief Return the immutable current configuration snapshot by value. */
    WaterStyleConfig config() const { return config_; }

    /** @brief Window / target size in pixels, used to compute screen-space UVs. */
    void setViewport(float width, float height);
    float getViewportWidth() const { return viewportW_; }
    float getViewportHeight() const { return viewportH_; }

    /** @brief Upload current params to the shader push constants. */
    void bindParams();

    /** @brief Draw the water plane (uses default mesh3d camera / lighting state). */
    void draw();

    /**
     * @brief Draw with an optional caller-rendered planar reflection.
     * @param planarReflection Borrowed texture sampled only during this synchronous draw; may be null.
     *        The caller retains ownership and may release it after this call returns.
     * @param strength Reflection contribution in [0,4]. A supplied planar reflection takes priority
     *        over the configured screen-space reflection history for this draw only.
     * @thread Main/render thread only. The call does not retain pointers or invoke callbacks.
     */
    void drawWithPlanarReflection(Texture* planarReflection, float strength = 1.0F);

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
    void drawWithReflection(Texture* reflection, float strength);
    void drawReflectionCapture();

    Graphics *gfx_ = nullptr;
    Shader *shader_ = nullptr;
    Mesh *mesh_ = nullptr;

    float time_ = 0.f;
    WaterStyleConfig config_;
    float viewportW_ = 0.f;
    float viewportH_ = 0.f;
    uint64_t captureDrawerToken_ = 0;
    uint32_t reflectionCaptureMask_ = 0xffffffffu;
    bool reflectionCaptureEnabled_ = true;
};

/** @brief Create the embedded water fragment shader (owned by Graphics). */
Shader *newWaterShader(Graphics *gfx);

}  // namespace eve::graphics
