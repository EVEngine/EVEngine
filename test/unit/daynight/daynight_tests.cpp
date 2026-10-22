#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_SkySolarOrbit)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include "daynight/sky/SkySolarOrbit.h"
#include "zeroerr/unittest.h"
// SkySolarOrbit.cpp
namespace test_SkySolarOrbit {
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
}  // namespace test_SkySolarOrbit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_SkyTimeline)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include "daynight/sky/SkyTimeline.h"
#include "zeroerr/unittest.h"
// SkyTimeline.cpp
namespace test_SkyTimeline {
using namespace eve::daynight;
namespace {
eve::Duration seconds(int64_t value) { return eve::Duration::fromNanoseconds(value * 1000000000); }
}  // namespace

TEST_CASE("daynight.sky timeline integrates weather wind across transition boundaries") {
    SkyWeather initial;
    initial.windXZ = {2, -1};
    auto created   = SkyTimeline::create({23, .1}, initial);
    REQUIRE(created.ok());
    auto       large = std::move(created).takeValue();
    auto       small = large;
    SkyWeather target;
    target.cloudCoverage = 10;
    target.rain          = 1;
    target.windXZ        = {6, 3};
    REQUIRE(large.transitionTo(target, seconds(10)).ok());
    REQUIRE(small.transitionTo(target, seconds(10)).ok());
    REQUIRE(large.advance(seconds(15)).ok());
    for (int step = 0; step < 150; ++step) REQUIRE(small.advance(eve::Duration::fromNanoseconds(100000000)).ok());
    const auto a = large.frame(), b = small.frame();
    REQUIRE(std::abs(a.hour - .5) < 1e-9);
    REQUIRE(std::abs(a.cloudTravelXZ[0] - 70) < 1e-9);
    REQUIRE(std::abs(a.cloudTravelXZ[1] - 25) < 1e-9);
    REQUIRE(std::abs(a.cloudTravelXZ[0] - b.cloudTravelXZ[0]) < 1e-9);
    REQUIRE(std::abs(a.cloudTravelXZ[1] - b.cloudTravelXZ[1]) < 1e-9);
    REQUIRE(a.weather.cloudCoverage == 10);
    REQUIRE(a.weather.rain == 1);
}

TEST_CASE("daynight.sky interrupted weather transition preserves position and independent state") {
    auto created = SkyTimeline::create({12, 0}, {});
    REQUIRE(created.ok());
    auto       sky         = std::move(created).takeValue();
    const auto independent = sky;
    SkyWeather storm;
    storm.cloudCoverage = 10;
    storm.windXZ        = {10, 0};
    REQUIRE(sky.transitionTo(storm, seconds(10)).ok());
    REQUIRE(sky.advance(seconds(5)).ok());
    REQUIRE(sky.frame().weather.cloudCoverage == 5);
    REQUIRE(sky.frame().cloudTravelXZ[0] == 12.5);
    REQUIRE(sky.transitionTo({}, seconds(5)).ok());
    REQUIRE(sky.frame().cloudTravelXZ[0] == 12.5);
    REQUIRE(sky.advance(seconds(5)).ok());
    REQUIRE(sky.frame().cloudTravelXZ[0] == 25);
    REQUIRE(sky.frame().weather.cloudCoverage == 0);
    REQUIRE(sky.frame().hour == 12);
    REQUIRE(independent.frame().elapsedSeconds == 0);
    REQUIRE(independent.frame().cloudTravelXZ[0] == 0);
    REQUIRE(sky.transitionTo(storm, seconds(0)).ok());
    REQUIRE(sky.frame().cloudTravelXZ[0] == 25);
    REQUIRE(sky.frame().weather.cloudCoverage == 10);
}

TEST_CASE("daynight.sky invalid commands leave clock and active transition unchanged") {
    REQUIRE(!SkyTimeline::create({24, 0}, {}).ok());
    auto created = SkyTimeline::create({}, {});
    REQUIRE(created.ok());
    auto       sky = std::move(created).takeValue();
    SkyWeather storm;
    storm.cloudCoverage = 10;
    REQUIRE(sky.transitionTo(storm, seconds(10)).ok());
    REQUIRE(sky.advance(seconds(2)).ok());
    auto invalid      = storm;
    invalid.windXZ[0] = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!sky.transitionTo(invalid, seconds(0)).ok());
    REQUIRE(!sky.advance(seconds(-1)).ok());
    REQUIRE(!sky.advance(eve::Duration::fromNanoseconds(std::numeric_limits<int64_t>::max())).ok());
    REQUIRE(sky.frame().elapsedSeconds == 2);
    REQUIRE(sky.advance(seconds(3)).ok());
    REQUIRE(sky.frame().weather.cloudCoverage == 5);
}
}  // namespace test_SkyTimeline
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_daynight)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include "daynight/DayNight.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// daynight.cpp
namespace test_daynight {
using namespace eve::daynight;

TEST_CASE("daynight.clockRoundTrip") {
    DayNight d;
    d.setTimeOfDay(6.0f);
    CHECK(d.getTimeOfDay() == 6.0f);
    d.setTimeOfDay(23.5f);
    CHECK(d.getTimeOfDay() == 23.5f);
    d.setTimeOfDay(-1.0f);  // wraps into the valid 0..24 range
    CHECK(d.getTimeOfDay() >= 0.0f);
    CHECK(d.getTimeOfDay() < 24.0f);
}

TEST_CASE("daynight.clockSpeedAndPause") {
    DayNight d;
    CHECK(d.getSpeed() >= 0.0f);
    d.setSpeed(2.0f);
    CHECK(d.getSpeed() == 2.0f);
    d.setSpeed(-1.0f);  // clamped non-negative
    CHECK(d.getSpeed() == 0.0f);
    d.setPaused(true);
    CHECK(d.isPaused());
    d.setPaused(false);
    CHECK(!d.isPaused());
}

TEST_CASE("daynight.sunElevationPeaksAtNoon") {
    DayNight d;
    d.setTimeOfDay(12.0f);
    CHECK(d.getSunElevation() > 0.0f);
    // Noon is the day-time peak; sun energy should be at or near max.
    CHECK(d.getSunIntensity() > 0.9f);
    CHECK(!d.isNight());
}

TEST_CASE("daynight.nightBelowHorizon") {
    DayNight d;
    d.setTimeOfDay(0.0f);  // midnight
    CHECK(d.getSunElevation() < 0.0f);
    CHECK(d.getSunIntensity() == 0.0f);
    CHECK(d.isNight());
}

TEST_CASE("daynight.sunDirectionNormalized") {
    DayNight d;
    d.setTimeOfDay(9.0f);
    const float x = d.getSunDirX(), y = d.getSunDirY(), z = d.getSunDirZ();
    const float len = std::sqrt(x * x + y * y + z * z);
    CHECK(std::fabs(len - 1.0f) < 1e-3f);
}

TEST_CASE("daynight.sunColorWarmsNearHorizon") {
    DayNight d;
    d.setTimeOfDay(12.0f);
    const float noonBlueRatio = d.getSunB() / std::max(d.getSunR(), 1e-6f);
    d.setTimeOfDay(7.0f);
    CHECK(d.getSunR() >= d.getSunG());
    CHECK(d.getSunG() >= d.getSunB());
    CHECK(d.getSunB() / std::max(d.getSunR(), 1e-6f) < noonBlueRatio);
    d.setTimeOfDay(0.0f);
    CHECK(d.getSunR() == 0.0f);
    CHECK(d.getSunG() == 0.0f);
    CHECK(d.getSunB() == 0.0f);
}

TEST_CASE("daynight.ambientBounded") {
    DayNight d;
    float    a = d.getAmbientBrightness();
    CHECK(a >= 0.0f);
    CHECK(a <= 1.0f);
}

TEST_CASE("daynight.nightLightToggles") {
    DayNight d;
    CHECK(d.isNightLight("moonlight"));
    d.setNightLight("moonlight", false);
    CHECK(!d.isNightLight("moonlight"));
    d.setNightLight("fire", true);
    CHECK(d.isNightLight("fire"));
    // Unknown name is ignored without throwing.
    d.setNightLight("laser", true);
    CHECK(!d.isNightLight("laser"));
}

TEST_CASE("daynight.fireflyPoolCapped") {
    DayNight d;
    for (int i = 0; i < 20; ++i) {
        d.addFirefly(float(i), 1.0f, 0.0f);
    }
    CHECK(d.getFireflyCount() <= 8);
    d.clearFireflies();
    CHECK(d.getFireflyCount() == 0);
}

TEST_CASE("daynight.firePositionStored") {
    DayNight d;
    d.setFirePosition(1.0f, 0.5f, -2.0f);
    CHECK(d.getSkyR() >= 0.0f);  // no throw; position is consumed on init
}

TEST_CASE("daynight.atmosphereParametersClampAndRoundTrip") {
    DayNight d;
    d.setTurbidity(4.5f);
    d.setSkyExposure(1.4f);
    d.setMieStrength(0.8f);
    CHECK(std::fabs(d.getTurbidity() - 4.5f) < 1e-5f);
    CHECK(std::fabs(d.getSkyExposure() - 1.4f) < 1e-5f);
    CHECK(std::fabs(d.getMieStrength() - 0.8f) < 1e-5f);
    d.setTurbidity(-10.f);
    d.setSkyExposure(100.f);
    d.setMieStrength(-1.f);
    CHECK(d.getTurbidity() >= 1.5f);
    CHECK(d.getSkyExposure() <= 8.f);
    CHECK(d.getMieStrength() == 0.f);
}
}  // namespace test_daynight
#endif
