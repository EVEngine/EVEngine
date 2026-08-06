#include "keyboard/sdl/Keyboard.h"

#include "window/Window.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_keyboard.h>

namespace eve::keyboard::sdl {

Keyboard::Keyboard() {
    // Video subsystem owns the keyboard state / text input on most backends.
    SDL_InitSubSystem(SDL_INIT_VIDEO);
}

Keyboard::~Keyboard() {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void Keyboard::setKeyRepeat(bool enable) {
    keyRepeat_ = enable;
}

bool Keyboard::hasKeyRepeat() const {
    return keyRepeat_;
}

bool Keyboard::isDown(const std::string& key) const {
    return isDown(std::vector<std::string>{key});
}

bool Keyboard::isDown(const std::vector<std::string>& keys) const {
    const Uint8* state = SDL_GetKeyboardState(nullptr);
    if (!state) return false;

    for (const auto& name : keys) {
        SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
        if (key == SDLK_UNKNOWN) continue;
        SDL_Scancode scancode = SDL_GetScancodeFromKey(key);
        if (scancode != SDL_SCANCODE_UNKNOWN && state[scancode]) return true;
    }
    return false;
}

bool Keyboard::isScancodeDown(const std::string& scancode) const {
    return isScancodeDown(std::vector<std::string>{scancode});
}

bool Keyboard::isScancodeDown(const std::vector<std::string>& scancodes) const {
    const Uint8* state = SDL_GetKeyboardState(nullptr);
    if (!state) return false;

    for (const auto& name : scancodes) {
        SDL_Scancode scancode = SDL_GetScancodeFromName(name.c_str());
        if (scancode != SDL_SCANCODE_UNKNOWN && state[scancode]) return true;
    }
    return false;
}

std::string Keyboard::getKeyFromScancode(const std::string& scancode) const {
    SDL_Scancode sdlScancode = SDL_GetScancodeFromName(scancode.c_str());
    if (sdlScancode == SDL_SCANCODE_UNKNOWN) return {};
    SDL_Keycode key = SDL_GetKeyFromScancode(sdlScancode);
    if (key == SDLK_UNKNOWN) return {};
    const char* name = SDL_GetKeyName(key);
    return name ? name : std::string{};
}

std::string Keyboard::getScancodeFromKey(const std::string& key) const {
    SDL_Keycode sdlKey = SDL_GetKeyFromName(key.c_str());
    if (sdlKey == SDLK_UNKNOWN) return {};
    SDL_Scancode scancode = SDL_GetScancodeFromKey(sdlKey);
    if (scancode == SDL_SCANCODE_UNKNOWN) return {};
    const char* name = SDL_GetScancodeName(scancode);
    return name ? name : std::string{};
}

void Keyboard::setTextInput(bool enable) {
    if (enable)
        SDL_StartTextInput();
    else
        SDL_StopTextInput();
}

void Keyboard::setTextInput(bool enable, double x, double y, double w, double h) {
    // SDL_SetTextInputRect expects window coords; API takes pixels.
    auto* window = getModInst(window, Window);
    if (window) {
        window->DPIToWindowCoords(&x, &y);
        window->DPIToWindowCoords(&w, &h);
    }

    SDL_Rect rect = {static_cast<int>(x), static_cast<int>(y), static_cast<int>(w),
                     static_cast<int>(h)};
    SDL_SetTextInputRect(&rect);
    setTextInput(enable);
}

bool Keyboard::hasTextInput() const {
    return SDL_IsTextInputActive() != SDL_FALSE;
}

bool Keyboard::hasScreenKeyboard() const {
    return SDL_HasScreenKeyboardSupport() != SDL_FALSE;
}

}  // namespace eve::keyboard::sdl
