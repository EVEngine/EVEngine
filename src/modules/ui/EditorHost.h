#pragma once

#include "common/Export.h"

#include <memory>
#include <string>

namespace ssq {
class VM;
}

namespace eve::ui {

/**
 * Headless MCP editor host (MVVM).
 *
 * The AI drives an EVEngine process through MCP: the View is a JSON widget
 * tree (applyEditor), the ViewModel is a Squirrel table (registerVM) that the
 * host binds to the View each frame (two-way `bind`, one-way commands /
 * `onChange` / `bind:options` / `bind:label` / `bind:visible` / `bind:enabled`).
 * Model data lives in the engine (Heightmap / Material / Scene) and is only
 * touched by the ViewModel script.
 *
 * Window lifecycle: the host OS window is created lazily on the first
 * openWindow / applyEditor; `frame()` pumps events, runs `eve_host_update(dt)`
 * and `eve_host_render()` hooks, then presents. `renderImGui()` must be called
 * between `ui.beginFrameAndRender()` and `gfx.present()`.
 *
 * Desktop-only full implementation; Emscripten builds get a stub so EVUI stays
 * linkable without Poco / DevTools. The header deliberately contains no
 * imgui.h (engine TUs include this header).
 */
class EVENGINE_API EditorHost {
public:
    static EditorHost& instance();

    EditorHost(const EditorHost&)            = delete;
    EditorHost& operator=(const EditorHost&) = delete;
    ~EditorHost();

    /** Attach to the host VM. gameRoot is the project dir for editors/. */
    void start(ssq::VM& vm, const std::string& gameRoot = {}, bool allowWindow = true);
    void stop();
    bool running() const { return running_; }
    bool windowAllowed() const { return allowWindow_; }
    const std::string& gameRoot() const { return gameRoot_; }

    // ---- OS window -------------------------------------------------------
    std::string openWindow(const std::string& title, int width, int height);
    std::string closeWindow();
    bool        windowOpen() const;
    std::string windowState() const;

    // ---- editors (View) --------------------------------------------------
    /** JSON object/string: {id,title,vm,x,y,width,height,theme,children[]}. */
    std::string applyEditor(const std::string& json);
    std::string removeEditor(const std::string& id);
    std::string listEditors() const;
    /** Full state JSON: editors + values + (non-destructive) events. */
    std::string editorState(const std::string& id) const;
    /** Pull bound ViewModel values into widget state (per-frame + on read). */
    void syncBindings();
    std::string setEditorValue(const std::string& editorId, const std::string& widgetId,
                               const std::string& jsonValue);
    /** Read and clear pending interaction events for one editor ("" = all). */
    std::string consumeEvents(const std::string& editorId);
    /** Last-frame widget rect JSON (for script drawing inside viewports). */
    std::string widgetRect(const std::string& editorId, const std::string& widgetId) const;

    // ---- ViewModel (Squirrel tables) --------------------------------------
    /** Compile Squirrel source in the host VM, then register table `name`. */
    std::string registerVM(const std::string& name, const std::string& source);
    std::string unregisterVM(const std::string& name);

    // ---- persistence -------------------------------------------------------
    /** Write editors/<id>.editor.json + editors/<id>.vm.nut under gameRoot. */
    std::string saveEditor(const std::string& id);
    /** Remove an editor from the session (files stay on disk). */
    std::string unloadEditor(const std::string& id);
    /** Load editors/<id>.editor.json (+ matching .vm.nut) on host startup. */
    void loadEditorsFromDisk();

    // ---- misc --------------------------------------------------------------
    std::string runScript(const std::string& source);
    std::string capture(const std::string& path);
    std::string status() const;
    void        requestExit() { exitRequested_ = true; }
    bool        exitRequested() const { return exitRequested_; }

    /** Pump events + update/render hooks + present (no-op without window). */
    void frame();
    /** Draw editor ImGui windows; call between beginFrameAndRender/present. */
    void renderImGui();
    /** Expose `eve.host` table (registerVM/unregisterVM/widgetRect/...). */
    void exposeScriptApi(ssq::VM& vm);

    struct Impl;  // pimpl: full definition lives in EditorHost.cpp

private:
    EditorHost();
    std::unique_ptr<Impl> impl_;
    bool                  running_       = false;
    bool                  allowWindow_   = true;
    bool                  exitRequested_ = false;
    std::string           gameRoot_;
};

}  // namespace eve::ui
