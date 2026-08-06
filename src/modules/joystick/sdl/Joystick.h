#pragma once

#include "joystick/Joystick.h"
#include "joystick/sdl/Pad.h"

#include <list>
#include <map>
#include <string>
#include <vector>

namespace eve::joystick::sdl {

class Joystick : public eve::joystick::Joystick {
public:
    Joystick();
    ~Joystick() override;

    Pad* addJoystick(int deviceindex) override;
    void removeJoystick(eve::joystick::Pad* pad) override;
    Pad* getJoystickFromID(int instanceid) override;
    Pad* getJoystick(int joyindex) override;
    int getIndex(const eve::joystick::Pad* pad) override;
    int getJoystickCount() const override;

    void loadGamepadMappings(const std::string& mappings) override;
    std::string saveGamepadMappings() override;
    std::string getGamepadMappingString(const std::string& guid) const override;

private:
    void checkGamepads(const std::string& guid) const;
    std::string getDeviceGUID(int deviceindex) const;

    std::vector<Pad*> activeSticks_;
    std::list<Pad*> joysticks_;
    std::map<std::string, bool> recentGamepadGUIDs_;
};

}  // namespace eve::joystick::sdl
