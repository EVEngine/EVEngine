// Keeps the window settings and the render surface aligned with the platform.
//
// Two things used to live in the SDL event pump and forced it to depend on both
// window and graphics: resize handling, and the app background/foreground
// callback that tears down and rebuilds the Vulkan surface on mobile.

#include "common/Capability.h"
#include "common/Module.h"
#include "common/WindowSurfaceHost.h"
#include "event/PlatformEventSink.h"
#include "window/Window.h"
#include "window/sdl/Window.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <algorithm>

namespace eve::window::sdl {
namespace {

/** Re-reads the real window size after a resize, rotation or resume. */
void syncWindowPixelSize() {
    auto *win = eve::ModuleManager::getInstance<eve::window::Window>("Window");
    if (!win) return;
    auto *sdlWin = dynamic_cast<Window *>(win);
    if (!sdlWin) return;
    SDL_Window *native = static_cast<SDL_Window *>(sdlWin->getHandle());
    if (!native) return;

    int lw = 0, lh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(native, &lw, &lh);
    SDL_Vulkan_GetDrawableSize(native, &pw, &ph);
    if (pw <= 0 || ph <= 0) {
        pw = lw;
        ph = lh;
    }
    WindowSettings s = win->getWindowSettings();
    s.width = static_cast<uint16_t>(std::max(lw, 1));
    s.height = static_cast<uint16_t>(std::max(lh, 1));
    // Keep settings + graphics viewport aligned after rotation / resume.
    sdlWin->updateSettings(s, true);
}

/**
 * SDL event watches fire the moment the event is posted inside SDL, unlike
 * SDL_PollEvent. The mobile background/foreground transitions have to be
 * handled there: by the time the poll loop runs, the native surface is already
 * gone.
 */
int SDLCALL watchAppEvents(void * /*udata*/, SDL_Event *event) {
    auto *surfaceHost = eve::cap::query<IWindowSurfaceHost>();
    if (!surfaceHost) return 1;

    switch (event->type) {
        case SDL_APP_DIDENTERBACKGROUND:
            // Stop presenting: the native surface is being torn down.
            surfaceHost->setActive(false);
            break;
        case SDL_APP_WILLENTERFOREGROUND:
        case SDL_APP_DIDENTERFOREGROUND:
            // The native window is recreated on resume; rebuild the render
            // surface and swapchain, then resume presenting.
            surfaceHost->requestSurfaceRecreate();
            surfaceHost->setActive(true);
            syncWindowPixelSize();
            break;
        default:
            break;
    }
    return 1;
}

class WindowEventSink : public eve::event::IPlatformEventSink {
public:
    bool observePlatformEvent(const void *nativeEvent) override {
        const auto &e = *static_cast<const SDL_Event *>(nativeEvent);
        if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                                          e.window.event == SDL_WINDOWEVENT_RESIZED))
            syncWindowPixelSize();
        // Never consume: SDL_WINDOWEVENT_CLOSE still has to reach the pump,
        // which turns it into "quit".
        return false;
    }

    void onPumpFinished() override {
        // Deferred to the first pump because the watch can only be added once
        // SDL is initialized, which is later than static registration.
        if (watchInstalled_) return;
        watchInstalled_ = true;
        SDL_AddEventWatch(watchAppEvents, nullptr);
    }

private:
    bool watchInstalled_ = false;
};

struct Register {
    Register() {
        static WindowEventSink sink;
        eve::cap::addListener<eve::event::IPlatformEventSink>(
            &sink, eve::event::IPlatformEventSink::kSurface);
    }
} g_register;

}  // namespace
}  // namespace eve::window::sdl
