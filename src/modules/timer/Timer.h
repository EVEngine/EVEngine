#pragma once

#include "common/Capability.h"
#include "common/Module.h"
#include "common/ServiceInterfaces.h"

#include <cstdint>

namespace eve::timer {

/**
 * @brief High-resolution frame/elapsed timer backed by SDL_GetPerformanceCounter().
 * Script: `timer <- eve.Timer();`
 */
class Timer : public Module, public eve::service::ITimer {
public:
    Module_REG(Timer);
    Timer();
    ~Timer() override = default;

    /**
     * @brief Seconds since the Timer was created (0 if the timer is unavailable).
     * Returns float (not double): SimpleSquirrel only pushes double when SQUSEDOUBLE is defined.
     */
    float getTime() const;
    /** @brief Seconds between the last two step() calls (0 until the first step). */
    float getDelta() const;
    /** @brief Advances the frame clock and returns the new delta in seconds. */
    float step();

    double elapsedSeconds() override { return getTime(); }

private:
    uint64_t freq_  = 0;
    uint64_t start_ = 0;
    uint64_t prev_  = 0;
    float    delta_ = 0.f;
};

}  // namespace eve::timer
