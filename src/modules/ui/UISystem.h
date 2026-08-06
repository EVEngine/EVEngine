#pragma once

#include "ui/UIHost.h"

#include <string>
#include <vector>

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

class UISystem {
public:
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
