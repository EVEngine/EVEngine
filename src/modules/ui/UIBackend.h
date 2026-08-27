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
    [[nodiscard("retain the UI texture registration id or explicitly handle failure")]]
    virtual uint64_t registerTexture(graphics::Texture * /*tex*/) {
        return 0;
    }
    virtual void unregisterTexture(uint64_t /*id*/) {}
    /** Texture pixel size for a registered id (used by nine-patch UV math). */
    virtual bool textureSize(uint64_t /*id*/, int * /*w*/, int * /*h*/) const { return false; }
    /**
     * @brief Returns an opaque backend draw handle for a registered texture.
     * @return Borrowed nullable handle owned by the backend; null means unknown or unsupported.
     * @ownership The backend owns the handle and callers must not free or cast it beyond the backend contract.
     * @lifetime Valid until unregisterTexture(), backend shutdown, or device reset.
     * @thread Call on the UI/render thread.
     * @reentrancy The lookup invokes no callbacks and is invalid across backend mutation.
     */
    virtual void *textureHandle(uint64_t /*id*/) const { return nullptr; }

    /**
     * @brief Whether textures are composited by the backend after its normal UI pass.
     *
     * Older renderer integrations may not support a texture handle per draw command.
     * Declarative widgets keep their normal layout/input behavior and enqueue their
     * textured rectangles through queueTextureDraw instead.
     * @return True when declarative widgets must use queueTextureDraw().
     */
    virtual bool usesQueuedTextureDraws() const { return false; }
    /**
     * @brief Queue one textured rectangle in the current UI coordinate space.
     * @param id Opaque id returned by registerTexture().
     * @param x Left edge in UI coordinates.
     * @param y Top edge in UI coordinates.
     * @param w Rectangle width.
     * @param h Rectangle height.
     * @param u0 Left texture coordinate.
     * @param v0 Top texture coordinate.
     * @param u1 Right texture coordinate.
     * @param v1 Bottom texture coordinate.
     * @param r Red tint multiplier.
     * @param g Green tint multiplier.
     * @param b Blue tint multiplier.
     * @param a Alpha tint multiplier.
     * @param opaque Whether composition must replace destination alpha.
     */
    virtual void queueTextureDraw(uint64_t /*id*/, float /*x*/, float /*y*/, float /*w*/, float /*h*/,
                                  float /*u0*/, float /*v0*/, float /*u1*/, float /*v1*/, float /*r*/,
                                  float /*g*/, float /*b*/, float /*a*/, bool /*opaque*/) {}
};

/** @brief Default backend: Dear ImGui + SDL + Vulkan (see ui/imgui/). */
std::unique_ptr<UIBackend> createImGuiBackend();

}  // namespace eve::ui
