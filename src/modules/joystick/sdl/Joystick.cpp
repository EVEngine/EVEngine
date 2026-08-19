#include "joystick/sdl/Joystick.h"

#include "common/Exception.h"
#include "common/StartupTiming.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace eve::joystick::sdl {

Joystick::Joystick() {
    StartupStage stage("joystick: SDL subsystem init + device enumeration");
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0)
        throw eve::Exception("Could not initialize SDL joystick subsystem (%s)", SDL_GetError());

    for (int i = 0; i < SDL_NumJoysticks(); i++) addJoystick(i);

    SDL_JoystickEventState(SDL_ENABLE);
    SDL_GameControllerEventState(SDL_ENABLE);
}

Joystick::~Joystick() {
    for (auto* stick : joysticks_) {
        stick->close();
        delete stick;
    }
    joysticks_.clear();
    activeSticks_.clear();

    if (SDL_WasInit(SDL_INIT_HAPTIC) != 0) SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
}

Pad* Joystick::getJoystick(int joyindex) {
    if (joyindex < 0 || static_cast<size_t>(joyindex) >= activeSticks_.size()) return nullptr;
    return activeSticks_[static_cast<size_t>(joyindex)];
}

int Joystick::getIndex(const eve::joystick::Pad* pad) {
    for (int i = 0; i < static_cast<int>(activeSticks_.size()); i++) {
        if (activeSticks_[static_cast<size_t>(i)] == pad) return i;
    }
    return -1;
}

int Joystick::getJoystickCount() const {
    return static_cast<int>(activeSticks_.size());
}

Pad* Joystick::getJoystickFromID(int instanceid) {
    for (auto* stick : activeSticks_) {
        if (stick->getInstanceID() == instanceid) return stick;
    }
    return nullptr;
}

Pad* Joystick::addJoystick(int deviceindex) {
    if (deviceindex < 0 || deviceindex >= SDL_NumJoysticks()) return nullptr;

    std::string guidstr = getDeviceGUID(deviceindex);
    Pad* pad = nullptr;
    bool reused = false;

    for (auto* stick : joysticks_) {
        if (!stick->isConnected() && stick->getGUID() == guidstr) {
            pad = stick;
            reused = true;
            break;
        }
    }

    if (!pad) {
        pad = new Pad(static_cast<int>(joysticks_.size()));
        joysticks_.push_back(pad);
    }

    removeJoystick(pad);

    if (!pad->open(deviceindex)) return nullptr;

    for (auto* active : activeSticks_) {
        if (pad->getHandle() == active->getHandle()) {
            pad->close();
            if (!reused) {
                joysticks_.remove(pad);
                delete pad;
            }
            return active;
        }
    }

    if (pad->isGamepad()) recentGamepadGUIDs_[pad->getGUID()] = true;

    activeSticks_.push_back(pad);
    return pad;
}

void Joystick::removeJoystick(eve::joystick::Pad* pad) {
    if (!pad) return;
    auto it = std::find(activeSticks_.begin(), activeSticks_.end(), pad);
    if (it != activeSticks_.end()) {
        (*it)->close();
        activeSticks_.erase(it);
    }
}

void Joystick::checkGamepads(const std::string& guid) const {
    for (int d_index = 0; d_index < SDL_NumJoysticks(); d_index++) {
        if (!SDL_IsGameController(d_index)) continue;
        if (guid.compare(getDeviceGUID(d_index)) != 0) continue;

        for (auto* stick : activeSticks_) {
            if (guid.compare(stick->getGUID()) != 0) continue;

            SDL_GameController* controller = SDL_GameControllerOpen(d_index);
            if (!controller) continue;

            SDL_Joystick* sdlstick = SDL_GameControllerGetJoystick(controller);
            bool open_gamepad = (sdlstick == static_cast<SDL_Joystick*>(stick->getHandle()));
            SDL_GameControllerClose(controller);

            if (open_gamepad) stick->openGamepad(d_index);
        }
    }
}

std::string Joystick::getDeviceGUID(int deviceindex) const {
    if (deviceindex < 0 || deviceindex >= SDL_NumJoysticks()) return "";

    char guidstr[33] = {'\0'};
    SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(deviceindex);
    SDL_JoystickGetGUIDString(guid, guidstr, sizeof(guidstr));
    return guidstr;
}

void Joystick::loadGamepadMappings(const std::string& mappings) {
    std::stringstream ss(mappings);
    std::string mapping;
    bool success = false;

    while (std::getline(ss, mapping)) {
        if (mapping.empty()) continue;
        if (mapping[0] == '#') continue;

        size_t pstartpos = mapping.find("platform:");
        if (pstartpos != std::string::npos) {
            pstartpos += strlen("platform:");
            size_t pendpos = mapping.find_first_of(',', pstartpos);
            std::string platform = mapping.substr(pstartpos, pendpos - pstartpos);
            if (platform.compare(SDL_GetPlatform()) != 0) {
                success = true;
                continue;
            }
            pstartpos -= strlen("platform:");
            mapping.erase(pstartpos, pendpos - pstartpos + 1);
        }

        if (SDL_GameControllerAddMapping(mapping.c_str()) != -1) {
            success = true;
            std::string guid = mapping.substr(0, mapping.find_first_of(','));
            recentGamepadGUIDs_[guid] = true;
            checkGamepads(guid);
        }
    }

    if (!success && !mappings.empty()) throw eve::Exception("Invalid gamepad mappings.");
}

std::string Joystick::getGamepadMappingString(const std::string& guid) const {
    SDL_JoystickGUID sdlguid = SDL_JoystickGetGUIDFromString(guid.c_str());
    char* sdlmapping = SDL_GameControllerMappingForGUID(sdlguid);
    if (!sdlmapping) return "";

    std::string mapping(sdlmapping);
    SDL_free(sdlmapping);

    if (mapping.find_last_of(',') != mapping.length() - 1) mapping += ",";
    mapping += "platform:" + std::string(SDL_GetPlatform());
    return mapping;
}

std::string Joystick::saveGamepadMappings() {
    std::string mappings;
    for (const auto& g : recentGamepadGUIDs_) {
        SDL_JoystickGUID sdlguid = SDL_JoystickGetGUIDFromString(g.first.c_str());
        char* sdlmapping = SDL_GameControllerMappingForGUID(sdlguid);
        if (!sdlmapping) continue;

        std::string mapping = sdlmapping;
        SDL_free(sdlmapping);

        if (mapping.find_last_of(',') != mapping.length() - 1) mapping += ",";
        mapping += "platform:" + std::string(SDL_GetPlatform()) + ",\n";
        mappings += mapping;
    }
    return mappings;
}

}  // namespace eve::joystick::sdl
