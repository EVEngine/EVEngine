#include "common/SmoothMax.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

using eve::math::smoothMax;

TEST_CASE("math.smoothMax.valuesAndRange") {
    CHECK_EQ(smoothMax(10.f, 10.f, 4.f), 11.f);
    CHECK_EQ(smoothMax(-10.f, -10.f, 4.f), -9.f);
    CHECK_EQ(smoothMax(3.f, 7.f, 0.f), 7.f);
    CHECK_EQ(smoothMax(3.f, 7.f, 4.f), 7.f);
    CHECK_EQ(smoothMax(3.f, 8.f, 4.f), 8.f);
    for (int i = -100; i <= 100; ++i) {
        const float a     = static_cast<float>(i) * 0.1f;
        const float value = smoothMax(a, 2.f, 4.f);
        CHECK_EQ(value, smoothMax(2.f, a, 4.f));
        CHECK(value >= std::max(a, 2.f));
        CHECK(value <= std::max(a, 2.f) + 1.f);
        CHECK(smoothMax(a + 0.01f, 2.f, 4.f) >= value);
        CHECK(std::abs(smoothMax(a + 8.f, 10.f, 4.f) - value - 8.f) < 2e-6f);
    }
}

TEST_CASE("math.smoothMax.continuousSlope") {
    constexpr float delta = 0.001f;
    for (const float x : {-4.f, 0.f, 4.f}) {
        const float center = smoothMax(x, 0.f, 4.f);
        const float left   = (center - smoothMax(x - delta, 0.f, 4.f)) / delta;
        const float right  = (smoothMax(x + delta, 0.f, 4.f) - center) / delta;
        CHECK(std::abs(left - right) < 0.002f);
        CHECK(std::abs(left - (x + 4.f) / 8.f) < 0.002f);
    }
}

TEST_CASE("math.smoothMax.mountainFootprint") {
    // A user-authored paraboloid falls below the terrain before its footprint ends.
    for (int i = -100; i <= 100; ++i) {
        const float x        = static_cast<float>(i);
        const float terrain  = 3.f + std::sin(x * 0.05f);
        const float mountain = 25.f - 0.01f * x * x;
        const float height   = smoothMax(terrain, mountain, 4.f);
        CHECK(height >= terrain);
        if (std::abs(x) >= 60.f) CHECK_EQ(height, terrain);
        if (std::abs(x) <= 20.f) CHECK_EQ(height, mountain);
    }
}

TEST_CASE("math.smoothMax.extremeFiniteInputs") {
    const float largest = std::numeric_limits<float>::max();
    CHECK_EQ(smoothMax(largest, -largest, largest), largest);
    CHECK_EQ(smoothMax(-largest, largest, largest), largest);
    CHECK_EQ(smoothMax(0.f, 0.f, largest), largest * 0.25f);
    CHECK(std::isinf(smoothMax(largest, largest, largest)));
    CHECK_EQ(smoothMax(0.f, 1.f, std::numeric_limits<float>::min()), 1.f);
}
