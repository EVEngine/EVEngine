#pragma once
#include <array>
#include <memory>
#include "daynight/sky/SkyTimeline.h"

namespace eve::graphics {
class Graphics;
struct SkyAtmosphereParameters;
struct SkyWispsLayer;
}  // namespace eve::graphics
namespace eve::daynight {
/** @brief Source cloud-time controls; phase is additive, speed is source time per injected second.
 * @details Weather wind multiplier is explicit; windXZ metre travel is a separate simulation quantity. */
struct SkyCloudMotion {
    double phase = 0, speed = 0, timeScale = 1, windMultiplier = 1;
    /** @brief Reject nonfinite values, phase outside +/-1e6 and multipliers outside [0,1000]. */
    [[nodiscard]] Result<void> validate() const;
};
/** @brief Independent runtime settings; not a serialized profile or a legacy DayNight override. */
struct SkyRuntimeSettings {
    SkyClock             clock;
    SkyWeather           weather;
    SkyCloudMotion       cloudMotion;
    double               sunPitchDegrees = 30;
    double               sunYawDegrees   = 0;
    std::array<float, 3> sunIrradiance{5, 5, 5};
};
/** @brief Copied simulation and evaluated solar lighting used by every view. */
struct SkyRuntimeFrame {
    SkyFrame              simulation;
    double                cloudTime       = 0;
    float                 wispsMorphPhase = 0;
    std::array<double, 3> sunDirection{};
    std::array<float, 3>  sunIrradiance{};
};
/** @brief Owns independent sky simulation and its prepared atmospheric contributor.
 * @details Exactly one timeline advances from caller-injected time. All render views
 * consume the resulting lighting and authored wisps morph phase without advancing simulation.
 * Weather transitions are simulated; volumetric clouds, weather-driven fog and precipitation are not yet rendered.
 * No legacy DayNight state or scene light is changed.
 * @thread Graphics thread, outside contributor dispatch, including destruction.
 * @lifetime Owns all state and the render pass; provider-first retirement is detected
 * through its weak lifetime token, and pass-first destruction unregisters contributors.
 * @reentrancy No script callbacks, locks or retained caller configuration pointers. */
class SkyRuntime {
public:
    /** @brief Validate settings and prepare all atmospheric resources before publication.
     * @return Detached owning runtime or failure without published contributors.
     * @param wisps Optional source data borrowed synchronously; GPU preparation copies it.
     * @cost CPU LUT baking and GPU pipeline/upload work; amortize over configuration lifetime. */
    [[nodiscard]] static Result<std::unique_ptr<SkyRuntime>> prepare(
        graphics::Graphics& graphics, const SkyRuntimeSettings& settings,
        const graphics::SkyAtmosphereParameters& atmosphere, const graphics::SkyWispsLayer* wisps = nullptr);
    ~SkyRuntime();
    SkyRuntime(const SkyRuntime&)            = delete;
    SkyRuntime& operator=(const SkyRuntime&) = delete;
    /** @brief Advance one simulation clock and atomically publish solar lighting/cloud phase; invalid time preserves
     * all. */
    [[nodiscard]] Result<void> advance(Duration duration);
    /** @brief Start a validated weather transition without advancing simulation time. */
    [[nodiscard]] Result<void> transitionTo(const SkyWeather& target, Duration duration);
    /** @brief Copy current simulation and solar lighting without advancing either. */
    [[nodiscard]] SkyRuntimeFrame frame() const noexcept;
    /** @brief Attach the prepared main-view and reflection contributors, idempotently. */
    [[nodiscard]] Result<void> attach();
    /** @brief Unregister both contributors; simulation and prepared resources remain owned. */
    void detach() noexcept;

private:
    SkyRuntime();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::daynight
