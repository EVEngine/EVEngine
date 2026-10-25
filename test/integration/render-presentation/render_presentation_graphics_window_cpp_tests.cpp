// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: window cpp
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include <cstdlib>
#include <vector>
#include "graphics/Graphics.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/unittest.h"
// window_cpp.cpp
namespace test_window_cpp {
namespace {

using WindowSettings = eve::window::WindowSettings;

void openWindow(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT, eve::window::Window*& win, eve::graphics::Graphics*& gfx,
                int w = 320, int h = 240) {
    win = eve::window::Window::create();
    gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
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
    s.width        = static_cast<uint16_t>(w);
    s.height       = static_cast<uint16_t>(h);
    s.centered     = true;
    s.fullscreen   = false;
    s.desktop_mode = true;
    s.resizable    = true;
    s.borderless   = false;
    s.vsync        = 1;
    s.minwidth     = 64;
    s.minheight    = 64;
    return s;
}

void checkCoreSettings(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT, const WindowSettings& expected,
                       const WindowSettings& actual) {
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
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(_ZEROERR_TEST_CONTEXT, win, gfx, 320, 240);

    WindowSettings s = makeSettings(320, 240);
    REQUIRE(win->setWindowSettings(s));
    checkCoreSettings(_ZEROERR_TEST_CONTEXT, s, win->getWindowSettings());
    CHECK(win->getHandle() != nullptr);

    win->close();
}

TEST_CASE("window.setSizeGetDimensions") {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(_ZEROERR_TEST_CONTEXT, win, gfx, 320, 240);

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
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(_ZEROERR_TEST_CONTEXT, win, gfx, 320, 240);

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
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(_ZEROERR_TEST_CONTEXT, win, gfx, 320, 240);

    WindowSettings s = win->getWindowSettings();
    s.desktop_mode   = true;
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
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(_ZEROERR_TEST_CONTEXT, win, gfx, 320, 240);
    REQUIRE(win->getHandle() != nullptr);

    win->close();
    CHECK(win->getHandle() == nullptr);
    CHECK(!win->setFullscreenDesktop(true));
    CHECK(!win->setFullscreenExclusive(false));
}

TEST_CASE("window.setWindowSettingsRecreatesAfterClose") {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(_ZEROERR_TEST_CONTEXT, win, gfx, 320, 240);

    win->close();
    CHECK(win->getHandle() == nullptr);

    WindowSettings s = makeSettings(320, 240);
    REQUIRE(win->setWindowSettings(s));
    CHECK(win->getHandle() != nullptr);
    checkCoreSettings(_ZEROERR_TEST_CONTEXT, s, win->getWindowSettings());
    CHECK_EQ(win->getWidth(), 320);
    CHECK_EQ(win->getHeight(), 240);

    win->close();
}

TEST_CASE("window.zeroSizeUsesDesktopDimensions") {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    win                          = eve::window::Window::create();
    gfx                          = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    WindowSettings s = makeSettings(0, 0);
    REQUIRE(win->setWindowSettings(s));
    CHECK_GT(win->getWidth(), 0);
    CHECK_GT(win->getHeight(), 0);
    CHECK_GT(win->getWindowSettings().width, 0u);
    CHECK_GT(win->getWindowSettings().height, 0u);

    win->close();
}

TEST_CASE("window.isOpenAfterCreateAndClose") {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(_ZEROERR_TEST_CONTEXT, win, gfx, 320, 240);
    CHECK(win->isOpen());
    win->close();
    CHECK(!win->isOpen());
}

TEST_CASE("window.titleAndPositionAndVSync") {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(_ZEROERR_TEST_CONTEXT, win, gfx, 320, 240);

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

    std::string ori              = win->getDisplayOrientation(0);
    const bool  validOrientation = ori == "unknown" || ori == "landscape" || ori == "landscapeFlipped" ||
                                  ori == "portrait" || ori == "portraitFlipped";
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
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(_ZEROERR_TEST_CONTEXT, win, gfx, 320, 240);

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
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(_ZEROERR_TEST_CONTEXT, win, gfx, 320, 240);
    win->requestAttention(false);
    win->requestAttention(true);
    win->close();
    win->requestAttention(false);
}

TEST_CASE("window.setIconRGBA") {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(_ZEROERR_TEST_CONTEXT, win, gfx, 320, 240);

    static std::vector<uint8_t> pixels(32 * 32 * 4, 0);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i]     = 255;  // R
        pixels[i + 3] = 255;  // A
    }
    CHECK(win->setIconRGBA(pixels.data(), 32, 32));

    // Recreating the window (e.g. via setWindowSettings) must re-apply the icon.
    WindowSettings s = makeSettings(320, 240);
    REQUIRE(win->setWindowSettings(s));
    CHECK(win->getHandle() != nullptr);

    win->close();
}

TEST_CASE("window.setIconRGBANullFails") {
    auto* win = eve::window::Window::create();
    REQUIRE(win != nullptr);
    CHECK(!win->setIconRGBA(nullptr, 32, 32));
}

TEST_CASE("window.showMessageBoxOptional") {
    const char* run = std::getenv("EVENGINE_TEST_MSGBOX");
    if (!run || std::string(run) != "1") {
        return;
    }
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(_ZEROERR_TEST_CONTEXT, win, gfx, 320, 240);
    CHECK(win->showMessageBox("t", "m", "info", true));
    eve::window::Window::MessageBoxData data;
    data.type              = "warning";
    data.title             = "t";
    data.message           = "m";
    data.buttons           = {"OK", "Cancel"};
    data.enterButtonIndex  = 0;
    data.escapeButtonIndex = 1;
    data.attachToWindow    = true;
    int idx                = win->showMessageBoxData(data);
    CHECK_GE(idx, 0);
    win->close();
}
}  // namespace test_window_cpp

// Contract section: window lifecycle
#include "PrivateTestScope.h"

#include <SDL2/SDL.h>
#include <chrono>
#include <memory>
#include <optional>
#include "graphics/Color.h"
#include "platform_event/PlatformEvent.h"

namespace test_window_lifecycle {
namespace {
struct CloseWindow {
    eve::window::Window* window;
    ~CloseWindow() {
        if (window) window->close();
    }
};

void initialize(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT, eve::window::Window*& window,
                eve::graphics::Graphics*& graphics) {
    REQUIRE(std::getenv("EVENGINE_TEST_NO_RENDER") == nullptr);
    window   = eve::window::Window::create();
    graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width = settings.height = 64;
    settings.high_dpi                = false;
    settings.vsync                   = 0;
    REQUIRE(window->setWindowSettings(settings));
    auto* events = eve::platform_event::PlatformEvent::create();
    REQUIRE(events != nullptr);
    events->pump();
    REQUIRE(graphics->isActive());
}

void pushWindowEvent(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT, eve::window::Window* window, Uint8 type) {
    SDL_Event event{};
    event.type            = SDL_WINDOWEVENT;
    event.window.windowID = SDL_GetWindowID(static_cast<SDL_Window*>(window->getHandle()));
    event.window.event    = type;
    REQUIRE_EQ(SDL_PushEvent(&event), 1);
}

void verifyResumedFrame(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT, eve::graphics::Graphics* graphics) {
    REQUIRE(graphics->isActive());
    graphics->setScreenReadbackEnabled(true);
    graphics->clear(eve::graphics::Color(0.1f, 0.2f, 0.3f, 1.f), std::nullopt, std::nullopt);
    graphics->present();
    std::unique_ptr<eve::image::ImageData> image(graphics->newImageData());
    REQUIRE(image != nullptr);
    REQUIRE_GT(graphics->getWidth(), 0);
    REQUIRE_GT(graphics->getHeight(), 0);
    REQUIRE_EQ(image->getSize(), size_t(graphics->getWidth() * graphics->getHeight() * 4));
    const auto* pixels = static_cast<const uint8_t*>(image->getData());
    REQUIRE(pixels != nullptr);
    const size_t center = size_t((graphics->getHeight() / 2 * graphics->getWidth() + graphics->getWidth() / 2) * 4);
    REQUIRE_GT(pixels[center + 2], pixels[center + 1]);
    REQUIRE_GT(pixels[center + 1], pixels[center]);
}
}  // namespace

TEST_CASE("window.lifecycle.desktopEventContract") {
    eve::window::Window*     window   = nullptr;
    eve::graphics::Graphics* graphics = nullptr;
    CloseWindow              cleanup{nullptr};
    initialize(_ZEROERR_TEST_CONTEXT, window, graphics);
    cleanup.window = window;
    SUB_CASE("window.minimizeRestoreRoundTrip") {
        REQUIRE(window->isVisible());
        pushWindowEvent(_ZEROERR_TEST_CONTEXT, window, SDL_WINDOWEVENT_MINIMIZED);
        REQUIRE(!graphics->isActive());
        graphics->present();
        pushWindowEvent(_ZEROERR_TEST_CONTEXT, window, SDL_WINDOWEVENT_RESTORED);
        verifyResumedFrame(_ZEROERR_TEST_CONTEXT, graphics);
        REQUIRE_GT(window->getPixelWidth(), 0);
        REQUIRE_GT(window->getPixelHeight(), 0);
    };
    SUB_CASE("graphics.lifecycle.minimizeRestoreStillPresents") {
        // The minimize/restore pair is checked above; this row owns the distinct hidden/shown pair.
        pushWindowEvent(_ZEROERR_TEST_CONTEXT, window, SDL_WINDOWEVENT_HIDDEN);
        REQUIRE(!graphics->isActive());
        graphics->present();
        pushWindowEvent(_ZEROERR_TEST_CONTEXT, window, SDL_WINDOWEVENT_SHOWN);
        verifyResumedFrame(_ZEROERR_TEST_CONTEXT, graphics);
    };
    SUB_CASE("window.focusObservationSoft") {
        (void)window->hasFocus();
        (void)window->hasMouseFocus();
        pushWindowEvent(_ZEROERR_TEST_CONTEXT, window, SDL_WINDOWEVENT_FOCUS_LOST);
        REQUIRE(graphics->isActive());
        pushWindowEvent(_ZEROERR_TEST_CONTEXT, window, SDL_WINDOWEVENT_FOCUS_GAINED);
        verifyResumedFrame(_ZEROERR_TEST_CONTEXT, graphics);
    };
}

TEST_CASE("graphics.lifecycle.appBackgroundForegroundResume") {
    eve::window::Window*     window   = nullptr;
    eve::graphics::Graphics* graphics = nullptr;
    CloseWindow              cleanup{nullptr};
    initialize(_ZEROERR_TEST_CONTEXT, window, graphics);
    cleanup.window = window;
    for (int cycle = 0; cycle < 2; ++cycle) {
        SDL_Event event{};
        event.type = SDL_APP_DIDENTERBACKGROUND;
        REQUIRE_EQ(SDL_PushEvent(&event), 1);
        REQUIRE(!graphics->isActive());
        graphics->present();
        event.type = SDL_APP_DIDENTERFOREGROUND;
        REQUIRE_EQ(SDL_PushEvent(&event), 1);
        verifyResumedFrame(_ZEROERR_TEST_CONTEXT, graphics);
    }
}

TEST_CASE("window.maximizeRestoreRoundTrip") {
    eve::window::Window*     window   = nullptr;
    eve::graphics::Graphics* graphics = nullptr;
    CloseWindow              cleanup{nullptr};
    initialize(_ZEROERR_TEST_CONTEXT, window, graphics);
    cleanup.window = window;
    REQUIRE(!window->isMaximized());
    window->maximize();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (!window->isMaximized() && std::chrono::steady_clock::now() < deadline) {
        SDL_PumpEvents();
        SDL_Delay(1);
    }
    // Preserve the existing WM-dependent native observation: some headless WMs ignore maximize.
    if (!window->isMaximized()) {
        window->restore();
        return;
    }
    window->restore();
    const auto restored = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (window->isMaximized() && std::chrono::steady_clock::now() < restored) {
        SDL_PumpEvents();
        SDL_Delay(1);
    }
    REQUIRE(!window->isMaximized());
    verifyResumedFrame(_ZEROERR_TEST_CONTEXT, graphics);
}
}  // namespace test_window_lifecycle
