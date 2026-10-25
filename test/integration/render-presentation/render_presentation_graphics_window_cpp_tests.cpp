// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "module": "window",
  "modules": [
    "window"
  ],
  "origins": [
    "window_cpp.cpp",
    "window_lifecycle.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_window_cpp)
#include "PrivateTestScope.h"
#include <cstdlib>
#include <vector>
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// window_cpp.cpp
namespace test_window_cpp {
namespace {

using WindowSettings = eve::window::WindowSettings;

void openWindow(eve::window::Window*& win, eve::graphics::Graphics*& gfx, int w = 320, int h = 240) {
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
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);

    WindowSettings s = makeSettings(320, 240);
    REQUIRE(win->setWindowSettings(s));
    checkCoreSettings(s, win->getWindowSettings());
    CHECK(win->getHandle() != nullptr);

    win->close();
}

TEST_CASE("window.setSizeGetDimensions") {
    eve::window::Window*     win = nullptr;
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
    eve::window::Window*     win = nullptr;
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
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);

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
    openWindow(win, gfx, 320, 240);
    REQUIRE(win->getHandle() != nullptr);

    win->close();
    CHECK(win->getHandle() == nullptr);
    CHECK(!win->setFullscreenDesktop(true));
    CHECK(!win->setFullscreenExclusive(false));
}

TEST_CASE("window.setWindowSettingsRecreatesAfterClose") {
    eve::window::Window*     win = nullptr;
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
    openWindow(win, gfx, 320, 240);
    CHECK(win->isOpen());
    win->close();
    CHECK(!win->isOpen());
}

TEST_CASE("window.titleAndPositionAndVSync") {
    eve::window::Window*     win = nullptr;
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
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);
    win->requestAttention(false);
    win->requestAttention(true);
    win->close();
    win->requestAttention(false);
}

TEST_CASE("window.setIconRGBA") {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx, 320, 240);

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
    openWindow(win, gfx, 320, 240);
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_window_lifecycle)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <optional>
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "platform_event/PlatformEvent.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// window_lifecycle.cpp
namespace test_window_lifecycle {
// Window minimize / restore and app background / foreground resume.
//
// Desktop: minimize → restore must pause then resume presenting without crash.
// Mobile (and portable simulation): SDL_APP_* events must deactivate graphics,
// request surface recreate on resume, and allow present again.
//
// Focus-only alt-tab is intentionally not paused: the window may still be
// visible. Coverage for hasFocus is a soft observation (WM-dependent).


namespace {

using WindowSettings = eve::window::WindowSettings;

void openWindow(eve::window::Window*& win, eve::graphics::Graphics*& gfx, int w = 320, int h = 240) {
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

/** Install the window event watch (deferred to the first pump). */
void installLifecycleWatch() {
    auto* events = eve::platform_event::PlatformEvent::create();
    REQUIRE(events != nullptr);
    events->pump();
}

void presentClearFrames(eve::graphics::Graphics* gfx, int frames = 3) {
    for (int i = 0; i < frames; ++i) {
        gfx->clear(eve::graphics::Color(0.1f, 0.2f, 0.3f, 1.f), std::nullopt, std::nullopt);
        gfx->present();
    }
}

/** Fire a WINDOWEVENT through the same EventWatch the engine installs. */
void pushWindowEvent(Uint8 windowEvent) {
    SDL_Event e{};
    e.type         = SDL_WINDOWEVENT;
    e.window.event = windowEvent;
    REQUIRE(SDL_PushEvent(&e) == 1);
}

}  // namespace

TEST_CASE("window.minimizeRestoreRoundTrip") {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx);
    installLifecycleWatch();

    CHECK(win->isVisible());
    CHECK(!win->isMinimized());
    CHECK(gfx->isActive());
    presentClearFrames(gfx);

    // Portable path: synthesize the desktop minimize/restore events the
    // EventSink watches. Real SDL_MinimizeWindow is WM-dependent (Xvfb often
    // ignores iconify), so the synthetic events are the CI-stable contract.
    pushWindowEvent(SDL_WINDOWEVENT_MINIMIZED);
    CHECK(!gfx->isActive());
    presentClearFrames(gfx, 2);

    pushWindowEvent(SDL_WINDOWEVENT_RESTORED);
    CHECK(gfx->isActive());
    presentClearFrames(gfx);

    // Best-effort real iconify when the host supports it.
    win->minimize();
    SDL_Delay(50);
    SDL_PumpEvents();
    if (win->isMinimized()) {
        CHECK(!win->isVisible());
        CHECK(!gfx->isActive());
        presentClearFrames(gfx, 1);
        win->restore();
        SDL_Delay(50);
        SDL_PumpEvents();
        CHECK(!win->isMinimized());
        CHECK(win->isVisible());
        CHECK(gfx->isActive());
        presentClearFrames(gfx);
    } else {
        win->restore();
    }

    win->close();
}

TEST_CASE("window.maximizeRestoreRoundTrip") {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx);
    installLifecycleWatch();

    CHECK(!win->isMaximized());
    win->maximize();
    SDL_Delay(50);
    SDL_PumpEvents();

    if (!win->isMaximized()) {
        // Fullscreen-desktop WMs / some Xvfb configs ignore maximize.
        win->restore();
        win->close();
        return;
    }

    CHECK(win->isMaximized());
    win->restore();
    SDL_Delay(50);
    SDL_PumpEvents();
    CHECK(!win->isMaximized());
    presentClearFrames(gfx);

    win->close();
}

TEST_CASE("window.focusObservationSoft") {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx);
    installLifecycleWatch();

    // Focus queries must be callable; exact focus ownership is WM-dependent
    // under Xvfb, so we only require that present stays active while visible.
    (void)win->hasFocus();
    (void)win->hasMouseFocus();

    // Losing focus alone must NOT pause presenting (alt-tab while visible).
    CHECK(gfx->isActive());
    presentClearFrames(gfx, 1);

    win->close();
}

TEST_CASE("graphics.lifecycle.appBackgroundForegroundResume") {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx);
    installLifecycleWatch();
    presentClearFrames(gfx);

    REQUIRE(gfx->isActive());

    // Simulate mobile background: surface teardown path pauses presenting.
    SDL_Event bg{};
    bg.type = SDL_APP_DIDENTERBACKGROUND;
    REQUIRE(SDL_PushEvent(&bg) == 1);
    CHECK(!gfx->isActive());
    presentClearFrames(gfx, 2);  // no-op while inactive

    // Simulate resume: rebuild surface + swapchain, then present again.
    SDL_Event fg{};
    fg.type = SDL_APP_DIDENTERFOREGROUND;
    REQUIRE(SDL_PushEvent(&fg) == 1);
    CHECK(gfx->isActive());
    presentClearFrames(gfx);

    // A second background/foreground cycle must also survive (hot resume).
    REQUIRE(SDL_PushEvent(&bg) == 1);
    CHECK(!gfx->isActive());
    REQUIRE(SDL_PushEvent(&fg) == 1);
    CHECK(gfx->isActive());
    presentClearFrames(gfx);

    win->close();
}

TEST_CASE("graphics.lifecycle.minimizeRestoreStillPresents") {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    openWindow(win, gfx);
    installLifecycleWatch();
    presentClearFrames(gfx);

    // Desktop EventSink path: MINIMIZED pauses, RESTORED resumes + marks the
    // swapchain dirty. Synthetic events keep this deterministic on headless CI.
    pushWindowEvent(SDL_WINDOWEVENT_MINIMIZED);
    CHECK(!gfx->isActive());
    presentClearFrames(gfx, 2);

    pushWindowEvent(SDL_WINDOWEVENT_RESTORED);
    CHECK(gfx->isActive());
    presentClearFrames(gfx, 5);
    CHECK_GT(win->getPixelWidth(), 0);
    CHECK_GT(win->getPixelHeight(), 0);

    // HIDDEN / SHOWN is the other desktop pair some WMs emit instead of
    // MINIMIZED / RESTORED.
    pushWindowEvent(SDL_WINDOWEVENT_HIDDEN);
    CHECK(!gfx->isActive());
    pushWindowEvent(SDL_WINDOWEVENT_SHOWN);
    CHECK(gfx->isActive());
    presentClearFrames(gfx, 2);

    win->close();
}
}  // namespace test_window_lifecycle
#endif
