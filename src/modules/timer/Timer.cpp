#include "timer/Timer.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <SDL2/SDL.h>

#include <chrono>
#include <limits>

namespace eve::timer {

Module_IMPL(Timer, new Timer());

Timer::Timer() {
    SDL_InitSubSystem(SDL_INIT_TIMER);
    eve::cap::provide<eve::service::ITimer>(this);
    eve::cap::provide<eve::ITimeSource>(this);
    freq_  = SDL_GetPerformanceFrequency();
    start_ = monotonicNow();
    simulationClock_ = std::make_unique<eve::SimulationClock>(
        *this, eve::Duration::fromNanoseconds(16666667));
}

Timer::~Timer() {
    eve::cap::revoke<eve::ITimeSource>(this);
    eve::cap::revoke<eve::service::ITimer>(this);
}

float Timer::getTime() const {
    auto elapsed = monotonicNow().since(start_);
    if (!elapsed) return 0.f;
    return static_cast<float>(elapsed.value().seconds());
}

float Timer::getDelta() const {
    return static_cast<float>(getDeltaDuration().seconds());
}

float Timer::step() {
    auto result = stepSimulation();
    if (!result) {
        result.ignore("legacy Timer::step facade");
        return 0.f;
    }
    result.ignore("legacy Timer::step facade");
    return getDelta();
}

eve::MonotonicTimestamp Timer::monotonicNow() const {
    if (freq_ == 0) return eve::MonotonicTimestamp::zero();
    const long double counter = static_cast<long double>(SDL_GetPerformanceCounter());
    const long double nanos = counter * 1000000000.0L / static_cast<long double>(freq_);
    if (nanos >= static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
        return eve::MonotonicTimestamp(std::numeric_limits<std::int64_t>::max());
    return eve::MonotonicTimestamp(static_cast<std::int64_t>(nanos));
}

eve::WallClockTimestamp Timer::wallClockNow() const {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    return eve::WallClockTimestamp(nanos);
}

eve::Result<std::vector<eve::SimulationStep>> Timer::stepSimulation() {
    return simulationClock_->sample();
}

eve::SimulationClock& Timer::simulationClock() { return *simulationClock_; }

eve::Duration Timer::getDeltaDuration() const {
    return simulationClock_ ? simulationClock_->lastFrameDelta() : eve::Duration::zero();
}

eve::FrameIndex Timer::getFrameIndex() const {
    return simulationClock_ ? simulationClock_->frameIndex() : eve::FrameIndex::zero();
}

void Timer::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Timer::create, false);
    expose(cls);
}

void Timer::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Timer::getName);
    cls.addFunc("getTime", &Timer::getTime);
    cls.addFunc("getDelta", &Timer::getDelta);
    cls.addFunc("step", &Timer::step);
}

}  // namespace eve::timer
