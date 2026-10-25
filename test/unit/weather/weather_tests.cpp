#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_snow)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "weather/Snow.h"
#include "weather/SnowField.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

namespace test_snow {
using eve::weather::SnowField;

// Grid contract: resize establishes a zero-filled field; writes clamp to
// [0,1], out-of-range accesses are inert, and mutations set the dirty flag.
TEST_CASE("snow.spec.grid_bounds_clamping_and_dirty") {
    SnowField field(8, 6);
    REQUIRE_EQ(field.getWidth(), 8);
    CHECK_EQ(field.getHeight(), 6);
    CHECK_EQ(field.height(-1, 0), 0.f);
    CHECK_EQ(field.height(3, 2), 0.f);
    field.clearDirty();
    field.setHeight(99, 99, .7f);
    CHECK(!field.isDirty());
    field.setHeight(3, 2, 2.f);
    CHECK(field.isDirty());
    CHECK_EQ(field.height(3, 2), 1.f);
    field.setHeight(3, 2, -.5f);
    CHECK_EQ(field.height(3, 2), 0.f);
    field.fill(.5f);
    CHECK_EQ(field.height(0, 0), .5f);
    field.resize(4, 3);
    CHECK_EQ(field.getWidth(), 4);
    CHECK_EQ(field.getHeight(), 3);
    CHECK_EQ(field.height(3, 2), 0.f);
}

// Footprint contract: direction rotates the long axis; the center is pressed
// down, the perimeter rises, and distant cells remain unchanged.
TEST_CASE("snow.spec.footprint_direction_center_and_rim") {
    SnowField alongX(16, 16);
    alongX.fill(.8f);
    alongX.stampFootprint(8.f, 8.f, 1.f, 0.f, 3.f, .5f);
    REQUIRE(alongX.height(8, 8) < .4f);
    CHECK(alongX.height(10, 8) < .75f);
    CHECK_EQ(alongX.height(8, 10), .8f);
    CHECK(alongX.height(11, 8) > .85f);
    CHECK_EQ(alongX.height(2, 2), .8f);
    SnowField alongZ(16, 16);
    alongZ.fill(.8f);
    alongZ.stampFootprint(8.f, 8.f, 0.f, 1.f, 3.f, .5f);
    CHECK(alongZ.height(8, 10) < .75f);
    CHECK_EQ(alongZ.height(10, 8), .8f);
}

// Impact and recovery contract: a crater has a depressed bowl and raised rim;
// snowfall moves coverage monotonically toward one and ignores negative input.
TEST_CASE("snow.spec.impact_bowl_rim_and_snowfall") {
    SnowField field(20, 20);
    field.fill(.8f);
    field.stampImpact(10.f, 10.f, 4.f, .6f);
    REQUIRE(field.height(10, 10) < .21f);
    CHECK(field.height(13, 10) < .78f);
    CHECK(field.height(14, 10) > .85f);
    CHECK_EQ(field.height(16, 10), .8f);
    field.clearDirty();
    field.addSnowfall(.2f);
    CHECK(field.isDirty());
    CHECK(field.height(10, 10) > .2f);
    field.addSnowfall(2.f);
    CHECK_EQ(field.height(10, 10), 1.f);
    field.clearDirty();
    field.addSnowfall(-1.f);
    CHECK(!field.isDirty());
}

// Texture contract: height red encodes depth; albedo distinguishes packed
// snow from exposed soil; flat normals point +Z and a +X slope tilts -X.
TEST_CASE("snow.spec.height_albedo_and_normal_pixel_contract") {
    SnowField field(16, 16);
    field.fill(.5f);
    const auto height = field.toHeightRGBA();
    REQUIRE_EQ(height.size(), std::size_t{16 * 16 * 4});
    CHECK_GE(height[0], 126);
    CHECK_LE(height[0], 130);
    CHECK_EQ(height[1], std::uint8_t{0});
    CHECK_EQ(height[2], std::uint8_t{0});
    CHECK_EQ(height[3], std::uint8_t{255});
    const auto flat = field.toNormalRGBA();
    CHECK_GE(flat[0], 126);
    CHECK_LE(flat[0], 130);
    CHECK_GE(flat[1], 126);
    CHECK_LE(flat[1], 130);
    CHECK(flat[2] >= 252);
    field.fill(0.f);
    const auto soil = field.toAlbedoRGBA();
    field.fill(.4f);
    const auto packed = field.toAlbedoRGBA();
    field.fill(.85f);
    const auto fresh = field.toAlbedoRGBA();
    CHECK(soil[0] < 110);
    CHECK(packed[0] > 180);
    CHECK(packed[2] > packed[0]);
    CHECK(fresh[0] > packed[0]);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) field.setHeight(x, y, .4f + .05f * float(x));
    const auto        slope  = field.toNormalRGBA();
    const std::size_t center = (8 * 16 + 8) * 4;
    CHECK(slope[center] < 120);
    CHECK_GE(slope[center + 1], 126);
    CHECK_LE(slope[center + 1], 130);
}

// Heightmap contract: the snow layer adds depth times thickness to terrain;
// bare ground and cells outside the snow field keep terrain height.
TEST_CASE("snow.spec.heightmap_bridge") {
    eve::procgen::Heightmap terrain(8, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) terrain.setHeight(x, y, .5f);
    SnowField snow(8, 8);
    snow.fill(.5f);
    eve::procgen::Heightmap output;
    eve::weather::applySnowToHeightmap(snow, terrain, output, .2f);
    REQUIRE_EQ(output.getWidth(), 8);
    CHECK(std::fabs(output.height(3, 3) - .6f) < 1e-5f);
    snow.setHeight(2, 2, 0.f);
    eve::weather::applySnowToHeightmap(snow, terrain, output, .2f);
    CHECK(std::fabs(output.height(2, 2) - .5f) < 1e-5f);
    SnowField small(4, 4);
    small.fill(1.f);
    eve::weather::applySnowToHeightmap(small, terrain, output, .2f);
    CHECK(std::fabs(output.height(6, 6) - .5f) < 1e-5f);
}
}  // namespace test_snow
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_thunder_strike)
#include "PrivateTestScope.h"
#include "weather/ThunderStrike.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>

namespace test_thunder_strike_unit {
using namespace eve::weather;

// Trigger contract: seeded strike returns a bounded location and valid audio
// clip, then marks state active. An active strike cannot be retriggered.
TEST_CASE("weather.thunder.spec.trigger_location_audio_and_conflict") {
    ThunderStrikeState    state;
    ThunderStrikeSettings settings;
    settings.intensity      = 2.f;
    settings.radius         = 420.f;
    settings.volume         = .7f;
    settings.audioClipCount = 4;
    auto strike             = triggerThunderStrike(state, settings, 10.f, 20.f, 30.f, 1234u);
    REQUIRE(strike.ok());
    CHECK(state.playing);
    CHECK_EQ(state.intensity, 2.f);
    CHECK_GE(strike.value().y, 70.f);
    CHECK_LT(strike.value().y, 270.f);
    CHECK_GE(strike.value().audioClipIndex, 1);
    CHECK_LT(strike.value().audioClipIndex, 4);
    CHECK(!triggerThunderStrike(state, settings, 0.f, 0.f, 0.f, 1u).ok());
}

// Time contract: the flash decays by the documented clamped interpolation and
// stops below threshold. Negative dt and nonfinite settings reject atomically.
TEST_CASE("weather.thunder.spec.decay_seed_and_failure_atomicity") {
    ThunderStrikeSettings settings;
    ThunderStrikeState    firstState, secondState;
    auto                  first  = triggerThunderStrike(firstState, settings, 1.f, 2.f, 3.f, 99u);
    auto                  second = triggerThunderStrike(secondState, settings, 1.f, 2.f, 3.f, 99u);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().x, second.value().x);
    CHECK_EQ(first.value().y, second.value().y);
    CHECK_EQ(first.value().z, second.value().z);
    CHECK_EQ(first.value().audioClipIndex, -1);
    REQUIRE(advanceThunderStrike(firstState, .25f).ok());
    CHECK_EQ(firstState.intensity, .5f);
    REQUIRE(advanceThunderStrike(firstState, 1.f).ok());
    CHECK(!firstState.playing);
    CHECK_EQ(firstState.intensity, 0.f);
    CHECK(!advanceThunderStrike(secondState, -.1f).ok());
    CHECK(secondState.playing);
    ThunderStrikeState untouched;
    settings.radius = std::numeric_limits<float>::quiet_NaN();
    CHECK(!triggerThunderStrike(untouched, settings, 0.f, 0.f, 0.f, 7u).ok());
    CHECK(!untouched.playing);
}
}  // namespace test_thunder_strike_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_weather)
#include "PrivateTestScope.h"
#include "weather/Lightning.h"
#include "weather/PcgInteriorWeatherVolume.h"
#include "weather/Weather.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <string>

namespace test_weather_unit {
using namespace eve::weather;

// Preset contract: every declared preset round trips; unknown names leave
// the previously selected preset unchanged.
TEST_CASE("weather.spec.preset_selection") {
    Weather weather;
    for (const char* preset : {"clear", "drizzle", "rain", "storm", "snow", "fog", "wind", "blizzard"}) {
        weather.setPreset(preset);
        REQUIRE_EQ(weather.getPreset(), std::string(preset));
    }
    weather.setPreset("tsunami");
    CHECK_EQ(weather.getPreset(), "blizzard");
}

// Parameter contract: intensity, wind and mood setters retain accepted
// values and clamp their documented bounded ranges.
TEST_CASE("weather.spec.intensity_wind_and_mood_parameters") {
    Weather weather;
    weather.setIntensity(-1.f);
    REQUIRE_EQ(weather.getIntensity(), 0.f);
    weather.setIntensity(2.f);
    CHECK_EQ(weather.getIntensity(), 1.f);
    weather.setIntensity(.6f);
    CHECK(std::fabs(weather.getIntensity() - .6f) < 1e-6f);
    weather.setWindSpeed(-4.f);
    CHECK_EQ(weather.getWindSpeed(), 0.f);
    weather.setWindSpeed(12.f);
    weather.setWindDirection(90.f);
    CHECK_EQ(weather.getWindSpeed(), 12.f);
    CHECK_EQ(weather.getWindDirection(), 90.f);
    weather.setSkyColor(.2f, .3f, .4f);
    CHECK_EQ(weather.getSkyColorR(), .2f);
    CHECK_EQ(weather.getSkyColorG(), .3f);
    CHECK_EQ(weather.getSkyColorB(), .4f);
    weather.setFogDensity(-1.f);
    CHECK_EQ(weather.getFogDensity(), 0.f);
    weather.setSunIntensity(3.f);
    CHECK_EQ(weather.getSunIntensity(), 1.f);
    CHECK_GE(weather.getAmbientBrightness(), .34f);
    CHECK_LE(weather.getAmbientBrightness(), .91f);
}

// Snow-wind contract: a full 3-axis override is atomic and clear restores the
// derived wind; nonfinite input cannot replace the previous value.
TEST_CASE("weather.spec.snow_wind_override_atomicity") {
    Weather weather;
    REQUIRE(weather.setSnowWind(2.5f, -3.25f, 1.75f).ok());
    CHECK(weather.hasSnowWind());
    CHECK_EQ(weather.getSnowWindX(), 2.5f);
    CHECK_EQ(weather.getSnowWindY(), -3.25f);
    CHECK_EQ(weather.getSnowWindZ(), 1.75f);
    CHECK(!weather.setSnowWind(std::numeric_limits<float>::infinity(), 0.f, 0.f).ok());
    CHECK_EQ(weather.getSnowWindX(), 2.5f);
    weather.clearSnowWind();
    CHECK(!weather.hasSnowWind());
}

// Interior-volume contract: rejected shape edits preserve the prior volume;
// enter and exit transitions select the appropriate reverb and collision mode.
TEST_CASE("weather.spec.interior_shape_and_transition") {
    PcgInteriorWeatherVolume volume;
    REQUIRE(volume.configureBox(2.f, 3.f, 4.f, 10.f, 8.f, 6.f, 1, 7, 3).ok());
    CHECK(volume.contains(7.f, 7.f, 7.f));
    CHECK(!volume.configureSphere(0.f, 0.f, 0.f, -1.f, 0, 2, 4).ok());
    CHECK(volume.contains(7.f, 7.f, 7.f));
    REQUIRE(volume.configureSphere(0.f, 0.f, 0.f, 4.f, 0, 21, 8).ok());
    CHECK(volume.contains(3.f, 0.f, 0.f));
    CHECK(!volume.contains(5.f, 0.f, 0.f));
    Weather weather;
    auto    outside = weather.applyInteriorVolume(volume, 8.f, 0.f, 0.f);
    REQUIRE(outside.ok());
    CHECK_EQ(outside.value(), PcgInteriorWeatherTransition::Unchanged);
    CHECK_EQ(weather.getCurrentWeatherReverbPreset(), 8);
    auto entered = weather.applyInteriorVolume(volume, 0.f, 0.f, 0.f);
    REQUIRE(entered.ok());
    CHECK_EQ(entered.value(), PcgInteriorWeatherTransition::Entered);
    CHECK(weather.isInsideInteriorWeather());
    CHECK(weather.isInteriorWeatherCollisionRequested());
    CHECK_EQ(weather.getCurrentWeatherReverbPreset(), 21);
    auto exited = weather.applyInteriorVolume(volume, 5.f, 0.f, 0.f);
    REQUIRE(exited.ok());
    CHECK_EQ(exited.value(), PcgInteriorWeatherTransition::Exited);
    CHECK(!weather.isInsideInteriorWeather());
    CHECK_EQ(weather.getCurrentWeatherReverbPreset(), 8);
    CHECK(!weather.applyInteriorVolume(volume, std::numeric_limits<float>::infinity(), 0.f, 0.f).ok());
}

// Lightning contract: seed controls repeatability; generated indices and
// coordinates are finite, and invalid geometry settings fail validation.
TEST_CASE("weather.spec.lightning_geometry_and_envelope") {
    LightningSettings settings;
    auto              first    = buildLightningGeometry(settings, 0);
    auto              repeated = buildLightningGeometry(settings, 0);
    auto              changed  = buildLightningGeometry(settings, 1);
    REQUIRE(first.ok());
    REQUIRE(repeated.ok());
    REQUIRE(changed.ok());
    CHECK(first.value().positions == repeated.value().positions);
    CHECK(first.value().positions != changed.value().positions);
    for (auto index : first.value().indices) CHECK(index < first.value().positions.size() / 3);
    for (float coordinate : first.value().positions) CHECK(std::isfinite(coordinate));
    settings.branches = 0;
    auto trunk        = buildLightningGeometry(settings, 0);
    REQUIRE(trunk.ok());
    CHECK_EQ(trunk.value().indices.size(), static_cast<std::size_t>((1 << settings.subdivisions) * 12));
    settings.subdivisions = 9;
    CHECK(!buildLightningGeometry(settings, 0).ok());
    settings.subdivisions = 5;
    settings.width        = std::numeric_limits<float>::quiet_NaN();
    CHECK(!validateLightning(settings).ok());
    CHECK_EQ(lightningEnvelope(0.f), 1.f);
    CHECK_EQ(lightningEnvelope(1.f), 0.f);
    CHECK_EQ(lightningEnvelope(std::numeric_limits<float>::infinity()), 0.f);
}
}  // namespace test_weather_unit
#endif
