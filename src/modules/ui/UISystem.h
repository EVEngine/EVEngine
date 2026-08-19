#pragma once

#include "ui/UIHost.h"

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
    UIHost *host = nullptr;
    std::string hostName;
    std::string nodeId;
    std::string kind;  // "click" | "toggle" | "value" | "text"
    uint32_t handlerIndex = 0;
    bool toggleValue = false;
    float floatValue = 0.f;
    std::string textValue;
};

/** One script-facing click: "hostName/nodeId". */
struct UIClick {
    std::string hostName;
    std::string nodeId;
};

/** Value/text/toggle change for script: "hostName/nodeId". */
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
 * (owned by Graphics) and the input routed from the widget rect (local mouse,
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
    /** Backend used for texture size lookups while rendering (set by UI). */
    static void setBackend(UIBackend *backend);
    static UIBackend *backend();
    static const UIStats &stats();
    /** Viewport state for "hostName/nodeId"; nullptr if no such viewport. */
    static ViewportState *viewportState(const std::string &hostName, const std::string &nodeId);
    /** Create/refresh the offscreen canvas for a viewport key. */
    static ViewportState *ensureViewport(const std::string &key, int w, int h);

    /** Walk all UIHost (+ subclasses) via ECS View. */
    static void render();

    static std::vector<UIEvent> &pendingEvents();
    static void dispatchEvents();

    /** Lookup by Meta.name across the UIHost View. */
    static UIHost *findHost(const std::string &name);
    /** First host with Meta.ownerId == ownerId, or nullptr. */
    static UIHost *findHostByOwner(uint32_t ownerId);

    static std::vector<UIClick> &clickQueue();
    static std::vector<UIChange> &changeQueue();
    /** Pop next click as "name/node"; empty if none. */
    static std::string consumeClick();
    /** Pop next click for a specific host; returns node id only (or ""). */
    static std::string consumeClickFor(const std::string &hostName);
    /** Pop next change as "name/node"; empty if none. */
    static std::string consumeChange();
};

}  // namespace eve::ui
