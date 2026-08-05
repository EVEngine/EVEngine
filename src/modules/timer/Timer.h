#pragma once

#include "common/Module.h"

#include <cstdint>

namespace eve::timer {

class Timer : public Module {
public:
    Module_REG(Timer);
    Timer();
    ~Timer() override = default;

    // float (not double): SimpleSquirrel only pushes double when SQUSEDOUBLE is defined.
    float getTime() const;
    float getDelta() const;
    float step();

private:
    uint64_t freq_  = 0;
    uint64_t start_ = 0;
    uint64_t prev_  = 0;
    float    delta_ = 0.f;
};

}  // namespace eve::timer
