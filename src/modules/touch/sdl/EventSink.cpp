// Feeds SDL finger events into the touch module's press state.
//
// This lived in the SDL event pump, which therefore had to know about the touch
// module and its SDL backend. Owning it here removes that edge.
//
// SDL also exposes touch state through query functions, but some backends
// update those on another thread, so press state is only ever advanced from
// here. The Touch module binds itself as owner at construction so the hot path
// never looks up ModuleManager. DPI conversion uses the SDL window id on the
// finger event (no Window module reverse dependency).

#include "common/Capability.h"
#include "common/config.h"
#include "platform_event/PlatformEventSink.h"
#include "touch/sdl/EventSinkOwner.h"
#include "touch/sdl/Touch.h"

#include <SDL2/SDL_events.h>
#include <SDL2/SDL_video.h>

namespace eve::touch::sdl {
namespace {

Touch *g_touch = nullptr;

#ifndef EVENGINE_MACOSX
/** SDL reports normalized finger coordinates; the engine works in pixels. */
void normalizedToDPICoords(const SDL_TouchFingerEvent &finger, double *x, double *y) {
    double w = 1.0, h = 1.0;
    if (SDL_Window *native = SDL_GetWindowFromID(finger.windowID)) {
        int iw = 0, ih = 0;
        SDL_GetWindowSize(native, &iw, &ih);
        if (iw > 0) w = iw;
        if (ih > 0) h = ih;
    }
    if (x) *x = (*x) * w;
    if (y) *y = (*y) * h;
}
#endif

class TouchEventSink : public eve::platform_event::IPlatformEventSink {
public:
    bool shouldConsumePlatformEvent(const void *nativeEvent) override {
        const auto &e = *static_cast<const SDL_Event *>(nativeEvent);
        if (e.type != SDL_FINGERDOWN && e.type != SDL_FINGERUP && e.type != SDL_FINGERMOTION)
            return false;

        auto *touchMod = g_touch;
        if (!touchMod) return false;

        eve::touch::Touch::TouchInfo info{};
        info.id = static_cast<int64_t>(e.tfinger.fingerId);
        info.x = e.tfinger.x;
        info.y = e.tfinger.y;
        info.dx = e.tfinger.dx;
        info.dy = e.tfinger.dy;
        info.pressure = e.tfinger.pressure;
#ifndef EVENGINE_MACOSX
        normalizedToDPICoords(e.tfinger, &info.x, &info.y);
        normalizedToDPICoords(e.tfinger, &info.dx, &info.dy);
#endif
        touchMod->onEvent(e.type, info);
        // Finger events update state only; they produce no queued Message.
        return true;
    }
};

struct Register {
    Register() {
        static TouchEventSink sink;
        eve::cap::addListener<eve::platform_event::IPlatformEventSink>(&sink,
                                                                       eve::platform_event::IPlatformEventSink::kInput);
    }
} g_register;

}  // namespace

void bindTouchEventSinkOwner(Touch *owner) noexcept { g_touch = owner; }

}  // namespace eve::touch::sdl
