#include "daynight/sky/SkyTimeline.h"
#include <cmath>
#include <limits>
#include "zeroerr/unittest.h"

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
