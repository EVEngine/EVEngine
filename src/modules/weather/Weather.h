#pragma once

#include "common/Module.h"

#include <string>

namespace eve::graphics {
class Graphics;
}

namespace eve::weather {

/**
 * @brief Weather module — real-time precipitation / lightning / wind system.
 *
 * Drives a set of camera-facing Renderable3D meshes (rain streaks, snow
 * flakes, lightning bolts) whose geometry is animated in the vertex shader
 * from a small set of per-frame uniforms (time, wind, intensity). Meshes and
 * shaders are built lazily on first use and reused, so nothing is allocated
 * per-frame.
 *
 * Usage from script:
 *   weather.setPreset("storm");
 *   weather.setIntensity(0.8);
 *   weather.setWindSpeed(12.0);
 *   // each frame before gfx.render3D():
 *   weather.update(dt, gfx);
 *
 * Presets: "clear", "drizzle", "rain", "storm", "snow", "fog".
 */
class Weather : public Module {
public:
    Module_REG(Weather);

    Weather();
    ~Weather() override;

    /** @brief Idempotent; builds meshes/shaders on first call. */
    void init(graphics::Graphics *gfx);
    /** @brief Advance sim + push per-frame uniforms; must run before gfx.render3D(). */
    void update(float dt, graphics::Graphics *gfx);

    // ---- presets ----
    void setPreset(const std::string &name);
    std::string getPreset() const;
    static const char *const kPresetNames[];
    static const int kPresetCount;

    // ---- precipitation / wind ----
    void setIntensity(float v);
    float getIntensity() const;
    void setWindSpeed(float v);
    float getWindSpeed() const;
    /** @brief Wind direction in degrees; 0 = toward +Z, 90 = toward -X. */
    void setWindDirection(float degrees);
    float getWindDirection() const;

    // ---- lightning ----
    void setLightningEnabled(bool on);
    bool isLightningEnabled() const;
    /** @brief Force a bolt strike this frame (useful for manual testing). */
    void strike();
    /** @brief How bright the current flash is (0..1), sampled by the scene for a key light. */
    float getFlash() const;

    // ---- mood (sky / fog), read by the example to tint the scene ----
    void setSkyColor(float r, float g, float b);
    float getSkyColorR() const;
    float getSkyColorG() const;
    float getSkyColorB() const;
    void setSunIntensity(float v);
    float getSunIntensity() const;
    void setFogColor(float r, float g, float b);
    float getFogColorR() const;
    float getFogColorG() const;
    float getFogColorB() const;
    void setFogDensity(float v);
    float getFogDensity() const;

    /**
     * @brief Enable legacy weather-owned sky and directional lighting.
     *
     * Disable this when DayNight owns the environment; precipitation and
     * lightning geometry continue to render and getFlash() remains available.
     */
    void setEnvironmentEnabled(bool enabled);
    bool isEnvironmentEnabled() const;

    /** @brief Ambient multiplier that the example should feed into camera.setAmbient(). */
    float getAmbientBrightness() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace eve::weather
