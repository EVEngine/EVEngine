#pragma once

#include <string>
#include <vector>

namespace eve::joystick {

/**
 * @brief One connected (or previously connected) joystick / gamepad device.
 * Gamepad axis/button names follow SDL GameController strings
 * (e.g. "leftx", "a", "dpup"). Hat directions: c/u/d/l/r/lu/ld/ru/rd.
 */
class Pad {
public:
    /** @brief Releases device handles; safe to call when already closed. */
    virtual ~Pad() = default;

    /** @brief Opens the device by SDL joystick device index. @return false on failure. */
    virtual bool open(int deviceindex) = 0;
    /** @brief Closes joystick and gamecontroller handles. */
    virtual void close() = 0;
    /** @brief True while the underlying device is still connected. */
    virtual bool isConnected() const = 0;

    /** @brief Human-readable device name from the OS/driver. */
    virtual std::string getName() const = 0;

    /** @brief Number of raw joystick axes. */
    virtual int getAxisCount() const = 0;
    /** @brief Number of raw joystick buttons. */
    virtual int getButtonCount() const = 0;
    /** @brief Number of hat (D-pad) controls. */
    virtual int getHatCount() const = 0;

    /** @brief Normalized axis value in [-1, 1] for a raw axis index. */
    virtual float getAxis(int axisindex) const = 0;
    /** @brief Snapshot of all raw axis values. */
    virtual std::vector<float> getAxes() const = 0;
    /** @brief Hat direction string for a hat index (see class brief). */
    virtual std::string getHat(int hatindex) const = 0;

    /** @brief True if the raw button index is currently pressed. */
    virtual bool isDown(int button) const = 0;
    /** @brief True if any of the raw button indices is currently pressed. */
    virtual bool isDown(const std::vector<int>& buttons) const = 0;

    /** @brief Opens the device as an SDL GameController. @return false on failure. */
    virtual bool openGamepad(int deviceindex) = 0;
    /** @brief True when a GameController mapping is active for this pad. */
    virtual bool isGamepad() const = 0;

    /** @brief Named gamepad axis value (SDL GameController axis string). */
    virtual float getGamepadAxis(const std::string& axis) const = 0;
    /** @brief True if the named gamepad button is pressed. */
    virtual bool isGamepadDown(const std::string& button) const = 0;
    /** @brief True if any of the named gamepad buttons is pressed. */
    virtual bool isGamepadDown(const std::vector<std::string>& buttons) const = 0;

    /** @brief Current SDL GameController mapping string for this device. */
    virtual std::string getGamepadMappingString() const = 0;

    /** @brief Native handle (SDL_Joystick* / implementation-defined). */
    virtual void* getHandle() const = 0;
    /** @brief Stable GUID string for mapping lookup. */
    virtual std::string getGUID() const = 0;
    /** @brief SDL instance id that stays unique while the device is open. */
    virtual int getInstanceID() const = 0;
    /** @brief Engine-side stable pad id assigned at construction. */
    virtual int getID() const = 0;

    /** @brief USB/HID vendor, product, and version ids when available. */
    virtual void getDeviceInfo(int& vendorID, int& productID, int& productVersion) const = 0;

    /** @brief True when haptic/rumble output is available. */
    virtual bool isVibrationSupported() = 0;
    /** @brief Starts rumble; duration &lt; 0 means until stopped. @return false if unsupported. */
    virtual bool setVibration(float left, float right, float duration = -1.0f) = 0;
    /** @brief Stops active rumble. */
    virtual bool setVibration() = 0;
    /** @brief Reads the last requested left/right rumble strengths. */
    virtual void getVibration(float& left, float& right) = 0;
};

/** @brief Clamps a raw axis sample into [-1, 1]. */
float clampAxis(float x);

}  // namespace eve::joystick
