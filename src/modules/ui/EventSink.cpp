// Lets ImGui see platform events before anything else claims them.
//
// The SDL event pump used to call UI::processEvent directly, which was one of
// the eight modules it had to know about. The UI module binds itself as owner
// at construction so the hot path never looks up ModuleManager.

#include "common/Capability.h"
#include "platform_event/PlatformEventSink.h"
#include "ui/EventSinkOwner.h"
#include "ui/UI.h"

#include <SDL2/SDL_events.h>

namespace eve::ui {
namespace {

UI* g_owner = nullptr;

class UIEventSink : public eve::platform_event::IPlatformEventSink {
public:
    bool shouldConsumePlatformEvent(const void *nativeEvent) override {
        if (g_owner) g_owner->processEvent(static_cast<const SDL_Event*>(nativeEvent));
        // ImGui records the event for its own state but does not claim it; the
        // game still receives the matching message.
        return false;
    }
};

struct Register {
    Register() {
        static UIEventSink sink;
        eve::cap::addListener<eve::platform_event::IPlatformEventSink>(
            &sink, eve::platform_event::IPlatformEventSink::kObserver);
    }
} g_register;

}  // namespace

void bindUIEventSinkOwner(UI* owner) noexcept { g_owner = owner; }

}  // namespace eve::ui
