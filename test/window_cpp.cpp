#include <cstdlib>

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "image/ImageData.h"
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

    // GitHub macOS runners (and some headless hosts) intermittently refuse
    // SDL_WINDOW_FULLSCREEN_DESKTOP; treat that as an environment skip.
    if (!win->setFullscreenDesktop(true)) {
        CHECK(!win->getWindowSettings().fullscreen);
        win->close();
        return;
    }
    CHECK(win->getWindowSettings().fullscreen);
    CHECK(win->getWindowSettings().desktop_mode);
    CHECK(win->getHandle() != nullptr);

    REQUIRE(win->setFullscreenDesktop(false));
    CHECK(!win->getWindowSettings().fullscreen);
    CHECK_EQ(win->getWidth(), 320);
    CHECK_EQ(win->getHeight(), 240);

    win->close();
}

TEST_CASE("window.setFullscreenDesktopRoundTripUsesSettings") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);

    WindowSettings s = win->getWindowSettings();
    s.desktop_mode = true;
    REQUIRE(win->setWindowSettings(s));

    if (!win->setFullscreenDesktop(true)) {
        CHECK(!win->getWindowSettings().fullscreen);
        win->close();
        return;
    }
    CHECK(win->getWindowSettings().fullscreen);
    CHECK(win->getWindowSettings().desktop_mode);

    REQUIRE(win->setFullscreenDesktop(false));
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
    CHECK(!win->setFullscreenDesktop(true));
    CHECK(!win->setFullscreenExclusive(false));
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

TEST_CASE("window.isOpenAfterCreateAndClose") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);
    CHECK(win->isOpen());
    win->close();
    CHECK(!win->isOpen());
}

TEST_CASE("window.titleAndPositionAndVSync") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);

    win->setWindowTitle("eve-window-test");
    CHECK_EQ(win->getWindowTitle(), std::string("eve-window-test"));

    win->setVSync(0);
    CHECK_EQ(win->getVSync(), 0);
    win->setVSync(1);
    CHECK_EQ(win->getVSync(), 1);

    int x = 0, y = 0, display = -1;
    win->getPosition(x, y, display);
    CHECK_GE(display, 0);

    win->setPosition(40, 50, display);
    int x2 = 0, y2 = 0, d2 = -1;
    win->getPosition(x2, y2, d2);
    CHECK_EQ(d2, display);
    CHECK_LE(std::abs(x2 - 40), 32);
    CHECK_LE(std::abs(y2 - 50), 32);

    CHECK(win->isVisible());
    win->close();
}

TEST_CASE("window.titleCachesBeforeOpen") {
    auto* win = eve::window::Window::create();
    REQUIRE(win != nullptr);
    win->setWindowTitle("cached-title");
    CHECK_EQ(win->getWindowTitle(), std::string("cached-title"));
    CHECK(!win->isOpen());
}

TEST_CASE("window.displayQueries") {
    auto* win = eve::window::Window::create();
    REQUIRE(win != nullptr);

    int n = win->getDisplayCount();
    CHECK_GT(n, 0);

    std::string name = win->getDisplayName(0);
    CHECK(!name.empty());

    std::string ori = win->getDisplayOrientation(0);
    const bool validOrientation = ori == "unknown" || ori == "landscape" || ori == "landscapeFlipped"
                                  || ori == "portrait" || ori == "portraitFlipped";
    CHECK(validOrientation);

    auto sizes = win->getFullscreenSizes(0);
    CHECK_GT(sizes.size(), 0u);

    int dw = 0, dh = 0;
    win->getDesktopDimensions(0, dw, dh);
    CHECK_GT(dw, 0);
    CHECK_GT(dh, 0);

    CHECK(win->getDisplayName(-1).empty());
    CHECK_EQ(win->getDisplayOrientation(999), std::string("unknown"));
    CHECK(win->getFullscreenSizes(999).empty());
    int zw = 1, zh = 1;
    win->getDesktopDimensions(999, zw, zh);
    CHECK_EQ(zw, 0);
    CHECK_EQ(zh, 0);
}

TEST_CASE("window.pixelSizeAndDpiRoundTrip") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);

    CHECK_GT(win->getPixelWidth(), 0);
    CHECK_GT(win->getPixelHeight(), 0);
    CHECK_GT(win->getNativeDPIScale(), 0.0);

    double px = 0, py = 0;
    win->toPixelsXY(10.0, 20.0, px, py);
    double wx = 0, wy = 0;
    win->fromPixelsXY(px, py, wx, wy);
    CHECK_EQ(wx, 10.0);
    CHECK_EQ(wy, 20.0);

    double x = 5.0, y = 6.0;
    win->windowToPixelCoords(&x, &y);
    win->pixelToWindowCoords(&x, &y);
    CHECK_EQ(x, 5.0);
    CHECK_EQ(y, 6.0);

    win->close();
    CHECK_EQ(win->getPixelWidth(), 0);
    CHECK_EQ(win->getPixelHeight(), 0);
    CHECK_EQ(win->getDPIScale(), 1.0);
}

TEST_CASE("window.requestAttentionNoCrash") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);
    win->requestAttention(false);
    win->requestAttention(true);
    win->close();
    win->requestAttention(false);
}

TEST_CASE("window.setAndGetIcon") {
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);

    // Window borrows the ImageData pointer, so it must outlive the window.
    static eve::image::ImageData icon(32, 32, "RGBA8");
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            icon.setPixel(x, y, eve::image::ImageData::Colorf{1.0f, 0.0f, 0.0f, 1.0f});

    CHECK(win->getIcon() == nullptr);
    CHECK(win->setIcon(&icon));
    CHECK(win->getIcon() == &icon);

    // Recreating the window (e.g. via setWindowSettings) must re-apply the icon.
    WindowSettings s = makeSettings(320, 240);
    REQUIRE(win->setWindowSettings(s));
    CHECK(win->getHandle() != nullptr);
    CHECK(win->getIcon() == &icon);

    win->close();
}

TEST_CASE("window.setIconNullFails") {
    auto* win = eve::window::Window::create();
    REQUIRE(win != nullptr);
    CHECK(!win->setIcon(nullptr));
}

TEST_CASE("window.showMessageBoxOptional") {
    const char* run = std::getenv("EVENGINE_TEST_MSGBOX");
    if (!run || std::string(run) != "1") {
        return;
    }
    eve::window::Window* win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);
    CHECK(win->showMessageBox("t", "m", "info", true));
    eve::window::Window::MessageBoxData data;
    data.type = "warning";
    data.title = "t";
    data.message = "m";
    data.buttons = {"OK", "Cancel"};
    data.enterButtonIndex = 0;
    data.escapeButtonIndex = 1;
    data.attachToWindow = true;
    int idx = win->showMessageBoxData(data);
    CHECK_GE(idx, 0);
    win->close();
}
