#include "timer/Timer.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <SDL2/SDL.h>

namespace eve::timer {

Module_IMPL(Timer, new Timer());

Timer::Timer() {
    SDL_InitSubSystem(SDL_INIT_TIMER);
    freq_  = SDL_GetPerformanceFrequency();
    start_ = SDL_GetPerformanceCounter();
    prev_  = start_;
    delta_ = 0.f;
}

float Timer::getTime() const {
    if (freq_ == 0) return 0.f;
    return float(double(SDL_GetPerformanceCounter() - start_) / double(freq_));
}

float Timer::getDelta() const { return delta_; }

float Timer::step() {
    if (freq_ == 0) {
        delta_ = 0.f;
        return delta_;
    }
    uint64_t now = SDL_GetPerformanceCounter();
    delta_       = float(double(now - prev_) / double(freq_));
    prev_        = now;
    return delta_;
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
