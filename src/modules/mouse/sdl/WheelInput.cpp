#include <SDL2/SDL_events.h>
#include <SDL2/SDL_version.h>
#include <cmath>
#include "mouse/Mouse.h"
#include "platform_event/PlatformEventSink.h"

namespace eve::mouse::sdl {
namespace {
// The linked backend owns one event-thread frame snapshot. Reading it never
// consumes input, so two independent camera/UI readers see the same frame.
class WheelSink final : public platform_event::IPlatformEventSink {
public:
    platform_event::Message* translatePlatformEvent(const void* nativeEvent) override {
        const auto& event = *static_cast<const SDL_Event*>(nativeEvent);
        if (event.type != SDL_MOUSEWHEEL) return nullptr;
#if SDL_VERSION_ATLEAST(2, 0, 18)
        const float x = event.wheel.preciseX, y = event.wheel.preciseY;
#else
        const float x = static_cast<float>(event.wheel.x), y = static_cast<float>(event.wheel.y);
#endif
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f;
        if (std::isfinite(x) && std::isfinite(y)) {
            pendingX += x * direction;
            pendingY += y * direction;
        }
        return nullptr;  // Other listeners may still inspect the event.
    }
    void onPumpFinished() override {
        frameX   = pendingX;
        frameY   = pendingY;
        pendingX = pendingY = 0.f;
    }
    float frameX = 0.f, frameY = 0.f;

private:
    float pendingX = 0.f, pendingY = 0.f;
};
WheelSink& wheelSink() {
    static WheelSink sink;
    return sink;
}
struct Register {
    Register() {
        cap::addListener<platform_event::IPlatformEventSink>(&wheelSink(), platform_event::IPlatformEventSink::kInput);
    }
} registration;
}  // namespace
}  // namespace eve::mouse::sdl

namespace eve::mouse {
float Mouse::getWheelX() const { return sdl::wheelSink().frameX; }
float Mouse::getWheelY() const { return sdl::wheelSink().frameY; }
}  // namespace eve::mouse
