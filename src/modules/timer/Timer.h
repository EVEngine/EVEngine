#pragma once

#include "common/Capability.h"
#include "common/Module.h"
#include "common/ServiceInterfaces.h"
#include "common/Time.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace eve::timer {

/**
 * @brief High-resolution frame/elapsed timer backed by SDL_GetPerformanceCounter().
 * Script: `timer <- eve.Timer();`
 */
class Timer : public Module, public eve::service::ITimer, public eve::ITimeSource {
public:
    Module_REG(Timer);
    Timer();
    ~Timer() override;

    /**
     * @brief Seconds since the Timer was created (0 if the timer is unavailable).
     * Returns float (not double): SimpleSquirrel only pushes double when SQUSEDOUBLE is defined.
     */
    float getTime() const;
    /** @brief Seconds between the last two step() calls (0 until the first step). */
    float getDelta() const;
    /** @brief Advances the frame clock and returns the new delta in seconds. */
    float step();

    /**
     * @brief Read the monotonic timestamp used by this timer.
     * @remarks Owner-thread only; this timestamp is not persistent simulation state.
     */
    [[nodiscard]] eve::MonotonicTimestamp monotonicNow() const override;
    /**
     * @brief Read wall-clock metadata from the system clock.
     * @remarks Owner-thread only and never used to advance simulation state.
     */
    [[nodiscard]] eve::WallClockTimestamp wallClockNow() const override;

    /** @brief Advance the injected fixed-step clock and return emitted simulation steps. */
    [[nodiscard]] eve::Result<std::vector<eve::SimulationStep>> stepSimulation();
    /** @brief Return the current deterministic simulation clock. Borrowed for Timer lifetime. */
    eve::SimulationClock& simulationClock();
    /** @brief Return the last source duration observed by step(). */
    eve::Duration getDeltaDuration() const;
    /** @brief Return the presentation frame ordinal maintained by the simulation clock. */
    eve::FrameIndex getFrameIndex() const;

    double elapsedSeconds() override { return getTime(); }

private:
    uint64_t                              freq_ = 0;
    eve::MonotonicTimestamp               start_;
    std::unique_ptr<eve::SimulationClock> simulationClock_;
};

}  // namespace eve::timer
