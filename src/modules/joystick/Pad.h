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
    virtual ~Pad() = default;

    virtual bool open(int deviceindex) = 0;
    virtual void close() = 0;
    virtual bool isConnected() const = 0;

    virtual std::string getName() const = 0;

    virtual int getAxisCount() const = 0;
    virtual int getButtonCount() const = 0;
    virtual int getHatCount() const = 0;

    virtual float getAxis(int axisindex) const = 0;
    virtual std::vector<float> getAxes() const = 0;
    virtual std::string getHat(int hatindex) const = 0;

    virtual bool isDown(int button) const = 0;
    virtual bool isDown(const std::vector<int>& buttons) const = 0;

    virtual bool openGamepad(int deviceindex) = 0;
    virtual bool isGamepad() const = 0;

    virtual float getGamepadAxis(const std::string& axis) const = 0;
    virtual bool isGamepadDown(const std::string& button) const = 0;
    virtual bool isGamepadDown(const std::vector<std::string>& buttons) const = 0;

    virtual std::string getGamepadMappingString() const = 0;

    virtual void* getHandle() const = 0;
    virtual std::string getGUID() const = 0;
    virtual int getInstanceID() const = 0;
    virtual int getID() const = 0;

    virtual void getDeviceInfo(int& vendorID, int& productID, int& productVersion) const = 0;

    virtual bool isVibrationSupported() = 0;
    virtual bool setVibration(float left, float right, float duration = -1.0f) = 0;
    virtual bool setVibration() = 0;
    virtual void getVibration(float& left, float& right) = 0;
};

float clampAxis(float x);

}  // namespace eve::joystick
