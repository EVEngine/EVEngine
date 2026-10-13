// Window minimize / restore and app background / foreground resume.
//
// Desktop: minimize → restore must pause then resume presenting without crash.
// Mobile (and portable simulation): SDL_APP_* events must deactivate graphics,
// request surface recreate on resume, and allow present again.
//
// Focus-only alt-tab is intentionally not paused: the window may still be
// visible. Coverage for hasFocus is a soft observation (WM-dependent).

#include <optional>

#include <SDL2/SDL.h>

#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "platform_event/PlatformEvent.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
