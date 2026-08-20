#pragma once

// Capability interface: declarative editor host (ui module). cmdline and
// devtools consume this so neither has to depend on the ui module.

#include "common/Export.h"

#include <string>

namespace ssq {
class VM;
}

namespace eve {

/** @brief Declarative editor-host control (provided by the ui module). */
class EVENGINE_API IEditorHost {
public:
    static constexpr const char* capabilityName = "IEditorHost";

    virtual ~IEditorHost() = default;

    virtual std::string status() const = 0;
    virtual std::string openWindow(const std::string& title, int width, int height) = 0;
    virtual std::string closeWindow() = 0;
    virtual std::string windowState() const = 0;
    virtual std::string applyEditor(const std::string& json) = 0;
    virtual std::string removeEditor(const std::string& id) = 0;
    virtual std::string listEditors() const = 0;
    virtual std::string editorState(const std::string& id) const = 0;
    virtual std::string setEditorValue(const std::string& editorId, const std::string& widgetId,
                                       const std::string& value) = 0;
    virtual std::string consumeEvents(const std::string& editorId) = 0;
    virtual std::string widgetRect(const std::string& editorId,
                                   const std::string& widgetId) const = 0;
    virtual std::string registerVM(const std::string& name, const std::string& source) = 0;
    virtual std::string unregisterVM(const std::string& name) = 0;
    virtual std::string saveEditor(const std::string& id) = 0;
    virtual std::string unloadEditor(const std::string& id) = 0;
    virtual std::string capture(const std::string& path) = 0;
    virtual std::string runScript(const std::string& source) = 0;
    virtual void requestExit() = 0;
    /** @brief True after requestExit() (main loop exit condition). */
    virtual bool exitRequested() = 0;
    /** @brief Pump the host's per-frame update (frame loop). */
    virtual void frame() = 0;
    /** @brief Shut the host down (stops window + workers). */
    virtual void stop() = 0;

    /** @brief Start the host against a Squirrel VM (cmdline integration). */
    virtual void start(ssq::VM& vm, const std::string& gameRoot, bool allowWindow) = 0;
    /** @brief Expose the host's script API on the VM. */
    virtual void exposeScriptApi(ssq::VM& vm) = 0;
};

}  // namespace eve
