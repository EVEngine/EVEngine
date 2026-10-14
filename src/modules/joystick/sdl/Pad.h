#pragma once

#include "joystick/Pad.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_haptic.h>

#include <string>

namespace eve::joystick::sdl {

/** @brief SDL joystick/gamepad backend (GameController + optional haptic rumble). */
class Pad : public eve::joystick::Pad {
public:
    /** @brief Creates a closed pad with the given engine id. */
    explicit Pad(int id);
    /** @brief Creates a pad and opens SDL device joyindex. */
    Pad(int id, int joyindex);
    /** @brief Closes SDL handles and stops rumble. */
    ~Pad() override;

    /** @brief @copydoc eve::joystick::Pad::open */
    bool open(int deviceindex) override;
    /** @brief @copydoc eve::joystick::Pad::close */
    void close() override;
    /** @brief @copydoc eve::joystick::Pad::isConnected */
    bool isConnected() const override;

    /** @brief @copydoc eve::joystick::Pad::getName */
    std::string getName() const override;

    /** @brief @copydoc eve::joystick::Pad::getAxisCount */
    int getAxisCount() const override;
    /** @brief @copydoc eve::joystick::Pad::getButtonCount */
    int getButtonCount() const override;
    /** @brief @copydoc eve::joystick::Pad::getHatCount */
    int getHatCount() const override;

    /** @brief @copydoc eve::joystick::Pad::getAxis */
    float getAxis(int axisindex) const override;
    /** @brief @copydoc eve::joystick::Pad::getAxes */
    std::vector<float> getAxes() const override;
    /** @brief @copydoc eve::joystick::Pad::getHat */
    std::string getHat(int hatindex) const override;

    /** @brief @copydoc eve::joystick::Pad::isDown */
    bool isDown(int button) const override;
    /** @brief @copydoc eve::joystick::Pad::isDown */
    bool isDown(const std::vector<int>& buttons) const override;

    /** @brief @copydoc eve::joystick::Pad::openGamepad */
    bool openGamepad(int deviceindex) override;
    /** @brief @copydoc eve::joystick::Pad::isGamepad */
    bool isGamepad() const override;

    /** @brief @copydoc eve::joystick::Pad::getGamepadAxis */
    float getGamepadAxis(const std::string& axis) const override;
    /** @brief @copydoc eve::joystick::Pad::isGamepadDown */
    bool isGamepadDown(const std::string& button) const override;
    /** @brief @copydoc eve::joystick::Pad::isGamepadDown */
    bool isGamepadDown(const std::vector<std::string>& buttons) const override;

    /** @brief @copydoc eve::joystick::Pad::getGamepadMappingString */
    std::string getGamepadMappingString() const override;

    /** @brief @copydoc eve::joystick::Pad::getHandle */
    void* getHandle() const override;
    /** @brief @copydoc eve::joystick::Pad::getGUID */
    std::string getGUID() const override;
    /** @brief @copydoc eve::joystick::Pad::getInstanceID */
    int getInstanceID() const override;
    /** @brief @copydoc eve::joystick::Pad::getID */
    int getID() const override;

    /** @brief @copydoc eve::joystick::Pad::getDeviceInfo */
    void getDeviceInfo(int& vendorID, int& productID, int& productVersion) const override;

    /** @brief @copydoc eve::joystick::Pad::isVibrationSupported */
    bool isVibrationSupported() override;
    /** @brief @copydoc eve::joystick::Pad::setVibration */
    bool setVibration(float left, float right, float duration = -1.0f) override;
    /** @brief @copydoc eve::joystick::Pad::setVibration */
    bool setVibration() override;
    /** @brief @copydoc eve::joystick::Pad::getVibration */
    void getVibration(float& left, float& right) override;

private:
    bool checkCreateHaptic();
    bool runVibrationEffect();

    SDL_Joystick* joyhandle_ = nullptr;
    SDL_GameController* controller_ = nullptr;
    SDL_Haptic* haptic_ = nullptr;

    SDL_JoystickID instanceid_ = -1;
    std::string guid_;
    int id_ = 0;
    std::string name_;

    struct Vibration {
        float left = 0.0f;
        float right = 0.0f;
        SDL_HapticEffect effect{};
        Uint16 data[4]{};
        int id = -1;
        Uint32 endtime = SDL_HAPTIC_INFINITY;
    } vibration_;
};

}  // namespace eve::joystick::sdl
