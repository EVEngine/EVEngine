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

    // Pad instances are owned by the Joystick module (created/removed by the
    // SDL event sink), so they are exposed non-owning: the script constructor
    // returns null and pushed pads are never deleted by the GC.
    auto pad = table.addClass<Pad>(
        "Pad", std::function<Pad*()>([]() -> Pad* { return nullptr; }), true);
    pad.addFunc("getName", &Pad::getName);
    pad.addFunc("getAxisCount", &Pad::getAxisCount);
    pad.addFunc("getButtonCount", &Pad::getButtonCount);
    pad.addFunc("getHatCount", &Pad::getHatCount);
    pad.addFunc("getAxis", &Pad::getAxis);
    pad.addFunc("getAxes", &Pad::getAxes);
    pad.addFunc("getHat", &Pad::getHat);
    pad.addFunc("isDown", static_cast<bool (Pad::*)(int) const>(&Pad::isDown));
    pad.addFunc("isGamepad", &Pad::isGamepad);
    pad.addFunc("getGamepadAxis", &Pad::getGamepadAxis);
    pad.addFunc("isGamepadDown",
                static_cast<bool (Pad::*)(const std::string&) const>(&Pad::isGamepadDown));
    pad.addFunc("getGamepadMappingString", &Pad::getGamepadMappingString);
    pad.addFunc("getGUID", &Pad::getGUID);
    pad.addFunc("getInstanceID", &Pad::getInstanceID);
    pad.addFunc("getID", &Pad::getID);
    pad.addFunc("getVendorID", [](Pad* self) -> int {
        int vendor = 0, product = 0, version = 0;
        if (self) self->getDeviceInfo(vendor, product, version);
        return vendor;
    });
    pad.addFunc("getProductID", [](Pad* self) -> int {
        int vendor = 0, product = 0, version = 0;
        if (self) self->getDeviceInfo(vendor, product, version);
        return product;
    });
    pad.addFunc("getProductVersion", [](Pad* self) -> int {
        int vendor = 0, product = 0, version = 0;
        if (self) self->getDeviceInfo(vendor, product, version);
        return version;
    });
    pad.addFunc("isVibrationSupported", &Pad::isVibrationSupported);
    pad.addFunc("setVibration", [](Pad* self, float left, float right) -> bool {
        return self && self->setVibration(left, right);
    });
    pad.addFunc("setVibrationTimed",
                [](Pad* self, float left, float right, float duration) -> bool {
                    return self && self->setVibration(left, right, duration);
                });
    pad.addFunc("stopVibration", [](Pad* self) -> bool { return self && self->setVibration(); });
    pad.addFunc("getVibrationLeft", [](Pad* self) -> float {
        float left = 0.f, right = 0.f;
        if (self) self->getVibration(left, right);
        return left;
    });
    pad.addFunc("getVibrationRight", [](Pad* self) -> float {
        float left = 0.f, right = 0.f;
        if (self) self->getVibration(left, right);
        return right;
    });
}

void Joystick::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Joystick::getName);
    cls.addFunc("getJoystickCount", &Joystick::getJoystickCount);
    cls.addFunc("getJoystick", &Joystick::getJoystick);
    cls.addFunc("getJoystickFromID", &Joystick::getJoystickFromID);
    cls.addFunc("getIndex", &Joystick::getIndex);
    cls.addFunc("addJoystick", &Joystick::addJoystick);
    cls.addFunc("removeJoystick", &Joystick::removeJoystick);
    cls.addFunc("loadGamepadMappings", &Joystick::loadGamepadMappings);
    cls.addFunc("saveGamepadMappings", &Joystick::saveGamepadMappings);
    cls.addFunc("getGamepadMappingString", &Joystick::getGamepadMappingString);
}

}  // namespace eve::joystick
