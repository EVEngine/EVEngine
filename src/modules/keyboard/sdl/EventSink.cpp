// Translates SDL key and text events into engine messages.
//
// This lived in the SDL event pump, which had to consult the keyboard module
// for the key-repeat setting. Owning the translation here removes that edge.

#include "common/Capability.h"
#include "common/Module.h"
#include "keyboard/Keyboard.h"
#include "platform_event/PlatformEvent.h"
#include "platform_event/PlatformEventSink.h"

#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keyboard.h>

namespace eve::keyboard::sdl {
namespace {

using eve::platform_event::Message;
using eve::platform_event::Variant;

class KeyboardEventSink : public eve::platform_event::IPlatformEventSink {
public:
    Message *translatePlatformEvent(const void *nativeEvent) override {
        const auto &e = *static_cast<const SDL_Event *>(nativeEvent);
        switch (e.type) {
            case SDL_KEYDOWN: {
                // Repeats are dropped unless the module opted into them.
                if (e.key.repeat) {
                    auto *kb = eve::ModuleManager::getInstance<Keyboard>("Keyboard");
                    if (kb && !kb->hasKeyRepeat()) return nullptr;
                }
                const char *keyName = SDL_GetKeyName(e.key.keysym.sym);
                const char *scanName = SDL_GetScancodeName(e.key.keysym.scancode);
                return new Message("keypressed",
                                   {Variant::makeString(keyName ? keyName : ""),
                                    Variant::makeString(scanName ? scanName : ""),
                                    Variant::makeInt(e.key.repeat ? 1 : 0)});
            }
            case SDL_KEYUP: {
                const char *keyName = SDL_GetKeyName(e.key.keysym.sym);
                const char *scanName = SDL_GetScancodeName(e.key.keysym.scancode);
                return new Message("keyreleased",
                                   {Variant::makeString(keyName ? keyName : ""),
                                    Variant::makeString(scanName ? scanName : "")});
            }
            case SDL_TEXTINPUT:
                return new Message("textinput", {Variant::makeString(e.text.text)});
            case SDL_TEXTEDITING:
                return new Message("textedited", {Variant::makeString(e.edit.text),
                                                  Variant::makeInt(e.edit.start),
                                                  Variant::makeInt(e.edit.length)});
            default:
                return nullptr;
        }
    }
};

struct Register {
    Register() {
        static KeyboardEventSink sink;
        eve::cap::addListener<eve::platform_event::IPlatformEventSink>(&sink,
                                                                       eve::platform_event::IPlatformEventSink::kInput);
    }
} g_register;

}  // namespace
}  // namespace eve::keyboard::sdl
