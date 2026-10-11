#include "daynight/sky/SkySolarOrbit.h"
#include <cmath>
#include <limits>
#include "zeroerr/unittest.h"

using eve::daynight::SkySolarOrbit;

TEST_CASE("daynight.sky solar orbit matches Unreal resolved manual orbit samples") {
    // UE 5.8.2 / UDS 5.8 DirectionalLightComponent forward vectors, negated
    // and mapped from Z-up to Y-up. These are measured values, not formula outputs.
    struct Sample {
        double                hour, pitch, yaw;
        std::array<double, 3> direction;
    };
    const Sample samples[] = {{0, 30, 0, {.5000000067, -.8660253999, 0}},
                              {3, 30, 0, {.3535533835, -.6123724562, .7071067670}},
                              {6, 30, 0, {0, 0, 1}},
                              {9, 30, 0, {-.3535533835, .6123724562, .7071067670}},
                              {12, 30, 0, {-.5000000067, .8660253999, 0}},
                              {15, 30, 0, {-.3535533835, .6123724562, -.7071067670}},
                              {18, 30, 0, {0, 0, -1}},
                              {21, 30, 0, {.3535533835, -.6123724562, -.7071067670}},
                              {12, 0, 0, {0, 1, 0}},
                              {12, 60, 0, {-.8660253999, .5000000067, 0}},
                              {12, 30, 90, {0, .8660253999, -.5000000067}},
                              {6, 30, 90, {-1, 0, 0}},
                              {5.5, 30, 0, {.0652630909, -.1130389991, .9914448616}}};
    for (const auto& sample : samples) {
        auto orbit = SkySolarOrbit::create(sample.pitch, sample.yaw);
        REQUIRE(orbit.ok());
        auto direction = orbit.value().direction(sample.hour);
        REQUIRE(direction.ok());
        double lengthSquared = 0;
        for (size_t axis = 0; axis < 3; ++axis) {
            REQUIRE(std::abs(direction.value()[axis] - sample.direction[axis]) < 1e-6);
            lengthSquared += direction.value()[axis] * direction.value()[axis];
        }
        REQUIRE(std::abs(lengthSquared - 1) < 1e-12);
    }
}

TEST_CASE("daynight.sky solar orbit rejects invalid configuration and evaluation") {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!SkySolarOrbit::create(nan, 0).ok());
    REQUIRE(!SkySolarOrbit::create(0, nan).ok());
    REQUIRE(!SkySolarOrbit::create(91, 0).ok());
    REQUIRE(!SkySolarOrbit::create(0, 361).ok());
    auto orbit = SkySolarOrbit::create(30, 0);
    REQUIRE(orbit.ok());
    REQUIRE(!orbit.value().direction(-1).ok());
    REQUIRE(!orbit.value().direction(24).ok());
    REQUIRE(!orbit.value().direction(nan).ok());
    REQUIRE(orbit.value().direction(12).ok());
}
