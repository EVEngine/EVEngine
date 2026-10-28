#pragma once

#include "common/Export.h"
#include "common/Module.h"
#include "joystick/Pad.h"

#include <string>

namespace eve::joystick {

/**
 * @brief Joystick / gamepad manager. Tracks connected pads and gamecontroller mappings.
 */
class EVENGINE_API_PLATFORM Joystick : public Module {
public:
    Module_REG(Joystick);

    /** @brief Closes tracked pads and releases backend resources. */
    virtual ~Joystick();

    /** @brief Open a device by SDL device index; returns null on failure. */
    virtual Pad* addJoystick(int deviceindex) = 0;
    /** @brief Removes and destroys a previously opened pad. */
    virtual void removeJoystick(Pad* pad) = 0;

    /** @brief Looks up a pad by SDL instance id; null if unknown. */
    virtual Pad* getJoystickFromID(int instanceid) = 0;
    /** @brief Pad at a dense joyindex in [0, getJoystickCount()). */
    virtual Pad* getJoystick(int joyindex) = 0;
    /** @brief Dense index of pad, or -1 if not tracked. */
    virtual int getIndex(const Pad* pad) = 0;
    /** @brief Number of currently tracked pads. */
    virtual int getJoystickCount() const = 0;

    /** @brief Load SDL GameController mapping database (newline-separated). */
    virtual void loadGamepadMappings(const std::string& mappings) = 0;
    /** @brief Serializes the in-memory GameController mapping database. */
    virtual std::string saveGamepadMappings() = 0;
    /** @brief Mapping string for a device GUID, or empty if none. */
    virtual std::string getGamepadMappingString(const std::string& guid) const = 0;
};

}  // namespace eve::joystick
