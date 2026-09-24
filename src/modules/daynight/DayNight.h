#pragma once
#include "common/Export.h"


#include "common/Module.h"
#include "common/Result.h"

#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Light3D;
class ReflectionProbeCapture;
class Volumetric;
}  // namespace eve::graphics

namespace eve::daynight {

/** @brief Persistent manual sun settings projected from Pcg Photo Mode. */
struct PcgManualSunState {
    bool enabled = false;
    float pitchDegrees = 75.f;
    float rotationDegrees = 0.f;
    float intensity = 1.f;
    float red = 1.f;
    float green = 1.f;
    float blue = 1.f;
    float kelvin = 6500.f;
};

/** @brief Persistent Pcg skybox override settings. */
struct PcgSkyboxState {
    bool enabled = false;
    float rotationDegrees = 0.f;
    float exposure = 1.f;
    float tintRed = 0.f;
    float tintGreen = 0.f;
    float tintBlue = 0.f;
};

/** @brief Joint Pcg Photo Mode fog and density-volume state. */
struct PcgFogState {
    float additionalLinearDistance = 0.f;
    float additionalExponentialDensity = 0.f;
    bool overrideFog = false;
    int mode = 1;
    float red = 0.f, green = 0.f, blue = 0.f;
    float density = 0.01f;
    float startDistance = 100.f;
    float endDistance = 1000.f;
    float globalDensityMultiplier = 1.f;
    bool overrideDensityVolume = false;
    float densityAlbedoRed = 1.f, densityAlbedoGreen = 1.f, densityAlbedoBlue = 1.f;
    float densityVolumeDistance = 250.f;
    int densityVolumeEffect = 1;
    int densityVolumeTiling = 3;
};

/** @brief Pcg ambient-gradient colors and global direct-light multiplier. */
struct PcgAmbientLightState {
    bool active = false;
    float intensity = 1.f;
    float skyRed = 0.7027151f, skyGreen = 0.881016f, skyBlue = 1.001631f;
    float equatorRed = 0.6302439f, equatorGreen = 0.7919513f, equatorBlue = 0.85f;
    float groundRed = 0.5f, groundGreen = 0.4142857f, groundBlue = 0.3321428f;
    float globalLightMultiplier = 1.f;
};

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
class EVENGINE_API_WORLD DayNight : public Module {
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
    /** @brief Atmosphere-attenuated direct sunlight red channel, including intensity. */
    float getSunR() const;
    /** @brief Atmosphere-attenuated direct sunlight green channel, including intensity. */
    float getSunG() const;
    /** @brief Atmosphere-attenuated direct sunlight blue channel, including intensity. */
    float getSunB() const;
    /**
     * @brief Atomically replace the persistent Pcg manual-sun state.
     * @param state Candidate settings; angles must be in [0,360], intensity in [0,8],
     * color channels finite and non-negative, and kelvin in [1500,20000].
     * @return Success after the authoritative state changes, or InvalidArgument with no mutation.
     * @thread Game thread only. The value is copied and no callback is invoked.
     */
    [[nodiscard]] Result<void> setPcgManualSun(const PcgManualSunState &state);
    /** @brief Return the authoritative manual-sun settings by value. */
    PcgManualSunState getPcgManualSun() const noexcept;
    /**
     * @brief Atomically replace Pcg's persistent skybox override.
     * @param state Rotation in [0,360], exposure in [0,30], and finite non-negative tint.
     * @return Success, or InvalidArgument without changing the previous state.
     * @thread Game thread only; the value is copied and no callback is invoked.
     */
    [[nodiscard]] Result<void> setPcgSkybox(const PcgSkyboxState &state);
    /** @brief Return the authoritative Pcg skybox override by value. */
    PcgSkyboxState getPcgSkybox() const noexcept;
    /**
     * @brief Atomically replace all Pcg fog and density-volume controls.
     * @param state Candidate joint state using Pcg Photo Mode ranges and enum indices.
     * @return Success, or InvalidArgument without mutating the previous state.
     * @thread Game thread only; the value is copied and no callback is invoked.
     */
    [[nodiscard]] Result<void> setPcgFog(const PcgFogState &state);
    /** @brief Return the authoritative Pcg fog state by value. */
    PcgFogState getPcgFog() const noexcept;
    /**
     * @brief Atomically replace Pcg ambient colors, intensity and direct-light multiplier.
     * @param state Finite non-negative colors, intensity in [0,10], multiplier in [0,5].
     * @return Success, or InvalidArgument without changing the previous state.
     * @thread Game thread only; the value is copied and no callback is invoked.
     */
    [[nodiscard]] Result<void> setPcgAmbientLight(const PcgAmbientLightState &state);
    /** @brief Return the authoritative Pcg ambient-light state by value. */
    PcgAmbientLightState getPcgAmbientLight() const noexcept;

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

    /**
     * @brief Synchronize sun direction and atmosphere-derived fog lighting.
     *
     * This is the supported bridge between the day/night atmosphere and a
     * graphics Volumetric instance. Density and height remain scene/weather
     * controls; sky color, sun direction, exposure and animation time are
     * supplied by this module.
     * @param fog Target volumetric fog or cloud renderer; null is ignored.
     */
    void applyAtmosphere(graphics::Volumetric *fog) const;
    /**
     * @brief Synchronize linear directional sky radiance and sky IBL to a reflection probe.
     * @param probe Target runtime probe; null is ignored.
     */
    void applyReflectionProbeSky(graphics::ReflectionProbeCapture *probe) const;

    // ---- weather coupling ----
    /**
     * @brief Apply cloud cover and lightning exposure to the unified sky/lighting model.
     * @param cloudiness Cloud attenuation in [0,1].
     * @param lightningFlash Transient lightning energy in [0,1].
     */
    void setWeatherInfluence(float cloudiness, float lightningFlash);
    float getWeatherCloudiness() const;
    float getWeatherFlash() const;

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
