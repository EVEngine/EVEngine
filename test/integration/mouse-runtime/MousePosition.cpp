// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"default":"render-window","module":"mouse","modules":["mouse","window"]}
*/
// clang-format on

#include "mouse/Mouse.h"
#include "window/Window.h"
#include "zeroerr/unittest.h"

#if defined(_WIN32)
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

namespace {
// Model a desktop session that accepts warps but delivers no mouse motion.
LRESULT CALLBACK suppressMouseMotion(int code, WPARAM removed, LPARAM message) {
    if (code >= 0 && removed == PM_REMOVE) {
        auto* event = reinterpret_cast<MSG*>(message);
        if (event->message == WM_MOUSEMOVE) event->message = WM_NULL;
    }
    return CallNextHookEx(nullptr, code, removed, message);
}

struct DesktopState {
    POINT                cursor{};
    HHOOK                hook   = nullptr;
    eve::window::Window* window = nullptr;
    DesktopState() { GetCursorPos(&cursor); }
    ~DesktopState() {
        if (hook) UnhookWindowsHookEx(hook);
        if (window) window->close();
        SetCursorPos(cursor.x, cursor.y);
    }
};
}  // namespace

TEST_CASE("mouse.positionWithoutMotionDelivery") {
    DesktopState desktop;
    desktop.window = eve::window::Window::create();
    REQUIRE(desktop.window != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 320;
    settings.height   = 240;
    settings.centered = true;
    REQUIRE(desktop.window->setWindowSettings(settings));
    auto* window = static_cast<SDL_Window*>(desktop.window->getHandle());
    REQUIRE(window != nullptr);
    auto* mouse = eve::mouse::Mouse::create();
    REQUIRE(mouse != nullptr);
    SDL_RaiseWindow(window);
    SDL_PumpEvents();
    mouse->setPosition(50, 60);
    SDL_SysWMinfo wmInfo{};
    SDL_VERSION(&wmInfo.version);
    REQUIRE(SDL_GetWindowWMInfo(window, &wmInfo) == SDL_TRUE);
    // Seed the ordinary SDL cache, then remove mouse focus without keyboard focus.
    SendMessageW(wmInfo.info.win.window, WM_MOUSEMOVE, 0, MAKELPARAM(50, 60));
    desktop.hook = SetWindowsHookExW(WH_GETMESSAGE, suppressMouseMotion, nullptr, GetCurrentThreadId());
    REQUIRE(desktop.hook != nullptr);
    SendMessageW(wmInfo.info.win.window, WM_MOUSELEAVE, 0, 0);
    mouse->setPosition(80, 60);
    REQUIRE(SDL_GetMouseFocus() == nullptr);
    REQUIRE(SDL_GetKeyboardFocus() == window);
    int cachedX, cachedY;
    SDL_GetMouseState(&cachedX, &cachedY);
    REQUIRE(cachedX != 80);
    double x, y;
    mouse->getPosition(x, y);
    CHECK(x == 80);
    CHECK(y == 60);
    CHECK(mouse->getX() == 80);
    CHECK(mouse->getY() == 60);
    mouse->setY(90);
    mouse->getPosition(x, y);
    CHECK(x == 80);
    CHECK(y == 90);
    mouse->setX(100);
    mouse->getPosition(x, y);
    CHECK(x == 100);
    CHECK(y == 90);
}
#endif
