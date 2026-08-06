#pragma once

#include <SDL2/SDL.h>
#include <memory>

namespace eve::graphics {
class Graphics;
}

namespace eve::ui {

/**
 * Platform / renderer backend for declarative UI.
 * Concrete implementations live under ui/<backend>/ (e.g. ui/imgui).
 */
class UIBackend {
public:
    virtual ~UIBackend() = default;

    virtual bool init(SDL_Window *window, eve::graphics::Graphics *gfx) = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;

    virtual void processEvent(const SDL_Event *event) = 0;
    virtual void newFrame() = 0;

    virtual bool wantCaptureMouse() const { return false; }
    virtual bool wantCaptureKeyboard() const { return false; }
};

/** Default backend: Dear ImGui + SDL + Vulkan (see ui/imgui/). */
std::unique_ptr<UIBackend> createImGuiBackend();

}  // namespace eve::ui
