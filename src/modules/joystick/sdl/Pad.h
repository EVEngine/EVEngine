#pragma once

#include "joystick/Pad.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_haptic.h>

#include <string>

namespace eve::joystick::sdl {

/** @brief SDL 手柄/摇杆后端实现（含 GameController 与振动）。 */
class Pad : public eve::joystick::Pad {
public:
    explicit Pad(int id);
    Pad(int id, int joyindex);
    ~Pad() override;

    bool open(int deviceindex) override;
    void close() override;
    bool isConnected() const override;

    std::string getName() const override;

    int getAxisCount() const override;
    int getButtonCount() const override;
    int getHatCount() const override;

    float getAxis(int axisindex) const override;
    std::vector<float> getAxes() const override;
    std::string getHat(int hatindex) const override;

    bool isDown(int button) const override;
    bool isDown(const std::vector<int>& buttons) const override;

    bool openGamepad(int deviceindex) override;
    bool isGamepad() const override;

    float getGamepadAxis(const std::string& axis) const override;
    bool isGamepadDown(const std::string& button) const override;
    bool isGamepadDown(const std::vector<std::string>& buttons) const override;

    std::string getGamepadMappingString() const override;

    void* getHandle() const override;
    std::string getGUID() const override;
    int getInstanceID() const override;
    int getID() const override;

    void getDeviceInfo(int& vendorID, int& productID, int& productVersion) const override;

    bool isVibrationSupported() override;
    bool setVibration(float left, float right, float duration = -1.0f) override;
    bool setVibration() override;
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
