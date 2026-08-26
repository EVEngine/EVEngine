#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "weather/Weather.h"

#include "graphics/Graphics.h"
#include "window/Window.h"

#include <cmath>

using namespace eve::weather;

TEST_CASE("weather.backendShaderInitialization") {
    auto *window = eve::window::Window::create();
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 128;
    settings.height = 128;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));

    Weather weather;
    weather.init(graphics);
    weather.setPreset("storm");
    weather.strike();
    weather.update(1.f / 60.f, graphics);
    CHECK(weather.getIntensity() >= 0.f);
    CHECK(weather.getFlash() >= 0.f);
    window->close();
}

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
    float amb = w.getAmbientBrightness();
    CHECK(amb >= 0.34f);
    CHECK(amb <= 0.91f);
}
