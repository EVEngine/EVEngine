#pragma once
#include "common/Export.h"


#include "common/Module.h"
#include "common/PcgPhotoModeApply.h"
#include "common/Result.h"
#include "weather/PcgInteriorWeatherVolume.h"

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
 * Presets: "clear", "drizzle", "rain", "storm", "snow", "fog", "wind", "blizzard".
 */
class EVENGINE_API_ORCHESTRATION Weather : public Module, public IPhotoModeFieldSink {
public:
    Module_REG(Weather);

    /** @brief Weather. */
    Weather();
    /** @brief Weather. */
    ~Weather() override;

    /** @brief Idempotent; builds meshes/shaders on first call. */
    void init(graphics::Graphics *gfx);
    /** @brief Advance sim + push per-frame uniforms; must run before gfx.render3D(). */
    void update(float dt, graphics::Graphics *gfx);

    // ---- presets ----
    /** @brief Sets the preset. */
    void setPreset(const std::string &name);
    /** @brief Returns the preset. */
    std::string getPreset() const;
    static const char *const kPresetNames[];
    static const int kPresetCount;

    /** @brief Return whether this module owns a Pcg weather photo-mode field. */
    PhotoModeFieldAcceptance acceptsPhotoModeField(const PhotoModeAssignment& assignment) const noexcept override;
    /** @brief Atomically apply one Pcg weather field to this authoritative weather state. */
    [[nodiscard]] Result<void> applyPhotoModeField(const PhotoModeAssignment& assignment) override;
    /** @brief Return Pcg's master weather-enabled state. */
    bool getPcgWeatherEnabled() const;
    /** @brief Return Pcg's independent rain playback state. */
    bool getPcgRainEnabled() const;
    /** @brief Return Pcg's independent snow playback state. */
    bool getPcgSnowEnabled() const;
    /** @brief Return whether photo mode overrides the captured wind. */
    bool getPcgWindOverride() const;
    // ---- precipitation / wind ----
    /** @brief Sets the intensity. */
    void setIntensity(float v);
    /** @brief Returns the intensity. */
    float getIntensity() const;
    /** @brief Sets the wind speed. */
    void setWindSpeed(float v);
    /** @brief Returns the wind speed. */
    float getWindSpeed() const;
    /** @brief Wind direction in degrees; 0 = toward +Z, 90 = toward -X. */
    void setWindDirection(float degrees);
    /** @brief Returns the wind direction. */
    float getWindDirection() const;
    /** @brief Override snow world velocity exactly as Pcg SnowWindDir. */
    [[nodiscard]] Result<void> setSnowWind(float x,float y,float z);
    /** @brief Return to horizontal wind plus the native default fall speed. */
    void clearSnowWind();
    /** @brief Return whether a Pcg snow velocity override is active. */
    bool hasSnowWind() const;
    /** @brief Returns the snow wind x. */
    float getSnowWindX() const;
    /** @brief Returns the snow wind y. */
    float getSnowWindY() const;
    /** @brief Returns the snow wind z. */
    float getSnowWindZ() const;

    /** @brief Sample a bounds-driven interior volume and atomically apply an enter/exit transition. */
    [[nodiscard]] Result<PcgInteriorWeatherTransition> applyInteriorVolume(
        const PcgInteriorWeatherVolume& volume, float x, float y, float z);
    /** @brief Apply a trigger-driven enter/exit event without retaining the volume. */
    [[nodiscard]] Result<PcgInteriorWeatherTransition> setInteriorVolumeState(
        const PcgInteriorWeatherVolume& volume, bool inside);
    /** @brief Clear interior state and restore the last sampled exterior reverb preset. */
    void clearInteriorWeather();
    /** @brief True when inside interior weather. */
    bool isInsideInteriorWeather() const;
    /** @brief True when interior weather collision requested. */
    bool isInteriorWeatherCollisionRequested() const;
    /** @brief Returns the current weather reverb preset. */
    int getCurrentWeatherReverbPreset() const;

    // ---- lightning ----
    /** @brief Sets the lightning enabled. */
    void setLightningEnabled(bool on);
    /** @brief True when lightning enabled. */
    bool isLightningEnabled() const;
    /** @brief Force a bolt strike this frame (useful for manual testing). */
    void strike();
    /** @brief How bright the current flash is (0..1), sampled by the scene for a key light. */
    float getFlash() const;

    // ---- mood (sky / fog), read by the example to tint the scene ----
    /** @brief Sets the sky color. */
    void setSkyColor(float r, float g, float b);
    /** @brief Returns the sky color r. */
    float getSkyColorR() const;
    /** @brief Returns the sky color g. */
    float getSkyColorG() const;
    /** @brief Returns the sky color b. */
    float getSkyColorB() const;
    /** @brief Sets the sun intensity. */
    void setSunIntensity(float v);
    /** @brief Returns the sun intensity. */
    float getSunIntensity() const;
    /** @brief Sets the fog color. */
    void setFogColor(float r, float g, float b);
    /** @brief Returns the fog color r. */
    float getFogColorR() const;
    /** @brief Returns the fog color g. */
    float getFogColorG() const;
    /** @brief Returns the fog color b. */
    float getFogColorB() const;
    /** @brief Sets the fog density. */
    void setFogDensity(float v);
    /** @brief Returns the fog density. */
    float getFogDensity() const;

    /**
     * @brief Enable legacy weather-owned sky and directional lighting.
     *
     * Disable this when DayNight owns the environment; precipitation and
     * lightning geometry continue to render and getFlash() remains available.
     */
    void setEnvironmentEnabled(bool enabled);
    /** @brief True when environment enabled. */
    bool isEnvironmentEnabled() const;

    /** @brief Ambient multiplier that the example should feed into camera.setAmbient(). */
    float getAmbientBrightness() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace eve::weather
