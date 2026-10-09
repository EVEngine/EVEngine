#pragma once

#include <array>
#include "common/Result.h"
#include "common/Time.h"

namespace eve::daynight {
/** @brief Independent sky weather values; distances are metres and wind is metres/second. */
struct SkyWeather {
    double                cloudCoverage = 0;  // UDS range [0,10].
    double                fog           = 0;
    double                rain          = 0;
    double                snow          = 0;
    std::array<double, 2> windXZ{0, 0};
};

/** @brief Runtime clock configuration, independent of the legacy DayNight module. */
struct SkyClock {
    double initialHour    = 12;
    double hoursPerSecond = 1.0 / 3600.0;
};

/** @brief Copied sky simulation state shared by main-view and reflection consumers. */
struct SkyFrame {
    double                elapsedSeconds = 0;
    double                hour           = 12;
    SkyWeather            weather;
    std::array<double, 2> cloudTravelXZ{0, 0};
};

/**
 * @brief Owns one independent sky clock, weather transition and integrated cloud travel.
 * @details This is a value-owned simulation, not an ECS System or global singleton.
 * The caller injects elapsed time; no wall clock or random source is consulted.
 * Equal commands at equal simulation times produce equal results to double-precision
 * rounding; tests allow 1e-9 at ordinary scene durations. No bitwise cross-platform claim.
 * Values are runtime-only; this class does not define a persistent file format.
 * @thread One simulation thread owns mutations. Copy frame() for render consumers.
 * @reentrancy No callbacks, services, locks or borrowed objects are retained.
 */
class SkyTimeline {
public:
    /** @brief Construct validated state. Clock hours are [0,24), speed [0,24] hours/second.
     * @return Owning timeline, or InvalidArgument without publishing any state. */
    [[nodiscard]] static Result<SkyTimeline> create(const SkyClock& clock, const SkyWeather& weather);
    /** @brief Advance by a nonnegative injected simulation duration, rejecting overflow.
     * @return Success, or InvalidArgument leaving the previous state unchanged. */
    [[nodiscard]] Result<void> advance(Duration duration);
    /** @brief Start a linear weather transition from the current sampled weather.
     * @param target Coverage [0,10], fog/rain/snow [0,1], each wind axis [-1000,1000].
     * @param duration Nonnegative simulation duration; zero applies immediately without moving clouds.
     * @return Success, or InvalidArgument without changing the active transition. */
    [[nodiscard]] Result<void> transitionTo(const SkyWeather& target, Duration duration);
    /** @brief Copy the authoritative evaluated state; no provider pointers escape. */
    [[nodiscard]] SkyFrame frame() const noexcept;

private:
    SkyTimeline(const SkyClock& clock, const SkyWeather& weather);
    SkyClock              clock_;
    SkyWeather            from_, to_;
    Duration              elapsed_;
    Duration              transitionStart_;
    Duration              transitionDuration_;
    std::array<double, 2> travelOrigin_{0, 0};
};
}  // namespace eve::daynight
