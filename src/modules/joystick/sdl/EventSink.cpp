// Translates SDL joystick and game-controller events into engine messages, and
// keeps the module's device table in sync on hotplug.
//
// This lived in the SDL event pump, which therefore had to know about the
// Joystick module and its Pad type. Owning it here removes that edge.

#include "common/Capability.h"
#include "common/Module.h"
#include "platform_event/PlatformEvent.h"
#include "platform_event/PlatformEventSink.h"
#include "joystick/Joystick.h"
#include "joystick/Pad.h"

#include <SDL2/SDL_events.h>
#include <SDL2/SDL_gamecontroller.h>

#include <cmath>

namespace eve::joystick::sdl {
namespace {

using eve::platform_event::Message;
using eve::platform_event::Variant;

/** Axis values travel as thousandths so the message stays integral. */
int64_t axisMilli(int16_t raw) {
    return static_cast<int64_t>(std::lround(clampAxis(raw / 32768.0f) * 1000.0f));
}

class JoystickEventSink : public eve::platform_event::IPlatformEventSink {
public:
    Message *translatePlatformEvent(const void *nativeEvent) override {
        const auto &e = *static_cast<const SDL_Event *>(nativeEvent);
        auto *joy = eve::ModuleManager::getInstance<Joystick>("Joystick");
        if (!joy) return nullptr;

        switch (e.type) {
            case SDL_JOYDEVICEADDED: {
                auto *pad = joy->addJoystick(e.jdevice.which);
                if (!pad) return nullptr;
                return new Message("joystickadded",
                                   {Variant::makeInt(pad->getID()),
                                    Variant::makeString(pad->getName()),
                                    Variant::makeInt(pad->isGamepad() ? 1 : 0)});
            }
            case SDL_JOYDEVICEREMOVED: {
                auto *pad = joy->getJoystickFromID(e.jdevice.which);
                if (!pad) return nullptr;
                const int id = pad->getID();
                const std::string name = pad->getName();
                joy->removeJoystick(pad);
                return new Message("joystickremoved",
                                   {Variant::makeInt(id), Variant::makeString(name)});
            }
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP: {
                auto *pad = joy->getJoystickFromID(e.jbutton.which);
                if (!pad) return nullptr;
                return new Message(
                    e.type == SDL_JOYBUTTONDOWN ? "joystickpressed" : "joystickreleased",
                    {Variant::makeInt(pad->getID()), Variant::makeInt(e.jbutton.button)});
            }
            case SDL_JOYAXISMOTION: {
                auto *pad = joy->getJoystickFromID(e.jaxis.which);
                if (!pad) return nullptr;
                return new Message("joystickaxis",
                                   {Variant::makeInt(pad->getID()), Variant::makeInt(e.jaxis.axis),
                                    Variant::makeInt(axisMilli(e.jaxis.value))});
            }
            case SDL_JOYHATMOTION: {
                auto *pad = joy->getJoystickFromID(e.jhat.which);
                if (!pad) return nullptr;
                return new Message("joystickhat",
                                   {Variant::makeInt(pad->getID()), Variant::makeInt(e.jhat.hat),
                                    Variant::makeString(pad->getHat(e.jhat.hat))});
            }
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP: {
                auto *pad = joy->getJoystickFromID(e.cbutton.which);
                if (!pad) return nullptr;
                const char *btn = SDL_GameControllerGetStringForButton(
                    static_cast<SDL_GameControllerButton>(e.cbutton.button));
                return new Message(
                    e.type == SDL_CONTROLLERBUTTONDOWN ? "gamepadpressed" : "gamepadreleased",
                    {Variant::makeInt(pad->getID()), Variant::makeString(btn ? btn : "")});
            }
            case SDL_CONTROLLERAXISMOTION: {
                auto *pad = joy->getJoystickFromID(e.caxis.which);
                if (!pad) return nullptr;
                const char *axis = SDL_GameControllerGetStringForAxis(
                    static_cast<SDL_GameControllerAxis>(e.caxis.axis));
                return new Message("gamepadaxis", {Variant::makeInt(pad->getID()),
                                                   Variant::makeString(axis ? axis : ""),
                                                   Variant::makeInt(axisMilli(e.caxis.value))});
            }
            default:
                return nullptr;
        }
    }
};

struct Register {
    Register() {
        static JoystickEventSink sink;
        eve::cap::addListener<eve::platform_event::IPlatformEventSink>(
            &sink, eve::platform_event::IPlatformEventSink::kInput);
    }
} g_register;

}  // namespace
}  // namespace eve::joystick::sdl
