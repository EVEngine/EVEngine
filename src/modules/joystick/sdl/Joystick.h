#pragma once

#include "joystick/Joystick.h"
#include "joystick/sdl/Pad.h"

#include <list>
#include <map>
#include <string>
#include <vector>

namespace eve::joystick::sdl {

/** @brief SDL joystick/gamepad module backend (device enumeration and mappings). */
class Joystick : public eve::joystick::Joystick {
public:
    /** @brief Initializes SDL joystick subsystems used by this backend. */
    Joystick();
    /** @brief Closes all tracked pads. */
    ~Joystick() override;

    /** @brief @copydoc eve::joystick::Joystick::addJoystick */
    Pad* addJoystick(int deviceindex) override;
    /** @brief @copydoc eve::joystick::Joystick::removeJoystick */
    void removeJoystick(eve::joystick::Pad* pad) override;
    /** @brief @copydoc eve::joystick::Joystick::getJoystickFromID */
    Pad* getJoystickFromID(int instanceid) override;
    /** @brief @copydoc eve::joystick::Joystick::getJoystick */
    Pad* getJoystick(int joyindex) override;
    /** @brief @copydoc eve::joystick::Joystick::getIndex */
    int getIndex(const eve::joystick::Pad* pad) override;
    /** @brief @copydoc eve::joystick::Joystick::getJoystickCount */
    int getJoystickCount() const override;

    /** @brief @copydoc eve::joystick::Joystick::loadGamepadMappings */
    void loadGamepadMappings(const std::string& mappings) override;
    /** @brief @copydoc eve::joystick::Joystick::saveGamepadMappings */
    std::string saveGamepadMappings() override;
    /** @brief @copydoc eve::joystick::Joystick::getGamepadMappingString */
    std::string getGamepadMappingString(const std::string& guid) const override;

private:
    void checkGamepads(const std::string& guid) const;
    std::string getDeviceGUID(int deviceindex) const;

    std::vector<Pad*> activeSticks_;
    std::list<Pad*> joysticks_;
    std::map<std::string, bool> recentGamepadGUIDs_;
};

}  // namespace eve::joystick::sdl
