#pragma once

#include "common/BorrowedRef.h"
#include "ui/UIHost.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace eve::graphics {
class Canvas;
}

namespace eve::ui {
class UIBackend;
}

namespace eve::ui {

struct UIEvent {
    UIHostHandle host{};
    std::string hostName;
    std::string nodeId;
    int nodeIndex = -1;
    std::string kind;  // "click" | "toggle" | "value" | "text"
    uint32_t handlerIndex = 0;
    bool toggleValue = false;
    float floatValue = 0.f;
    std::string textValue;
};

/** @brief One script-facing click: "hostName/nodeId". */
struct UIClick {
    std::string hostName;
    std::string nodeId;
};

/** @brief Value/text/toggle change for script: "hostName/nodeId". */
struct UIChange {
    std::string hostName;
    std::string nodeId;
    std::string kind;
};

/** Per-frame UI statistics (P2-3 profiler counters). */
struct UIStats {
    int hostCount = 0;
    int nodeCount = 0;
    double measureMs = 0.0;
    double walkMs = 0.0;
};

/**
 * Per-frame state of one embedded Viewport widget: its offscreen render target
 * owned by Graphics, and the input routed from the widget rect (local mouse,
 * drag delta, wheel). Filled during UISystem::render().
 */
struct ViewportState {
    std::string key;                 // "hostName/nodeId"
    graphics::Canvas *canvas = nullptr;
    uint64_t textureId = 0;          // registered via UIBackend::registerTexture
    int width = 0;
    int height = 0;
    bool hovered = false;
    bool active = false;             // mouse button held inside the viewport
    float mouseX = 0.f;              // widget-local
    float mouseY = 0.f;
    float dragDX = 0.f;
    float dragDY = 0.f;
    float wheel = 0.f;
};

class UISystem {
public:
    /** @brief Sets the borrowed backend used for the current UI/render lifetime. */
    static void setBackend(UIBackend &backend);
    /** @brief Clears the backend association before the backend is shut down. */
    static void clearBackend() noexcept;
    /**
     * @brief Returns the configured backend for the current synchronous operation.
     * @return A borrowed reference when configured, or empty when unavailable.
     * @ownership UISystem does not own the backend.
     * @lifetime The reference is valid until backend replacement or teardown; do not retain it.
     * @thread Call on the UI/render thread.
     */
    [[nodiscard]] static eve::OptionalRef<UIBackend> backend();
    static const UIStats &stats();
    /**
     * @brief Returns viewport state for a host/node key when it exists.
     * @return A borrowed reference for this synchronous operation, or empty when absent.
     * @ownership UISystem owns the state.
     * @lifetime Valid until viewport removal, refresh, or render-state reset; do not retain it.
     * @thread Call on the UI/render thread.
     */
    [[nodiscard]] static eve::OptionalRef<ViewportState> viewportState(const std::string &hostName,
                                                                       const std::string &nodeId);
    /**
     * @brief Creates or refreshes offscreen state for a viewport key.
     * @return A borrowed state reference, or a structured failure for invalid dimensions/key.
     * @ownership UISystem owns the state and Graphics owns its Canvas.
     * @lifetime Valid until viewport removal, refresh, or render-state reset; do not retain it.
     * @thread Call on the UI/render thread.
     */
    [[nodiscard]] static eve::ResultRef<ViewportState> ensureViewport(const std::string &key, int w, int h);

    /** @brief Walk all UIHost (+ subclasses) via ECS View. */
    static void render();

    static std::vector<UIEvent> &pendingEvents();
    static void dispatchEvents();

    /**
     * @brief Lookup by Meta.name across the UIHost View.
     * @return Borrowed nullable host owned by the UI ECS world.
     * @ownership UISystem does not own the host; callers must not delete it.
     * @lifetime Valid until host destruction or ECS structural mutation.
     * @thread Call on the UI thread.
     * @reentrancy The lookup invokes no callbacks and is invalid across ECS mutation.
     */
    [[nodiscard]] static UIHostHandle findHost(const std::string &name);
    /**
     * @brief Find the first host with Meta.ownerId equal to ownerId, or null.
     * @return Borrowed nullable host owned by the UI ECS world.
     * @ownership UISystem does not own the host; callers must not delete it.
     * @lifetime Valid until host destruction or ECS structural mutation.
     * @thread Call on the UI thread.
     * @reentrancy The lookup invokes no callbacks and is invalid across ECS mutation.
     */
    [[nodiscard]] static UIHostHandle findHostByOwner(uint32_t ownerId);

    static std::vector<UIClick> &clickQueue();
    static std::vector<UIChange> &changeQueue();
    /** @brief Pop next click as "name/node"; empty if none. */
    static std::string consumeClick();
    /** @brief Pop next click for a specific host; returns node id only (or ""). */
    static std::string consumeClickFor(const std::string &hostName);
    /** @brief Pop next change as "name/node"; empty if none. */
    static std::string consumeChange();
};

}  // namespace eve::ui
