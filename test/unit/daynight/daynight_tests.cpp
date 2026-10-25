// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "SkySolarOrbit.cpp",
    "SkyTimeline.cpp",
    "daynight.cpp"
  ]
}
*/
// clang-format on

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_SkySolarOrbit)
#include "daynight/sky/SkySolarOrbit.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>
#include <limits>

namespace test_SkySolarOrbit {
using eve::daynight::SkySolarOrbit;

TEST_CASE("daynight.sky.orbit_referenceAndValidationContract") {
    SUB_CASE("reference hours return unit directions") {
        struct Sample {
            double                hour;
            double                pitch;
            double                yaw;
            std::array<double, 3> direction;
        };
        // Measured UE 5.8.2 manual-orbit vectors, mapped from Z-up into Y-up.
        const Sample samples[] = {
            {0, 30, 0, {.5000000067, -.8660253999, 0}},
            {6, 30, 0, {0, 0, 1}},
            {12, 30, 0, {-.5000000067, .8660253999, 0}},
            {18, 30, 0, {0, 0, -1}},
            {12, 0, 0, {0, 1, 0}},
            {12, 60, 0, {-.8660253999, .5000000067, 0}},
            {12, 30, 90, {0, .8660253999, -.5000000067}},
            {5.5, 30, 0, {.0652630909, -.1130389991, .9914448616}},
        };
        for (const auto& sample : samples) {
            const auto orbit = SkySolarOrbit::create(sample.pitch, sample.yaw);
            REQUIRE(orbit.ok());
            const auto actual = orbit.value().direction(sample.hour);
            REQUIRE(actual.ok());
            double squared = 0;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                CHECK(std::abs(actual.value()[axis] - sample.direction[axis]) < 1e-6);
                squared += actual.value()[axis] * actual.value()[axis];
            }
            CHECK(std::abs(squared - 1) < 1e-12);
        }
    };
    SUB_CASE("configuration and hour outside contract are rejected") {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        for (double pitch : {-91., 91., nan}) CHECK(!SkySolarOrbit::create(pitch, 0).ok());
        for (double yaw : {-361., 361., nan}) CHECK(!SkySolarOrbit::create(0, yaw).ok());
        const auto orbit = SkySolarOrbit::create(30, 0);
        REQUIRE(orbit.ok());
        CHECK(orbit.value().direction(0).ok());
        CHECK(orbit.value().direction(23.999).ok());
        for (double hour : {-1., 24., nan}) CHECK(!orbit.value().direction(hour).ok());
    };
}
}  // namespace test_SkySolarOrbit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_SkyTimeline)
#include "PrivateTestScope.h"  // Remove Windows SDK's near macro before the local comparison helper.
#include "daynight/sky/SkyTimeline.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace test_SkyTimeline {
using namespace eve::daynight;
namespace {
eve::Duration seconds(int64_t value) { return eve::Duration::fromNanoseconds(value * 1000000000); }
void          near(double actual, double expected) { CHECK(std::abs(actual - expected) < 1e-9); }
}  // namespace

TEST_CASE("daynight.sky.timeline_transitionAndFailureContract") {
    SUB_CASE("wind integrates across transition end and clock wrap") {
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
        const auto a = large.frame();
        const auto b = small.frame();
        near(a.hour, .5);
        near(a.cloudTravelXZ[0], 70);
        near(a.cloudTravelXZ[1], 25);
        near(a.cloudTravelXZ[0], b.cloudTravelXZ[0]);
        near(a.cloudTravelXZ[1], b.cloudTravelXZ[1]);
        near(a.weather.cloudCoverage, 10);
        near(a.weather.rain, 1);
    };
    SUB_CASE("interrupted transition starts at current sample") {
        auto created = SkyTimeline::create({12, 0}, {});
        REQUIRE(created.ok());
        auto       sky         = std::move(created).takeValue();
        const auto independent = sky;
        SkyWeather storm;
        storm.cloudCoverage = 10;
        storm.windXZ        = {10, 0};
        REQUIRE(sky.transitionTo(storm, seconds(10)).ok());
        REQUIRE(sky.advance(seconds(5)).ok());
        near(sky.frame().weather.cloudCoverage, 5);
        near(sky.frame().cloudTravelXZ[0], 12.5);
        REQUIRE(sky.transitionTo({}, seconds(5)).ok());
        REQUIRE(sky.advance(seconds(5)).ok());
        near(sky.frame().weather.cloudCoverage, 0);
        near(sky.frame().cloudTravelXZ[0], 25);
        near(sky.frame().hour, 12);
        near(independent.frame().elapsedSeconds, 0);
        near(independent.frame().cloudTravelXZ[0], 0);
        REQUIRE(sky.transitionTo(storm, seconds(0)).ok());
        near(sky.frame().weather.cloudCoverage, 10);
        near(sky.frame().cloudTravelXZ[0], 25);
    };
    SUB_CASE("invalid command preserves clock and active ramp") {
        CHECK(!SkyTimeline::create({24, 0}, {}).ok());
        CHECK(!SkyTimeline::create({0, -1}, {}).ok());
        auto created = SkyTimeline::create({}, {});
        REQUIRE(created.ok());
        auto       sky = std::move(created).takeValue();
        SkyWeather storm;
        storm.cloudCoverage = 10;
        REQUIRE(sky.transitionTo(storm, seconds(10)).ok());
        REQUIRE(sky.advance(seconds(2)).ok());
        auto invalid      = storm;
        invalid.windXZ[0] = std::numeric_limits<double>::quiet_NaN();
        CHECK(!sky.transitionTo(invalid, seconds(0)).ok());
        CHECK(!sky.advance(seconds(-1)).ok());
        CHECK(!sky.advance(eve::Duration::fromNanoseconds(std::numeric_limits<int64_t>::max())).ok());
        near(sky.frame().elapsedSeconds, 2);
        REQUIRE(sky.advance(seconds(3)).ok());
        near(sky.frame().weather.cloudCoverage, 5);
    };
}
}  // namespace test_SkyTimeline
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_daynight)
#include "daynight/DayNight.h"
#include "zeroerr/unittest.h"

#include <limits>

namespace test_daynight {
using eve::daynight::DayNight;

TEST_CASE("daynight.clockLightingAndAtmosphereContract") {
    SUB_CASE("clock setters wrap hours clamp speed and track pause") {
        DayNight clock;
        CHECK(clock.getSunElevation() > 0.f);
        clock.setTimeOfDay(24.f);
        CHECK_EQ(clock.getTimeOfDay(), 0.f);
        clock.setTimeOfDay(-1.f);
        CHECK_EQ(clock.getTimeOfDay(), 23.f);
        CHECK(clock.isNight());
        CHECK_EQ(clock.getSunIntensity(), 0.f);
        clock.setTimeOfDay(6.5f);
        CHECK_EQ(clock.getTimeOfDay(), 6.5f);
        clock.setSpeed(2.f);
        CHECK_EQ(clock.getSpeed(), 2.f);
        clock.setSpeed(-1.f);
        CHECK_EQ(clock.getSpeed(), 0.f);
        clock.setPaused(true);
        CHECK(clock.isPaused());
        clock.setPaused(false);
        CHECK(!clock.isPaused());
    };
    SUB_CASE("night light names are independent and unknown name is ignored") {
        DayNight lighting;
        CHECK(lighting.isNightLight("moonlight"));
        lighting.setNightLight("moonlight", false);
        CHECK(!lighting.isNightLight("moonlight"));
        lighting.setNightLight("fire", true);
        CHECK(lighting.isNightLight("fire"));
        lighting.setNightLight("unknown", true);
        CHECK(!lighting.isNightLight("unknown"));
        CHECK(lighting.isNightLight("fire"));
    };
    SUB_CASE("firefly pool is bounded and clear is idempotent") {
        DayNight lighting;
        for (int index = 0; index < 20; ++index) lighting.addFirefly(static_cast<float>(index), 1.f, 0.f);
        CHECK_EQ(lighting.getFireflyCount(), 8);
        lighting.clearFireflies();
        CHECK_EQ(lighting.getFireflyCount(), 0);
        lighting.clearFireflies();
        CHECK_EQ(lighting.getFireflyCount(), 0);
    };
    SUB_CASE("atmosphere setters round trip and clamp boundaries") {
        DayNight lighting;
        lighting.setTurbidity(4.5f);
        lighting.setSkyExposure(1.4f);
        lighting.setMieStrength(.8f);
        CHECK_EQ(lighting.getTurbidity(), 4.5f);
        CHECK_EQ(lighting.getSkyExposure(), 1.4f);
        CHECK_EQ(lighting.getMieStrength(), .8f);
        lighting.setTurbidity(-10.f);
        lighting.setSkyExposure(100.f);
        lighting.setMieStrength(-1.f);
        CHECK_EQ(lighting.getTurbidity(), 1.5f);
        CHECK_EQ(lighting.getSkyExposure(), 8.f);
        CHECK_EQ(lighting.getMieStrength(), 0.f);
    };
    SUB_CASE("manual sun rejects invalid candidate without mutating prior state") {
        DayNight                         lighting;
        eve::daynight::PcgManualSunState accepted;
        accepted.enabled      = true;
        accepted.pitchDegrees = 45.f;
        accepted.intensity    = 3.f;
        REQUIRE(lighting.setPcgManualSun(accepted).ok());
        auto invalid   = accepted;
        invalid.kelvin = std::numeric_limits<float>::quiet_NaN();
        CHECK(!lighting.setPcgManualSun(invalid).ok());
        const auto current = lighting.getPcgManualSun();
        CHECK(current.enabled);
        CHECK_EQ(current.pitchDegrees, 45.f);
        CHECK_EQ(current.intensity, 3.f);
        CHECK_EQ(current.kelvin, accepted.kelvin);
        CHECK(std::abs(lighting.getSunElevation() - 45.f) < 0.0001f);
        CHECK_EQ(lighting.getSunIntensity(), 3.f);
        lighting.setTimeOfDay(0.f);
        CHECK(std::abs(lighting.getSunElevation() - 45.f) < 0.0001f);
        accepted.enabled = false;
        REQUIRE(lighting.setPcgManualSun(accepted).ok());
        CHECK_EQ(lighting.getSunElevation(), -70.f);
        CHECK_EQ(lighting.getSunIntensity(), 0.f);
        CHECK(lighting.isNight());
    };
}
}  // namespace test_daynight
#endif
