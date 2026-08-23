#pragma once

#include "common/Module.h"

#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Light3D;
}  // namespace eve::graphics

namespace eve::daynight {

/**
 * @brief DayNight module — a time-of-day cycle that drives the sun, sky and light.
 *
 * Advances a 24-hour clock (configurable speed) and, each frame, repositions
 * the directional sun to match the current solar elevation / azimuth. A
 * procedural sky cubemap (sky gradient + sun disc + night stars) is generated
 * on demand and registered as the 3D IBL environment so both the skybox angle
 * and the reflected sky follow the sun as time flows.
 *
 * At night the module can switch between complementary lighting systems, each
 * implemented as a pool of Light3D entities:
 *   - moonlight   : a cool, low directional light from the moon direction
 *   - starlight   : a faint, slightly-blue ambient that softens the dark
 *   - fire        : a warm point light at a caller-provided campfire position
 *   - fireflies   : a set of warm, gently-drifting point lights
 *
 * Usage from script:
 *   daynight.setTimeOfDay(12.0);       // solar noon
 *   daynight.setSpeed(1.0);            // 1 real hour per simulated hour
 *   daynight.setNightLight("fireflies", true);
 *   // each frame before gfx.render3D():
 *   daynight.update(dt, gfx);
 *   camera.setAmbient(daynight.getAmbientR(), ...);
 */
class DayNight : public Module {
public:
    Module_REG(DayNight);

    DayNight();
    ~DayNight() override;

    /** @brief Idempotent; builds lights / sky cubemap on first call. */
    void init(graphics::Graphics *gfx);
    /** @brief Advance the clock and push sun/sky/light state; before gfx.render3D(). */
    void update(float dt, graphics::Graphics *gfx);

    // ---- clock ----
    void setTimeOfDay(float hours);   // 0..24
    float getTimeOfDay() const;
    void setSpeed(float hoursPerRealHour);   // default 1.0
    float getSpeed() const;
    void setPaused(bool paused);
    bool isPaused() const;
    /** @brief True when the sun is below the horizon (night). */
    bool isNight() const;

    // ---- sun ----
    /** @brief Solar elevation in degrees (max at local noon, ~70°). */
    float getSunElevation() const;
    /** @brief Solar azimuth in degrees, measured clockwise from +Z. */
    float getSunAzimuth() const;
    /** @brief World-space direction pointing AT the sun (normalized). */
    float getSunDirX() const;
    float getSunDirY() const;
    float getSunDirZ() const;
    /** @brief 0..1 sun energy; ramps to 0 below the horizon. */
    float getSunIntensity() const;

    // ---- atmosphere ----
    /** @brief Set aerosol turbidity. 1.5 is very clear; 10 is hazy. */
    void setTurbidity(float turbidity);
    float getTurbidity() const;
    /** @brief Exposure used when mapping physical sky radiance to the RGBA8 sky cubemap. */
    void setSkyExposure(float exposure);
    float getSkyExposure() const;
    /** @brief Relative Mie aerosol density; controls horizon haze and solar halo. */
    void setMieStrength(float strength);
    float getMieStrength() const;

    // ---- sky / ambient (sampled by the scene) ----
    float getSkyR() const;
    float getSkyG() const;
    float getSkyB() const;
    float getAmbientR() const;
    float getAmbientG() const;
    float getAmbientB() const;
    float getAmbientBrightness() const;

    // ---- skybox ----
    void setSkyboxEnabled(bool enabled);
    bool isSkyboxEnabled() const;

    // ---- night lighting ----
    /** @brief Enable a named light system: "moonlight"|"starlight"|"fire"|"fireflies". */
    void setNightLight(const std::string &name, bool enabled);
    bool isNightLight(const std::string &name) const;
    static const char *const kNamedLights[];
    static const int kNamedLightCount;

    /** @brief Position of the campfire point light (fire system). */
    void setFirePosition(float x, float y, float z);
    /** @brief Add one firefly anchor (world space); up to kMaxFireflies. */
    void addFirefly(float x, float y, float z);
    void clearFireflies();
    int getFireflyCount() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace eve::daynight
