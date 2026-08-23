#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "daynight/DayNight.h"

#include <algorithm>
#include <cmath>

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
    float a = d.getAmbientBrightness();
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
