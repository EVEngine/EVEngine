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
 * Dynamic water surface with sky reflection and animated ripples.
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
    ~Water() = default;

    Water(const Water &) = delete;
    Water &operator=(const Water &) = delete;

    /** Build a flat XZ plane (Y-up) sized sizeX × sizeZ with UVs in [0,1]². */
    void createPlane(float sizeX, float sizeZ, int segX, int segZ);

    /** Advance the animation clock by dt seconds. */
    void update(float dt);
    void setTime(float seconds);
    float getTime() const { return time_; }

    // --- Animation / material knobs ---
    void setWaveSpeed(float speed);
    float getWaveSpeed() const { return waveSpeed_; }

    /** Amplitude of the shore-edge waves. */
    void setWaveAmplitude(float amp);
    float getWaveAmplitude() const { return waveAmplitude_; }

    /** Amplitude of the occasional middle drop ripples. */
    void setRippleAmplitude(float amp);
    float getRippleAmplitude() const { return rippleAmplitude_; }

    /** Width (in UV, 0..1) of the edge wave band. */
    void setEdgeFalloff(float edge);
    float getEdgeFalloff() const { return edgeFalloff_; }

    /** How many expanding drop ripples exist. */
    void setRippleCount(int count);
    int getRippleCount() const { return rippleCount_; }

    /** Seconds between drop ripples. */
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

    /** Upload current params to the shader push constants. */
    void bindParams();

    /** Draw the water plane (uses default mesh3d camera / lighting state). */
    void draw();

    Shader *getShader() const { return shader_; }
    Mesh *getMesh() const { return mesh_; }

    /** Names of the push-constant parameters (for UI / inspection). */
    static int paramCount();
    static std::string paramName(int index);

private:
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
};

/** Create the embedded water fragment shader (owned by Graphics). */
Shader *newWaterShader(Graphics *gfx);

}  // namespace eve::graphics
