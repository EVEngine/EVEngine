// Publishes IInputInjection via SDL_PushEvent so Keyboard/Mouse isDown state
// matches polled events after the next PlatformEvent::pump().

#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/InputInjection.h"
#include "common/Result.h"
#include "common/Status.h"

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

namespace eve::platform_event::sdl {
namespace {

Result<void> pushEvent(SDL_Event& event) {
    if (SDL_PushEvent(&event) != 1) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, std::string("SDL_PushEvent failed: ") + SDL_GetError(), "input"));
    }
    return Result<void>::success();
}

class SdlInputInjection final : public eve::IInputInjection {
public:
    [[nodiscard]] Result<void> injectKey(std::string_view key, bool down, bool repeat) override {
        if (key.empty()) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "key name is required", "key"));
        }
        const std::string keyName(key);
        const SDL_Keycode keycode = SDL_GetKeyFromName(keyName.c_str());
        if (keycode == SDLK_UNKNOWN) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "unknown SDL key name '" + keyName + "'", "key"));
        }
        SDL_Event event{};
        event.type           = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.type       = event.type;
        event.key.state      = down ? SDL_PRESSED : SDL_RELEASED;
        event.key.repeat     = (down && repeat) ? 1 : 0;
        event.key.keysym.sym = keycode;
        event.key.keysym.scancode = SDL_GetScancodeFromKey(keycode);
        event.key.keysym.mod = KMOD_NONE;
        return pushEvent(event);
    }

    [[nodiscard]] Result<void> injectMouseButton(int button, bool down, float x, float y) override {
        if (button < 1 || button > 5) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "mouse button must be 1..5 (SDL button index)", "button"));
        }
        SDL_Event event{};
        event.type           = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.type    = event.type;
        event.button.button  = static_cast<Uint8>(button);
        event.button.state   = down ? SDL_PRESSED : SDL_RELEASED;
        event.button.clicks  = 1;
        event.button.x       = static_cast<Sint32>(x);
        event.button.y       = static_cast<Sint32>(y);
        return pushEvent(event);
    }

    [[nodiscard]] Result<void> injectMouseMove(float x, float y, float dx, float dy) override {
        SDL_Event event{};
        event.type       = SDL_MOUSEMOTION;
        event.motion.type = SDL_MOUSEMOTION;
        event.motion.x   = static_cast<Sint32>(x);
        event.motion.y   = static_cast<Sint32>(y);
        event.motion.xrel = static_cast<Sint32>(dx);
        event.motion.yrel = static_cast<Sint32>(dy);
        return pushEvent(event);
    }

    [[nodiscard]] Result<void> injectWheel(float x, float y) override {
        SDL_Event event{};
        event.type             = SDL_MOUSEWHEEL;
        event.wheel.type       = SDL_MOUSEWHEEL;
        event.wheel.x          = static_cast<Sint32>(x);
        event.wheel.y          = static_cast<Sint32>(y);
        event.wheel.direction  = SDL_MOUSEWHEEL_NORMAL;
#if SDL_VERSION_ATLEAST(2, 0, 18)
        event.wheel.preciseX   = x;
        event.wheel.preciseY   = y;
#endif
        return pushEvent(event);
    }

    [[nodiscard]] Result<void> injectText(std::string_view text) override {
        if (text.empty()) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "text payload is required", "text"));
        }
        SDL_Event event{};
        event.type = SDL_TEXTINPUT;
        event.text.type = SDL_TEXTINPUT;
        const std::size_t n =
            std::min(text.size(), static_cast<std::size_t>(SDL_TEXTINPUTEVENT_TEXT_SIZE - 1));
        std::memcpy(event.text.text, text.data(), n);
        event.text.text[n] = '\0';
        return pushEvent(event);
    }
};

struct Register {
    Register() {
        static SdlInputInjection injection;
        eve::cap::provide<eve::IInputInjection>(&injection);
    }
} g_register;

}  // namespace
}  // namespace eve::platform_event::sdl
