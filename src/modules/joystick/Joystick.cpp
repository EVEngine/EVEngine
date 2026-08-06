#include "joystick/Joystick.h"
#include "joystick/sdl/Joystick.h"
#include "joystick/Pad.h"

#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::joystick {

Module_IMPL(Joystick, new sdl::Joystick());

Joystick::~Joystick() {}

float clampAxis(float x) {
    if (std::fabs(x) < 0.01f) return 0.0f;
    if (x < -0.99f) return -1.0f;
    if (x > 0.99f) return 1.0f;
    return x;
}

void Joystick::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Joystick::create, false);
    expose(cls);
}

void Joystick::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Joystick::getName);
    cls.addFunc("getJoystickCount", &Joystick::getJoystickCount);
    cls.addFunc("loadGamepadMappings", &Joystick::loadGamepadMappings);
    cls.addFunc("saveGamepadMappings", &Joystick::saveGamepadMappings);
    cls.addFunc("getGamepadMappingString", &Joystick::getGamepadMappingString);
}

}  // namespace eve::joystick
