// Feeds SDL finger events into the touch module's press state.
//
// This lived in the SDL event pump, which therefore had to know about the touch
// module and its SDL backend. Owning it here removes that edge.
//
// SDL also exposes touch state through query functions, but some backends
// update those on another thread, so press state is only ever advanced from
// here.

#include "common/Capability.h"
#include "common/Module.h"
#include "common/config.h"
#include "platform_event/PlatformEventSink.h"
#include "touch/Touch.h"
#include "touch/sdl/Touch.h"
#include "window/Window.h"

#include <SDL2/SDL_events.h>

namespace eve::touch::sdl {
namespace {

#ifndef EVENGINE_MACOSX
/** SDL reports normalized finger coordinates; the engine works in pixels. */
void normalizedToDPICoords(double *x, double *y) {
    double w = 1.0, h = 1.0;
    if (auto *win = eve::ModuleManager::getInstance<eve::window::Window>("Window")) {
        w = win->getWidth();
        h = win->getHeight();
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

        auto *touchMod = dynamic_cast<Touch *>(
            eve::ModuleManager::getInstance<eve::touch::Touch>("Touch"));
        if (!touchMod) return false;

        eve::touch::Touch::TouchInfo info{};
        info.id = static_cast<int64_t>(e.tfinger.fingerId);
        info.x = e.tfinger.x;
        info.y = e.tfinger.y;
        info.dx = e.tfinger.dx;
        info.dy = e.tfinger.dy;
        info.pressure = e.tfinger.pressure;
#ifndef EVENGINE_MACOSX
        normalizedToDPICoords(&info.x, &info.y);
        normalizedToDPICoords(&info.dx, &info.dy);
#endif
        touchMod->onEvent(e.type, info);
        // Finger events update state only; they produce no queued Message.
        return true;
    }
};

struct Register {
    Register() {
        static TouchEventSink sink;
        eve::cap::addListener<eve::platform_event::IPlatformEventSink>(
            &sink, eve::platform_event::IPlatformEventSink::kInput);
    }
} g_register;

}  // namespace
}  // namespace eve::touch::sdl
