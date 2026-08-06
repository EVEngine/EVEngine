#include "joystick/sdl/Pad.h"

#include "common/Exception.h"

#include <algorithm>
#include <cstring>
#include <limits>

#ifndef SDL_TICKS_PASSED
#define SDL_TICKS_PASSED(A, B) ((Sint32)((B) - (A)) <= 0)
#endif

namespace eve::joystick::sdl {
namespace {

std::string hatToString(Uint8 value) {
    switch (value) {
    case SDL_HAT_CENTERED: return "c";
    case SDL_HAT_UP: return "u";
    case SDL_HAT_RIGHT: return "r";
    case SDL_HAT_DOWN: return "d";
    case SDL_HAT_LEFT: return "l";
    case SDL_HAT_RIGHTUP: return "ru";
    case SDL_HAT_RIGHTDOWN: return "rd";
    case SDL_HAT_LEFTUP: return "lu";
    case SDL_HAT_LEFTDOWN: return "ld";
    default: return "";
    }
}

}  // namespace

Pad::Pad(int id) : id_(id) {}

Pad::Pad(int id, int joyindex) : id_(id) {
    open(joyindex);
}

Pad::~Pad() {
    close();
}

bool Pad::open(int deviceindex) {
    close();

    joyhandle_ = SDL_JoystickOpen(deviceindex);
    if (!joyhandle_) return false;

    instanceid_ = SDL_JoystickInstanceID(joyhandle_);

    char cstr[33];
    SDL_JoystickGUID sdlguid = SDL_JoystickGetGUID(joyhandle_);
    SDL_JoystickGetGUIDString(sdlguid, cstr, static_cast<int>(sizeof(cstr)));
    guid_ = cstr;

    openGamepad(deviceindex);

    const char* joyname = SDL_JoystickName(joyhandle_);
    if (!joyname && controller_) joyname = SDL_GameControllerName(controller_);
    name_ = joyname ? joyname : "";

    return isConnected();
}

void Pad::close() {
    if (haptic_) SDL_HapticClose(haptic_);
    if (controller_) SDL_GameControllerClose(controller_);
    if (joyhandle_) SDL_JoystickClose(joyhandle_);

    joyhandle_ = nullptr;
    controller_ = nullptr;
    haptic_ = nullptr;
    instanceid_ = -1;
    vibration_ = Vibration{};
}

bool Pad::isConnected() const {
    return joyhandle_ != nullptr && SDL_JoystickGetAttached(joyhandle_);
}

std::string Pad::getName() const {
    return name_;
}

int Pad::getAxisCount() const {
    return isConnected() ? SDL_JoystickNumAxes(joyhandle_) : 0;
}

int Pad::getButtonCount() const {
    return isConnected() ? SDL_JoystickNumButtons(joyhandle_) : 0;
}

int Pad::getHatCount() const {
    return isConnected() ? SDL_JoystickNumHats(joyhandle_) : 0;
}

float Pad::getAxis(int axisindex) const {
    if (!isConnected() || axisindex < 0 || axisindex >= getAxisCount()) return 0.f;
    return clampAxis(static_cast<float>(SDL_JoystickGetAxis(joyhandle_, axisindex)) / 32768.0f);
}

std::vector<float> Pad::getAxes() const {
    std::vector<float> axes;
    int count = getAxisCount();
    if (!isConnected() || count <= 0) return axes;
    axes.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++)
        axes.push_back(clampAxis(static_cast<float>(SDL_JoystickGetAxis(joyhandle_, i)) / 32768.0f));
    return axes;
}

std::string Pad::getHat(int hatindex) const {
    if (!isConnected() || hatindex < 0 || hatindex >= getHatCount()) return "";
    return hatToString(SDL_JoystickGetHat(joyhandle_, hatindex));
}

bool Pad::isDown(int button) const {
    return isDown(std::vector<int>{button});
}

bool Pad::isDown(const std::vector<int>& buttons) const {
    if (!isConnected()) return false;
    int numbuttons = getButtonCount();
    for (int button : buttons) {
        if (button < 0 || button >= numbuttons) continue;
        if (SDL_JoystickGetButton(joyhandle_, button) == 1) return true;
    }
    return false;
}

bool Pad::openGamepad(int deviceindex) {
    if (!SDL_IsGameController(deviceindex)) return false;

    if (controller_) {
        SDL_GameControllerClose(controller_);
        controller_ = nullptr;
    }

    controller_ = SDL_GameControllerOpen(deviceindex);
    return isGamepad();
}

bool Pad::isGamepad() const {
    return controller_ != nullptr;
}

float Pad::getGamepadAxis(const std::string& axis) const {
    if (!isConnected() || !isGamepad()) return 0.f;
    SDL_GameControllerAxis sdlaxis = SDL_GameControllerGetAxisFromString(axis.c_str());
    if (sdlaxis == SDL_CONTROLLER_AXIS_INVALID) return 0.f;
    Sint16 value = SDL_GameControllerGetAxis(controller_, sdlaxis);
    return clampAxis(static_cast<float>(value) / 32768.0f);
}

bool Pad::isGamepadDown(const std::string& button) const {
    return isGamepadDown(std::vector<std::string>{button});
}

bool Pad::isGamepadDown(const std::vector<std::string>& buttons) const {
    if (!isConnected() || !isGamepad()) return false;
    for (const auto& name : buttons) {
        SDL_GameControllerButton sdlbutton = SDL_GameControllerGetButtonFromString(name.c_str());
        if (sdlbutton == SDL_CONTROLLER_BUTTON_INVALID) continue;
        if (SDL_GameControllerGetButton(controller_, sdlbutton) == 1) return true;
    }
    return false;
}

std::string Pad::getGamepadMappingString() const {
    char* sdlmapping = nullptr;
    if (controller_) sdlmapping = SDL_GameControllerMapping(controller_);
    if (!sdlmapping) {
        SDL_JoystickGUID sdlguid = SDL_JoystickGetGUIDFromString(guid_.c_str());
        sdlmapping = SDL_GameControllerMappingForGUID(sdlguid);
    }
    if (!sdlmapping) return "";

    std::string mappingstr(sdlmapping);
    SDL_free(sdlmapping);

    if (mappingstr.find_last_of(',') != mappingstr.length() - 1) mappingstr += ",";
    mappingstr += "platform:" + std::string(SDL_GetPlatform());
    return mappingstr;
}

void* Pad::getHandle() const {
    return joyhandle_;
}

std::string Pad::getGUID() const {
    return guid_;
}

int Pad::getInstanceID() const {
    return static_cast<int>(instanceid_);
}

int Pad::getID() const {
    return id_;
}

void Pad::getDeviceInfo(int& vendorID, int& productID, int& productVersion) const {
#if SDL_VERSION_ATLEAST(2, 0, 6)
    if (joyhandle_) {
        vendorID = SDL_JoystickGetVendor(joyhandle_);
        productID = SDL_JoystickGetProduct(joyhandle_);
        productVersion = SDL_JoystickGetProductVersion(joyhandle_);
        return;
    }
#endif
    vendorID = productID = productVersion = 0;
}

bool Pad::checkCreateHaptic() {
    if (!isConnected()) return false;

    if (!SDL_WasInit(SDL_INIT_HAPTIC) && SDL_InitSubSystem(SDL_INIT_HAPTIC) < 0) return false;

    if (haptic_ && SDL_HapticIndex(haptic_) != -1) return true;

    if (haptic_) {
        SDL_HapticClose(haptic_);
        haptic_ = nullptr;
    }

    haptic_ = SDL_HapticOpenFromJoystick(joyhandle_);
    vibration_ = Vibration{};
    return haptic_ != nullptr;
}

bool Pad::isVibrationSupported() {
    if (!checkCreateHaptic()) return false;
    unsigned int features = SDL_HapticQuery(haptic_);
    if ((features & SDL_HAPTIC_LEFTRIGHT) != 0) return true;
    if (isGamepad() && (features & SDL_HAPTIC_CUSTOM) != 0) return true;
    if ((features & SDL_HAPTIC_SINE) != 0) return true;
    return false;
}

bool Pad::runVibrationEffect() {
    if (vibration_.id != -1) {
        if (SDL_HapticUpdateEffect(haptic_, vibration_.id, &vibration_.effect) == 0) {
            if (SDL_HapticRunEffect(haptic_, vibration_.id, 1) == 0) return true;
        }
        SDL_HapticDestroyEffect(haptic_, vibration_.id);
        vibration_.id = -1;
    }

    vibration_.id = SDL_HapticNewEffect(haptic_, &vibration_.effect);
    return vibration_.id != -1 && SDL_HapticRunEffect(haptic_, vibration_.id, 1) == 0;
}

bool Pad::setVibration(float left, float right, float duration) {
    left = std::min(std::max(left, 0.0f), 1.0f);
    right = std::min(std::max(right, 0.0f), 1.0f);

    if (left == 0.0f && right == 0.0f) return setVibration();
    if (!checkCreateHaptic()) return false;

    Uint32 length = SDL_HAPTIC_INFINITY;
    if (duration >= 0.0f) {
        float maxduration = static_cast<float>(std::numeric_limits<Uint32>::max()) / 1000.0f;
        length = static_cast<Uint32>(std::min(duration, maxduration) * 1000.0f);
    }

    bool success = false;
    unsigned int features = SDL_HapticQuery(haptic_);
    int axes = SDL_HapticNumAxes(haptic_);

    if ((features & SDL_HAPTIC_LEFTRIGHT) != 0) {
        memset(&vibration_.effect, 0, sizeof(SDL_HapticEffect));
        vibration_.effect.type = SDL_HAPTIC_LEFTRIGHT;
        vibration_.effect.leftright.length = length;
        vibration_.effect.leftright.large_magnitude = static_cast<Uint16>(left * 0xFFFF);
        vibration_.effect.leftright.small_magnitude = static_cast<Uint16>(right * 0xFFFF);
        success = runVibrationEffect();
    }

    if (!success && isGamepad() && (features & SDL_HAPTIC_CUSTOM) && axes == 2) {
        vibration_.data[0] = vibration_.data[2] = static_cast<Uint16>(left * 0x7FFF);
        vibration_.data[1] = vibration_.data[3] = static_cast<Uint16>(right * 0x7FFF);
        memset(&vibration_.effect, 0, sizeof(SDL_HapticEffect));
        vibration_.effect.type = SDL_HAPTIC_CUSTOM;
        vibration_.effect.custom.length = length;
        vibration_.effect.custom.channels = 2;
        vibration_.effect.custom.period = 10;
        vibration_.effect.custom.samples = 2;
        vibration_.effect.custom.data = vibration_.data;
        success = runVibrationEffect();
    }

    if (!success && (features & SDL_HAPTIC_SINE) != 0) {
        memset(&vibration_.effect, 0, sizeof(SDL_HapticEffect));
        vibration_.effect.type = SDL_HAPTIC_SINE;
        vibration_.effect.periodic.length = length;
        vibration_.effect.periodic.period = 10;
        float strength = std::max(left, right);
        vibration_.effect.periodic.magnitude = static_cast<Sint16>(strength * 0x7FFF);
        success = runVibrationEffect();
    }

    if (success) {
        vibration_.left = left;
        vibration_.right = right;
        vibration_.endtime =
            (length == SDL_HAPTIC_INFINITY) ? SDL_HAPTIC_INFINITY : (SDL_GetTicks() + length);
    } else {
        vibration_.left = vibration_.right = 0.0f;
        vibration_.endtime = SDL_HAPTIC_INFINITY;
    }
    return success;
}

bool Pad::setVibration() {
    bool success = true;
    if (SDL_WasInit(SDL_INIT_HAPTIC) && haptic_ && SDL_HapticIndex(haptic_) != -1)
        success = (SDL_HapticStopEffect(haptic_, vibration_.id) == 0);
    if (success) vibration_.left = vibration_.right = 0.0f;
    return success;
}

void Pad::getVibration(float& left, float& right) {
    if (vibration_.endtime != SDL_HAPTIC_INFINITY) {
        if (SDL_TICKS_PASSED(SDL_GetTicks(), vibration_.endtime)) {
            setVibration();
            vibration_.endtime = SDL_HAPTIC_INFINITY;
        }
    }

    int id = vibration_.id;
    if (!haptic_ || id == -1 || SDL_HapticGetEffectStatus(haptic_, id) != 1)
        vibration_.left = vibration_.right = 0.0f;

    left = vibration_.left;
    right = vibration_.right;
}

}  // namespace eve::joystick::sdl
