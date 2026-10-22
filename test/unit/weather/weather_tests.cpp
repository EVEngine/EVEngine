#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_snow)
#include "PrivateTestScope.h"
#include <cmath>
#include "procgen/heightmap/Heightmap.h"
#include "weather/Snow.h"
#include "weather/SnowField.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// snow.cpp
namespace test_snow {
// snow.cpp
using eve::weather::SnowField;

TEST_CASE("SnowField.basicGrid") {
    SnowField f(8, 6);
    CHECK_EQ(f.getWidth(), 8);
    CHECK_EQ(f.getHeight(), 6);
    CHECK_EQ(f.height(3, 2), 0.f);
    CHECK_EQ(f.height(-1, 0), 0.f);  // out-of-bounds reads 0

    f.setHeight(3, 2, 0.8f);
    CHECK_EQ(f.height(3, 2), 0.8f);
    f.fill(0.5f);
    CHECK_EQ(f.height(0, 0), 0.5f);

    f.setHeight(0, 0, 2.f);  // clamped to [0,1]
    CHECK_EQ(f.height(0, 0), 1.f);
    f.setHeight(0, 0, -0.5f);
    CHECK_EQ(f.height(0, 0), 0.f);
    f.setHeight(99, 99, 0.7f);  // out-of-bounds write is a no-op

    f.resize(4, 4);
    CHECK_EQ(f.getWidth(), 4);
    CHECK_EQ(f.getHeight(), 4);
    CHECK(f.isDirty());
}

TEST_CASE("SnowField.footprintDepressesAndRaisesRim") {
    SnowField f(16, 16);
    f.fill(0.8f);
    f.clearDirty();

    f.stampFootprint(8.f, 8.f, 1.f, 0.f, 3.f, 0.5f);
    CHECK(f.isDirty());

    CHECK(f.height(8, 8) < 0.4f);    // center pressed down
    CHECK(f.height(10, 8) < 0.75f);  // inner footprint lowered
    CHECK(f.height(8, 10) == 0.8f);  // narrower across: untouched
    CHECK(f.height(11, 8) > 0.85f);  // pushed-up rim at the edge
    CHECK(f.height(2, 2) == 0.8f);   // far away untouched

    f.clearDirty();
    CHECK(!f.isDirty());
}

TEST_CASE("SnowField.footprintFollowsTravelDirection") {
    SnowField f(16, 16);
    f.fill(0.8f);
    f.stampFootprint(8.f, 8.f, 0.f, 1.f, 3.f, 0.5f);

    CHECK(f.height(8, 10) < 0.75f);  // elongated along +Z
    CHECK(f.height(10, 8) == 0.8f);  // across direction untouched
}

TEST_CASE("SnowField.impactCraterBowlAndRim") {
    SnowField f(20, 20);
    f.fill(0.8f);
    f.stampImpact(10.f, 10.f, 4.f, 0.6f);

    CHECK(f.height(10, 10) < 0.21f);  // deep bowl center
    CHECK(f.height(13, 10) < 0.78f);  // shallow bowl near the edge
    CHECK(f.height(14, 10) > 0.85f);  // raised ejecta ring outside the bowl
    CHECK(f.height(16, 10) == 0.8f);  // outside the rim band

    // Ejecta is intentionally lumpy: ring heights vary per cell instead of
    // forming a perfect smooth ring.
    float lo = 1.f, hi = 0.f;
    for (int z = 5; z <= 15; ++z) {
        for (int x = 5; x <= 15; ++x) {
            const float dx = float(x) - 10.f;
            const float dz = float(z) - 10.f;
            const float n  = std::sqrt(dx * dx + dz * dz) / 4.f;
            if (n < 0.95f || n > 1.4f) continue;
            lo = std::min(lo, f.height(x, z));
            hi = std::max(hi, f.height(x, z));
        }
    }
    CHECK(hi - lo > 0.05f);
}

TEST_CASE("SnowField.snowfallRecovery") {
    SnowField f(4, 4);
    f.fill(0.5f);
    f.clearDirty();

    f.addSnowfall(0.2f);
    CHECK(f.isDirty());
    CHECK(std::fabs(f.height(0, 0) - 0.7f) < 1e-5f);

    f.addSnowfall(0.5f);
    CHECK_EQ(f.height(0, 0), 1.f);  // clamped at full cover
    f.addSnowfall(-1.f);            // negative amounts are ignored
    CHECK_EQ(f.height(0, 0), 1.f);
}

TEST_CASE("SnowField.heightTexture") {
    SnowField f(3, 2);
    f.fill(1.f);
    std::vector<uint8_t> rgba = f.toHeightRGBA();
    CHECK_EQ(int(rgba.size()), 3 * 2 * 4);
    CHECK_EQ(int(rgba[0]), 255);   // R = height
    CHECK_EQ(int(rgba[12]), 255);  // R of the last cell
    CHECK_EQ(int(rgba[1]), 0);     // G/B stay 0 so albedo stays clean
    CHECK_EQ(int(rgba[2]), 0);

    f.setHeight(0, 0, 0.5f);
    rgba = f.toHeightRGBA();
    CHECK(int(rgba[0]) >= 126);
    CHECK(int(rgba[0]) <= 130);
    CHECK_EQ(int(rgba[3]), 255);  // alpha always opaque
}

TEST_CASE("SnowField.albedoBlendsSnowToGround") {
    SnowField f(4, 4);
    f.fill(0.f);
    std::vector<uint8_t> rgba = f.toAlbedoRGBA();
    CHECK(int(rgba[0]) >= 85);  // dark soil
    CHECK(int(rgba[0]) <= 100);
    CHECK(int(rgba[1]) >= 70);
    CHECK(int(rgba[1]) <= 85);
    CHECK(int(rgba[2]) >= 55);
    CHECK(int(rgba[2]) <= 68);

    f.fill(1.f);
    rgba = f.toAlbedoRGBA();
    CHECK(int(rgba[0]) >= 230);  // cool white snow
    CHECK(int(rgba[0]) <= 245);
    CHECK(int(rgba[3]) == 255);

    // Albedo is a color ramp, not a copy of the height channel: at s = 0.25 the
    // red channel (packed snow) must differ from the height texture's
    // red (64).
    f.fill(0.25f);
    const int albedoR = int(f.toAlbedoRGBA()[0]);
    const int heightR = int(f.toHeightRGBA()[0]);
    CHECK(std::abs(albedoR - heightR) > 20);
}

TEST_CASE("SnowField.normalMapFromGradient") {
    SnowField f(16, 16);
    f.fill(0.5f);
    std::vector<uint8_t> flat = f.toNormalRGBA();
    CHECK(int(flat[0]) >= 126);  // flat = 128,128,255
    CHECK(int(flat[0]) <= 130);
    CHECK(int(flat[1]) >= 126);
    CHECK(int(flat[1]) <= 130);
    CHECK(int(flat[2]) >= 252);

    // Linearly rising snow toward +X tilts the normal away from +U.
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            f.setHeight(x, z, 0.4f + 0.05f * float(x));
        }
    }
    std::vector<uint8_t> slope = f.toNormalRGBA();
    const size_t         i     = (8 * 16 + 8) * 4;
    CHECK(int(slope[i + 0]) < 120);  // nx < 0 -> R below mid gray
    CHECK(int(slope[i + 1]) >= 126);
    CHECK(int(slope[i + 1]) <= 130);
    CHECK(int(slope[i + 2]) >= 245);
}

TEST_CASE("SnowField.applyToHeightmapAddsSnow") {
    eve::procgen::Heightmap terrain(8, 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            terrain.setHeight(x, y, 0.5f);
        }
    }

    SnowField f(8, 8);
    f.fill(0.5f);
    eve::procgen::Heightmap out;
    eve::weather::applySnowToHeightmap(f, terrain, out, 0.2f);
    CHECK_EQ(out.getWidth(), 8);
    CHECK(std::fabs(out.height(3, 3) - 0.6f) < 1e-5f);  // 0.5 + 0.5 * 0.2

    f.setHeight(2, 2, 0.f);
    eve::weather::applySnowToHeightmap(f, terrain, out, 0.2f);
    CHECK(std::fabs(out.height(2, 2) - 0.5f) < 1e-5f);  // bare ground

    SnowField small(4, 4);
    small.fill(1.f);
    eve::weather::applySnowToHeightmap(small, terrain, out, 0.2f);
    CHECK(std::fabs(out.height(6, 6) - 0.5f) < 1e-5f);  // outside field: terrain only
}

// snow_appearance.cpp
TEST_CASE("SnowField.compressedSnowRemainsSnow") {
    eve::weather::SnowField field(8, 8);
    field.fill(0.4f);
    const auto compressed = field.toAlbedoRGBA();
    REQUIRE(compressed[0] > 180);
    REQUIRE(compressed[2] > compressed[0]);
    field.fill(0.f);
    const auto exposed = field.toAlbedoRGBA();
    REQUIRE(exposed[0] < 110);
    REQUIRE(exposed[2] < exposed[0]);
    field.fill(0.85f);
    const auto fresh = field.toAlbedoRGBA();
    REQUIRE(fresh[0] > compressed[0]);
}
}  // namespace test_snow
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_thunder_strike)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "weather/ThunderStrike.h"
#include "weather/Weather.h"
// thunder_strike.cpp
namespace test_thunder_strike_unit {
using namespace eve::weather;

TEST_CASE("weather.thunderStrike.matchesPcgLocationAudioAndDecay") {
    ThunderStrikeState    state;
    ThunderStrikeSettings settings;
    settings.intensity      = 2.f;
    settings.radius         = 420.f;
    settings.volume         = 0.7f;
    settings.audioClipCount = 4;
    auto strike             = triggerThunderStrike(state, settings, 10.f, 20.f, 30.f, 1234u);
    REQUIRE(strike.ok());
    CHECK(state.playing);
    CHECK(state.intensity == 2.f);
    CHECK(strike.value().y >= 70.f);
    CHECK(strike.value().y < 270.f);
    CHECK(strike.value().audioClipIndex >= 1);
    CHECK(strike.value().audioClipIndex < 4);
    CHECK(!triggerThunderStrike(state, settings, 0.f, 0.f, 0.f, 1u).ok());
    REQUIRE(advanceThunderStrike(state, 0.25f).ok());
    CHECK(state.intensity == 1.f);
    REQUIRE(advanceThunderStrike(state, 1.f).ok());
    CHECK(!state.playing);
    CHECK(state.intensity == 0.f);
}

TEST_CASE("weather.thunderStrike.isDeterministicAndFailureAtomic") {
    ThunderStrikeSettings settings;
    ThunderStrikeState    a, b;
    auto                  first  = triggerThunderStrike(a, settings, 1.f, 2.f, 3.f, 99u);
    auto                  second = triggerThunderStrike(b, settings, 1.f, 2.f, 3.f, 99u);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(first.value().x == second.value().x);
    CHECK(first.value().y == second.value().y);
    CHECK(first.value().z == second.value().z);
    CHECK(first.value().audioClipIndex == -1);
    ThunderStrikeState untouched;
    settings.radius = std::numeric_limits<float>::quiet_NaN();
    CHECK(!triggerThunderStrike(untouched, settings, 0.f, 0.f, 0.f, 7u).ok());
    CHECK(!untouched.playing);
    CHECK(!advanceThunderStrike(untouched, -0.1f).ok());
}
}  // namespace test_thunder_strike_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_weather)
#include <cmath>
#include <limits>
#include "PrivateTestScope.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "weather/Lightning.h"
#include "weather/PcgInteriorWeatherVolume.h"
#include "weather/Weather.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// weather.cpp
namespace test_weather_unit {
using namespace eve::weather;


TEST_CASE("weather.presetRoundTrip") {
    Weather w;
    w.setPreset("clear");
    CHECK(w.getPreset() == "clear");
    w.setPreset("drizzle");
    CHECK(w.getPreset() == "drizzle");
    w.setPreset("rain");
    CHECK(w.getPreset() == "rain");
    w.setPreset("storm");
    CHECK(w.getPreset() == "storm");
    w.setPreset("snow");
    CHECK(w.getPreset() == "snow");
    w.setPreset("fog");
    CHECK(w.getPreset() == "fog");
    w.setPreset("wind");
    CHECK(w.getPreset() == "wind");
    w.setPreset("blizzard");
    CHECK(w.getPreset() == "blizzard");
}

TEST_CASE("weather.unknownPresetIgnored") {
    Weather w;
    w.setPreset("rain");
    w.setPreset("tsunami");  // not a known preset; keep previous
    CHECK(w.getPreset() == "rain");
}

TEST_CASE("weather.intensityClamped") {
    Weather w;
    w.setIntensity(-1.0f);
    CHECK(w.getIntensity() == 0.0f);
    w.setIntensity(2.0f);
    CHECK(w.getIntensity() == 1.0f);
    w.setIntensity(0.6f);
    CHECK(std::fabs(w.getIntensity() - 0.6f) < 1e-6f);
}

TEST_CASE("weather.windClamped") {
    Weather w;
    w.setWindSpeed(12.0f);
    CHECK(w.getWindSpeed() == 12.0f);
    w.setWindSpeed(-4.0f);  // non-negative
    CHECK(w.getWindSpeed() == 0.0f);
    w.setWindDirection(90.0f);
    CHECK(w.getWindDirection() == 90.0f);
}

TEST_CASE("weather.lightningToggle") {
    Weather w;
    CHECK(w.isLightningEnabled());
    w.setLightningEnabled(false);
    CHECK(!w.isLightningEnabled());
    w.setLightningEnabled(true);
    CHECK(w.isLightningEnabled());
}

TEST_CASE("weather.moodParams") {
    Weather w;
    w.setSkyColor(0.2f, 0.3f, 0.4f);
    CHECK(w.getSkyColorR() == 0.2f);
    CHECK(w.getSkyColorG() == 0.3f);
    CHECK(w.getSkyColorB() == 0.4f);

    w.setFogColor(0.5f, 0.55f, 0.6f);
    CHECK(w.getFogColorR() == 0.5f);
    CHECK(w.getFogColorG() == 0.55f);
    CHECK(w.getFogColorB() == 0.6f);

    w.setFogDensity(0.03f);
    CHECK(w.getFogDensity() == 0.03f);
    w.setFogDensity(-1.0f);
    CHECK(w.getFogDensity() == 0.0f);

    w.setSunIntensity(0.4f);
    CHECK(w.getSunIntensity() == 0.4f);
    w.setSunIntensity(3.0f);  // clamped to [0,1]
    CHECK(w.getSunIntensity() == 1.0f);
}

TEST_CASE("weather.ambientInRange") {
    // intensityCur starts at 0 (clear); ambient is bright and bounded.
    Weather w;
    float   amb = w.getAmbientBrightness();
    CHECK(amb >= 0.34f);
    CHECK(amb <= 0.91f);
}

TEST_CASE("weather.pcgSnowWindThreeAxisAndAtomicValidation") {
    Weather w;
    REQUIRE(w.setSnowWind(2.5f, -3.25f, 1.75f).ok());
    CHECK(w.hasSnowWind());
    CHECK(w.getSnowWindX() == 2.5f);
    CHECK(w.getSnowWindY() == -3.25f);
    CHECK(w.getSnowWindZ() == 1.75f);
    CHECK(!w.setSnowWind(std::numeric_limits<float>::infinity(), 0.f, 0.f).ok());
    CHECK(w.getSnowWindX() == 2.5f);
    w.clearSnowWind();
    CHECK(!w.hasSnowWind());
}

TEST_CASE("weather.pcgInteriorVolumeShapesValidateAtomically") {
    PcgInteriorWeatherVolume volume;
    REQUIRE(volume.configureBox(2.f, 3.f, 4.f, 10.f, 8.f, 6.f, 1, 7, 3).ok());
    CHECK(volume.contains(7.f, 7.f, 7.f));
    CHECK(!volume.contains(7.01f, 7.f, 7.f));
    CHECK(!volume.configureSphere(0.f, 0.f, 0.f, -1.f, 0, 2, 4).ok());
    CHECK(volume.contains(7.f, 7.f, 7.f));
    REQUIRE(volume.configureSphere(0.f, 0.f, 0.f, 5.f, 0, 11, 13).ok());
    CHECK(volume.contains(3.f, 4.f, 0.f));
    CHECK(!volume.contains(3.01f, 4.f, 0.f));
    CHECK(!volume.contains(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f));
}

TEST_CASE("weather.pcgInteriorVolumeTransitionsAndReverb") {
    PcgInteriorWeatherVolume volume;
    REQUIRE(volume.configureSphere(0.f, 0.f, 0.f, 4.f, 0, 21, 8).ok());
    Weather weather;
    auto    outside = weather.applyInteriorVolume(volume, 8.f, 0.f, 0.f);
    REQUIRE(outside.ok());
    CHECK(outside.value() == PcgInteriorWeatherTransition::Unchanged);
    CHECK(weather.getCurrentWeatherReverbPreset() == 8);
    auto entered = weather.applyInteriorVolume(volume, 0.f, 0.f, 0.f);
    REQUIRE(entered.ok());
    CHECK(entered.value() == PcgInteriorWeatherTransition::Entered);
    CHECK(weather.isInsideInteriorWeather());
    CHECK(weather.isInteriorWeatherCollisionRequested());
    CHECK(weather.getCurrentWeatherReverbPreset() == 21);
    auto unchanged = weather.setInteriorVolumeState(volume, true);
    REQUIRE(unchanged.ok());
    CHECK(unchanged.value() == PcgInteriorWeatherTransition::Unchanged);
    auto exited = weather.applyInteriorVolume(volume, 5.f, 0.f, 0.f);
    REQUIRE(exited.ok());
    CHECK(exited.value() == PcgInteriorWeatherTransition::Exited);
    CHECK(!weather.isInsideInteriorWeather());
    CHECK(weather.getCurrentWeatherReverbPreset() == 8);
    CHECK(!weather.applyInteriorVolume(volume, std::numeric_limits<float>::infinity(), 0.f, 0.f).ok());
}
TEST_CASE("weather.lightning deterministic bounded geometry") {
    LightningSettings s;
    auto a = buildLightningGeometry(s, 0), b = buildLightningGeometry(s, 0), c = buildLightningGeometry(s, 1);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(c.ok());
    CHECK(a.value().positions == b.value().positions);
    CHECK(a.value().positions != c.value().positions);
    CHECK(a.value().positions.size() / 3 == a.value().normals.size() / 3);
    for (auto index : a.value().indices) CHECK(index < a.value().positions.size() / 3);
    for (float x : a.value().positions) CHECK(std::isfinite(x));
    s.branches = 0;
    auto trunk = buildLightningGeometry(s, 0);
    REQUIRE(trunk.ok());
    CHECK(trunk.value().indices.size() == static_cast<size_t>((1 << s.subdivisions) * 12));
    CHECK(trunk.value().positions[1] == 0.f);
    CHECK(trunk.value().positions[trunk.value().positions.size() - 2] == 1.f);
    s.subdivisions = 9;
    CHECK(!buildLightningGeometry(s, 0).ok());
    s.subdivisions = 5;
    s.width        = std::numeric_limits<float>::quiet_NaN();
    CHECK(!validateLightning(s).ok());
    CHECK(lightningEnvelope(0) == 1.f);
    CHECK(lightningEnvelope(.15f) < lightningEnvelope(.20f));
    CHECK(lightningEnvelope(1) == 0.f);
    CHECK(lightningEnvelope(std::numeric_limits<float>::infinity()) == 0.f);
}

}  // namespace test_weather_unit
#endif
