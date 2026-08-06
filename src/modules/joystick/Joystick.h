#pragma once

#include "common/Module.h"
#include "joystick/Pad.h"

#include <string>

namespace eve::joystick {

/**
 * Joystick / gamepad manager. Tracks connected pads and gamecontroller mappings.
 */
class Joystick : public Module {
public:
    Module_REG(Joystick);

    virtual ~Joystick();

    /** Open a device by SDL device index; returns null on failure. */
    virtual Pad* addJoystick(int deviceindex) = 0;
    virtual void removeJoystick(Pad* pad) = 0;

    virtual Pad* getJoystickFromID(int instanceid) = 0;
    virtual Pad* getJoystick(int joyindex) = 0;
    virtual int getIndex(const Pad* pad) = 0;
    virtual int getJoystickCount() const = 0;

    /** Load SDL GameController mapping database (newline-separated). */
    virtual void loadGamepadMappings(const std::string& mappings) = 0;
    virtual std::string saveGamepadMappings() = 0;
    virtual std::string getGamepadMappingString(const std::string& guid) const = 0;
};

}  // namespace eve::joystick
