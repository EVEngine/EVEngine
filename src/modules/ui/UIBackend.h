#pragma once

#include <SDL2/SDL.h>
#include <cstdint>
#include <memory>

namespace eve::graphics {
class Graphics;
class Texture;
}

namespace eve::ui {

/**
 * @brief Platform / renderer backend for declarative UI.
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

    /** @brief Scale fonts + ImGui style metrics (1 = default desktop). */
    virtual void setScale(float /*scale*/) {}
    virtual float getScale() const { return 1.f; }

    /**
     * Register an engine texture for UI drawing. Returns an opaque id usable
     * as UINode::textureId (0 = unsupported / failure). The texture must stay
     * alive while registered.
     */
    virtual uint64_t registerTexture(graphics::Texture * /*tex*/) { return 0; }
    virtual void unregisterTexture(uint64_t /*id*/) {}
    /** Texture pixel size for a registered id (used by nine-patch UV math). */
    virtual bool textureSize(uint64_t /*id*/, int * /*w*/, int * /*h*/) const { return false; }
    /** Backend draw handle (ImTextureID) for a registered id; null if unknown. */
    virtual void *textureHandle(uint64_t /*id*/) const { return nullptr; }
};

/** @brief Default backend: Dear ImGui + SDL + Vulkan (see ui/imgui/). */
std::unique_ptr<UIBackend> createImGuiBackend();

}  // namespace eve::ui
