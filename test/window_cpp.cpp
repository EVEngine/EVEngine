#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "window/Window.h"

namespace {

using WindowSettings = eve::window::WindowSettings;

void openWindow(eve::window::Window*& win, eve::graphics::Graphics*& gfx, int w = 320, int h = 240) {
    win = eve::window::Window::create();
    gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    WindowSettings s;
    s.width      = static_cast<uint16_t>(w);
    s.height     = static_cast<uint16_t>(h);
    s.centered   = true;
    s.fullscreen = false;
    s.resizable  = true;
    REQUIRE(win->setWindowSettings(s));
}

WindowSettings makeSettings(int w, int h) {
    WindowSettings s;
    s.width          = static_cast<uint16_t>(w);
    s.height         = static_cast<uint16_t>(h);
    s.centered       = true;
    s.fullscreen     = false;
    s.desktop_mode   = true;
    s.resizable      = true;
    s.borderless     = false;
    s.vsync          = 1;
    s.minwidth       = 64;
    s.minheight      = 64;
    return s;
}

void checkCoreSettings(const WindowSettings& expected, const WindowSettings& actual) {
    CHECK_EQ(actual.width, expected.width);
    CHECK_EQ(actual.height, expected.height);
    CHECK_EQ(actual.centered, expected.centered);
    CHECK_EQ(actual.fullscreen, expected.fullscreen);
    CHECK_EQ(actual.desktop_mode, expected.desktop_mode);
    CHECK_EQ(actual.resizable, expected.resizable);
    CHECK_EQ(actual.borderless, expected.borderless);
    CHECK_EQ(actual.vsync, expected.vsync);
    CHECK_EQ(actual.minwidth, expected.minwidth);
    CHECK_EQ(actual.minheight, expected.minheight);
}

}  // namespace

TEST_CASE("window.setWindowSettingsRoundTrip") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);

    WindowSettings s = makeSettings(320, 240);
    REQUIRE(win->setWindowSettings(s));
    checkCoreSettings(s, win->getWindowSettings());
    CHECK(win->getHandle() != nullptr);

    win->close();
}

TEST_CASE("window.setSizeGetDimensions") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);

    win->setSize(400, 300);
    CHECK_EQ(win->getWidth(), 400);
    CHECK_EQ(win->getHeight(), 300);
    CHECK_EQ(win->getWindowSettings().width, 400u);
    CHECK_EQ(win->getWindowSettings().height, 300u);

    win->setSize(128, 96);
    CHECK_EQ(win->getWidth(), 128);
    CHECK_EQ(win->getHeight(), 96);

    win->close();
}

TEST_CASE("window.setFullscreenDesktopRoundTrip") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);

    CHECK(!win->getWindowSettings().fullscreen);

    REQUIRE(win->setFullscreen(true, true));
    CHECK(win->getWindowSettings().fullscreen);
    CHECK(win->getWindowSettings().desktop_mode);
    CHECK(win->getHandle() != nullptr);

    REQUIRE(win->setFullscreen(false, true));
    CHECK(!win->getWindowSettings().fullscreen);
    CHECK_EQ(win->getWidth(), 320);
    CHECK_EQ(win->getHeight(), 240);

    win->close();
}

TEST_CASE("window.setFullscreenSingleArgUsesDesktopMode") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);

    WindowSettings s = win->getWindowSettings();
    s.desktop_mode = true;
    REQUIRE(win->setWindowSettings(s));

    REQUIRE(win->setFullscreen(true));
    CHECK(win->getWindowSettings().fullscreen);
    CHECK(win->getWindowSettings().desktop_mode);

    REQUIRE(win->setFullscreen(false));
    CHECK(!win->getWindowSettings().fullscreen);

    win->close();
}

TEST_CASE("window.closeClearsHandleAndBlocksFullscreen") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);
    REQUIRE(win->getHandle() != nullptr);

    win->close();
    CHECK(win->getHandle() == nullptr);
    CHECK(!win->setFullscreen(true, true));
    CHECK(!win->setFullscreen(false));
}

TEST_CASE("window.setWindowSettingsRecreatesAfterClose") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);

    win->close();
    CHECK(win->getHandle() == nullptr);

    WindowSettings s = makeSettings(320, 240);
    REQUIRE(win->setWindowSettings(s));
    CHECK(win->getHandle() != nullptr);
    checkCoreSettings(s, win->getWindowSettings());
    CHECK_EQ(win->getWidth(), 320);
    CHECK_EQ(win->getHeight(), 240);

    win->close();
}

TEST_CASE("window.zeroSizeUsesDesktopDimensions") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    win = eve::window::Window::create();
    gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);

    WindowSettings s = makeSettings(0, 0);
    REQUIRE(win->setWindowSettings(s));
    CHECK_GT(win->getWidth(), 0);
    CHECK_GT(win->getHeight(), 0);
    CHECK_GT(win->getWindowSettings().width, 0u);
    CHECK_GT(win->getWindowSettings().height, 0u);

    win->close();
}
