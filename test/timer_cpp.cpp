#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "timer/Timer.h"

#include <chrono>
#include <thread>

TEST_CASE("timer.getTime.monotonic") {
    auto* t = eve::timer::Timer::create();
    float t0 = t->getTime();
    float t1 = t->getTime();
    CHECK(t1 >= t0);
}

TEST_CASE("timer.step.deltaNonNegative") {
    auto* t = eve::timer::Timer::create();
    t->step();
    CHECK(t->getDelta() >= 0.f);
    float d = t->step();
    CHECK(d >= 0.f);
    CHECK(t->getDelta() == d);
}

TEST_CASE("timer.step.deltaPositiveAfterSleep") {
    auto* t = eve::timer::Timer::create();
    t->step();
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    float delta = t->step();
    CHECK(delta > 0.f);
    CHECK(t->getDelta() == delta);
}
