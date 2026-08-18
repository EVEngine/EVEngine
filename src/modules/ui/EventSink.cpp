// Lets ImGui see platform events before anything else claims them.
//
// The SDL event pump used to call UI::processEvent directly, which was one of
// the eight modules it had to know about.

#include "common/Capability.h"
#include "common/Module.h"
#include "event/PlatformEventSink.h"
#include "ui/UI.h"

#include <SDL2/SDL_events.h>

namespace eve::ui {
namespace {

class UIEventSink : public eve::event::IPlatformEventSink {
public:
    bool observePlatformEvent(const void *nativeEvent) override {
        if (auto *ui = eve::ModuleManager::getInstance<UI>("UI"))
            ui->processEvent(static_cast<const SDL_Event *>(nativeEvent));
        // ImGui records the event for its own state but does not claim it; the
        // game still receives the matching message.
        return false;
    }
};

struct Register {
    Register() {
        static UIEventSink sink;
        eve::cap::addListener<eve::event::IPlatformEventSink>(
            &sink, eve::event::IPlatformEventSink::kObserver);
    }
} g_register;

}  // namespace
}  // namespace eve::ui
