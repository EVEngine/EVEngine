#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "demo/Demo.h"
#include "graphics/Graphics.h"
#include "sound/SoundData.h"
#include "window/Window.h"
#include "common/Exception.h"

TEST_CASE("demo.newSound.music") {
    auto *demo = eve::demo::Demo::create();
    auto *sd = demo->newSound("music");
    REQUIRE(sd != nullptr);
    CHECK(sd->getSampleRate() == 22050);
    CHECK(sd->getBitDepth() == 16);
    CHECK(sd->getChannelCount() == 1);
    CHECK(sd->getSampleCount() == 22050 * 4);
    CHECK(sd->getDuration() > 3.9);
    delete sd;
}

TEST_CASE("demo.newSound.sfx") {
    auto *demo = eve::demo::Demo::create();
    for (const char *kind : {"shoot", "explode", "hit"}) {
        auto *sd = demo->newSound(kind);
        REQUIRE(sd != nullptr);
        CHECK(sd->getSampleCount() > 0);
        CHECK(sd->getSize() == size_t(sd->getSampleCount()) * 2u);
        delete sd;
    }
}

TEST_CASE("demo.newSound.unknownThrows") {
    auto *demo = eve::demo::Demo::create();
    try {
        demo->newSound("nope");
        CHECK(false);
    } catch (const eve::Exception &) {
        CHECK(true);
    }
}

TEST_CASE("demo.newPlanetTexture") {
    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 64;
    s.height = 64;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto *demo = eve::demo::Demo::create();
    auto *tex = demo->newPlanetTexture(gfx);
    REQUIRE(tex != nullptr);
    CHECK_EQ(tex->getWidth(), 512);
    CHECK_EQ(tex->getHeight(), 256);

    win->close();
}
