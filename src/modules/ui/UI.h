#pragma once

#include "common/Module.h"
#include "ui/UIBackend.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"

#include <SDL2/SDL.h>
#include <simplesquirrel/simplesquirrel.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::graphics {
class Canvas;
class Graphics;
class Texture;
}

namespace eve::ui {

class DatabasePanel;
class EditorShell;
class Inspector;
class ScenePanel;
struct UIEvent;

/**
 * @brief Declarative UI module (eve.UI).
 *
 * ECS: named UIHost panels. Builder + Component.build (C++/script) + list/when/theme.
 */
class UI : public Module {
public:
    Module_REG(UI);
    UI();
    ~UI() override;

    /** @brief Creates the platform UI backend (ImGui); true on success. */
    bool initBackend();
    /** @brief Destroys the platform UI backend. */
    void shutdownBackend();
    /** @brief True once the backend exists. */
    bool isBackendReady() const;

    /** @brief Feeds an SDL event into the UI backend (before window/game handling). */
    void processEvent(const SDL_Event *event);
    /** @brief Builds and presents the current frame's UI. */
    void beginFrameAndRender();
    /** @brief Dispatches queued widget callbacks (click/toggle/value/text). */
    void dispatchEvents();

    /** @brief True when the UI wants to capture mouse input this frame. */
    bool wantCaptureMouse() const;
    /** @brief True when the UI wants to capture keyboard input this frame. */
    bool wantCaptureKeyboard() const;

    /** @brief Creates/replaces a named host from a WidgetDesc tree and selects it. */
    UIHost *mountAs(const std::string &name, WidgetDesc root);
    /** @brief Mounts the tree as an auto-named host and selects it. */
    UIHost *mount(WidgetDesc root);
    /** @brief Replaces the selected host's tree. */
    UIHost *remount(WidgetDesc root);
    /** @brief Remount with key reconcile (props-only when structure matches). */
    UIHost *remountReconcile(WidgetDesc root);
    /** @brief Creates/replaces a named host (does not select it). */
    UIHost *remountAs(const std::string &name, WidgetDesc root);

    /** @brief Selects a named host; false when it does not exist. */
    bool select(const std::string &name);
    /** @brief Finds a host by name, or nullptr. */
    UIHost *findHost(const std::string &name) const;
    /** @brief Finds the host bound to an owner id, or nullptr. */
    UIHost *findHostByOwner(uint32_t ownerId) const;
    /** @brief Currently selected host, or nullptr. */
    UIHost *current() const { return selected_; }
    /** @brief Binds the selected host to a UI/scene owner id. */
    void bindOwner(uint32_t ownerId);

    /** @brief Imperative builder: open a new build pass (see beginWindow etc.). */
    void beginBuild();
    /** @brief Opens a window for the current build pass. */
    void beginWindow(const std::string &title, const std::string &id = "root");
    /** @brief Opens a group container in the current build pass. */
    void beginGroup(const std::string &id = "");
    /** @brief Opens a list container (rows added with addListItem). */
    void beginList(const std::string &id);
    /** @brief Opens a collapsible header. */
    void beginCollapsing(const std::string &label, const std::string &id = "", bool open = true);
    /** @brief Opens a sized child region. */
    void beginChild(const std::string &id, float width = 0.f, float height = 120.f);
    /** Virtualized scroll list; rows are the children added before end(). */
    void beginScrollList(const std::string &id = "", float height = 0.f, float itemHeight = 0.f);
    /**
     * @brief Begin a Flex container.
     * @param direction "row" / "column" (case-insensitive; default row)
     * @param id stable node id
     * @param gap spacing between children; <0 uses theme ItemSpacing
     */
    void beginFlex(const std::string &direction = "row", const std::string &id = "",
                   float gap = -1.f);
    /** @brief Opens a row flex container. */
    void beginRow(const std::string &id = "", float gap = -1.f);
    /** @brief Opens a column flex container. */
    void beginColumn(const std::string &id = "", float gap = -1.f);
    /** @brief Closes the innermost open container. */
    void end();
    /** @brief Adds a text label to the current container. */
    void addText(const std::string &content, const std::string &id = "");
    /** Text with an explicit wrap width (0 = no wrap). */
    void addTextWrapped(const std::string &content, float width, const std::string &id = "");
    /** @brief Adds a button to the current container. */
    void addButton(const std::string &label, const std::string &id = "");
    /** @brief Adds a semantic icon by name (for example "search" or "save"). */
    void addIcon(const std::string &name, const std::string &id = "");
    /** @brief Adds a semantic icon button with an optional visible label. */
    void addIconButton(const std::string &name, const std::string &label = "",
                       const std::string &id = "");
    /** @brief Adds an inline-break spacer. */
    void addSameLine(const std::string &id = "");
    /** @brief Adds a separator line. */
    void addSeparator(const std::string &id = "");
    /** @brief Adds a checkbox. */
    void addCheckbox(const std::string &label, bool checked, const std::string &id = "");
    /** @brief Adds a slider. */
    void addSlider(const std::string &label, float value, float minV, float maxV,
                   const std::string &id = "");
    /** @brief Adds a progress bar. */
    void addProgress(float fraction, const std::string &id = "", const std::string &overlay = "");
    /** Colored / textured image; size 0 = default (32px or flex-assigned). */
    void addImage(const std::string &id = "", float width = 0.f, float height = 0.f);
    /** Clickable image button (click routes through consumeClick / callbacks). */
    void addImageButton(const std::string &id, float width, float height);
    /** Embedded viewport widget (see viewportCanvas / viewport* input getters). */
    void addViewport(const std::string &id, float width = 0.f, float height = 0.f);
    /** Dropdown; options separated by '\n', `selected` = initial index. */
    void addCombo(const std::string &label, const std::string &options, int selected,
                  const std::string &id = "");
    /** @brief Adds an editable text field. */
    void addInputText(const std::string &label, const std::string &value, const std::string &id = "");
    /** @brief Flexible empty space inside Flex (default grow=1). */
    void addSpacer(const std::string &id = "", float grow = 1.f);
    /**
     * @brief Set flex item props on the most recently added child of the current open container.
     * No-op if there is no current child.
     */
    void setItemFlexGrow(float grow);
    /** @brief Sets width/height on the most recently added child. */
    void setItemSize(float width, float height);
    /** Layout box model on the most recently added child (no-op if none). */
    void setItemMargin(float l, float t, float r, float b);
    void setItemPadding(float l, float t, float r, float b);
    void setItemMinSize(float w, float h);
    void setItemMaxSize(float w, float h);
    void setItemPercent(float w, float h);
    /** Place the most recently added child absolutely inside the current Flex. */
    void setItemAbsolute(float anchorX, float anchorY, float x = 0.f, float y = 0.f);
    /** Set Flex container align/justify on the current open Flex (no-op otherwise). */
    /** @brief Set Flex container align/justify on the current open Flex (no-op otherwise). */
    void setFlexAlign(const std::string &align);
    /** @brief Sets Flex container justify on the current open Flex. */
    void setFlexJustify(const std::string &justify);
    /** @brief Append one list row button (call inside beginList). */
    void addListItem(const std::string &label, const std::string &id = "");

    /** @brief Finishes the build pass and mounts the built tree. */
    bool mountBuild();
    /** @brief Finishes the build pass and mounts as a named host. */
    bool mountBuildAs(const std::string &name);
    /** @brief Like mountBuildAs but reconciles by key when possible. */
    bool remountBuildAs(const std::string &name);

    /**
     * @brief Replace children of a Group `listId` with buttons for each label (reconcile).
     * Host must already exist and contain a group with that id.
     */
    bool setListItems(const std::string &listId, const std::vector<std::string> &items);

    /** @brief Widget state setters/getters on the current host (by node id). */
    void setText(const std::string &id, const std::string &text);
    void setTextWrap(const std::string &id, float width);
    void setVisible(const std::string &id, bool visible);
    void setChecked(const std::string &id, bool checked);
    void setValue(const std::string &id, float value);
    void setValueText(const std::string &id, const std::string &value);
    void setImageTint(const std::string &id, float r, float g, float b, float a = 1.f);
    void setImageUv(const std::string &id, float u0, float v0, float u1, float v1);
    void setImageNinePatch(const std::string &id, float l, float t, float r, float b);
    void setImageCornerRadius(const std::string &id, float radius);
    /** Bind a texture id from registerTexture() to an Image/ImageButton node. */
    void setImageTextureId(const std::string &id, uint64_t textureId);
    /** Register an engine texture for UI drawing; returns opaque id (0 = failure). */
    uint64_t registerTexture(graphics::Texture *tex);
    float getValue(const std::string &id) const;
    std::string getValueText(const std::string &id) const;
    bool getChecked(const std::string &id) const;
    /** @brief Host-level state. */
    void setHostVisible(bool visible);
    void setHostLayer(int layer);
    /** @brief Marks the host as a modal (blocks other hosts) / overlay. */
    void setHostModal(bool modal);
    void setHostOverlay(bool overlay);
    /** @brief Positions the host window with a pivot (0..1 each axis). */
    void setHostPos(float x, float y, float pivotX = 0.f, float pivotY = 0.f);
    /** Host anchor in display (0..1); offsets come from setHostPos. */
    void setHostAnchor(float x, float y);
    /** Explicit host window size (px). */
    void setHostSize(float w, float h);
    /** Host window size as a fraction of the display (0..1). */
    void setHostPercent(float w, float h);
    /**
     * Animate the selected host's window position (px) from its current value
     * to (x, y) over durationMs (0 = jump immediately). Driven by wall clock
     * inside beginFrameAndRender().
     */
    void animateHostPos(float x, float y, float durationMs);
    /** @brief Returns the id of the clicked widget since the last frame (or ""). */
    std::string consumeClick();
    /** @brief Returns the id of the changed widget since the last frame (or ""). */
    std::string consumeChange();

    /**
     * Script-side event callbacks (P0-3): register a closure on a node id of
     * the selected host. onClick(id, fn) fires fn(); onChange(id, fn) fires
     * fn(kind, value) where kind is "toggle"|"value"|"text" and value matches
     * the widget type (bool / float / string). Handlers run inside
     * dispatchEvents() (after the C++ callbacks and before the poll queues).
     */
    void onClick(const std::string &id, ssq::Function fn);
    void onChange(const std::string &id, ssq::Function fn);

    /** @brief Applies the dark/light built-in theme. */
    void setThemeDark();
    void setThemeLight();
    /** @brief Named preset: "dark" / "light" (case-insensitive). Returns false if unknown. */
    bool setTheme(const std::string &name);
    /** @brief Name of the active theme. */
    std::string getTheme() const;
    /** @brief Enables/disables keyboard navigation support. */
    void setNavKeyboard(bool enabled);
    void setNavGamepad(bool enabled);
    /** @brief Global UI scale factor (default 1). */
    void setScale(float scale);
    /** @brief Current UI scale factor. */
    float getScale() const;
    /** "hosts=.. nodes=.. measureMs=.. walkMs=.." from the last render frame. */
    std::string getStats() const;
    /** Serialize the selected host's tree to JSON (UI asset pipeline). */
    std::string saveTreeJson() const;
    /** Replace the selected host's tree from JSON produced by saveTreeJson(). */
    bool loadTreeJson(const std::string &json);
    /**
     * Offscreen render target of a Viewport widget in the selected host.
     * The game renders 2D (gfx.setCanvas) or 3D (gfx.renderScene3DToCanvas)
     * into it each frame before ui.beginFrameAndRender(); the UI then shows it.
     * Returns nullptr until the widget has a layout rect (first render frame).
     */
    graphics::Canvas *viewportCanvas(const std::string &id);
    bool viewportHovered(const std::string &id);
    bool viewportActive(const std::string &id);
    float viewportMouseX(const std::string &id);
    float viewportMouseY(const std::string &id);
    float viewportDragDX(const std::string &id);
    float viewportDragDY(const std::string &id);
    float viewportWheel(const std::string &id);

    /** @brief One-shot convenience: a window with a label and a button. */
    void mountSimple(const std::string &title, const std::string &labelText,
                     const std::string &buttonText);

    // ---- Reflection-driven property inspector (MVVM DevTools) ---------------
    /** @brief Opens the auto-generated inspector (scans reflected classes). */
    bool inspectOpen();
    /** @brief Closes the inspector panel. */
    void inspectClose();
    /** @brief Re-scans script classes; true when any class is reflected. */
    bool inspectRefresh();
    /** @brief Selects a class in the inspector (creates its first instance). */
    bool inspectSelectClass(const std::string &name);
    /** @brief Inspects a caller-provided live script instance. */
    bool inspectObject(ssq::Object object);
    /**
     * @brief Registers the script callback used by the inspector Pick button.
     * The callback is stored on the script side (eve._inspectorPickHandler) and
     * must return a live script instance (or null).
     */
    bool inspectSetPickHandler(ssq::Function fn);
    /** @brief Calls the pick handler and inspects the returned object. */
    bool inspectPickScene();
    /** @brief Creates another instance of the selected inspector class. */
    bool inspectAddInstance();

    // ---- Reflection-driven database panel -------------------------------
    /** @brief Opens the database panel (class menu + editable instance grid). */
    bool dbOpen();
    /** @brief Closes the database panel. */
    void dbClose();
    /** @brief Re-scans reflected classes; true when any class exists. */
    bool dbRefresh();
    /** @brief Selects the class shown in the grid. */
    bool dbSelectClass(const std::string &name);
    /**
     * @brief Registers a live script object in the database grid.
     * @param object Live script instance.
     * @param label  Optional display label ("" = auto "Class #n").
     * @return Entry id, or 0 on failure.
     */
    uint64_t dbRegister(ssq::Object object, const std::string &label);
    /** @brief Creates + registers an instance of the selected class. */
    uint64_t dbCreateInstance();
    /** @brief Removes an entry from the database grid. */
    bool dbUnregister(uint64_t id);

    // ---- DevTools editor shell --------------------------------------------
    /** @brief Opens the menu bar and docks the inspector + database panels. */
    bool editorOpen();
    /** @brief Closes the editor shell (and its docked panels). */
    void editorClose();
    /** @brief Shows one docked panel ("inspector"/"database"; "" hides all). */
    bool editorSelectPanel(const std::string &name);

    // ---- Scene hierarchy panel --------------------------------------------
    /** @brief Opens the scene panel (tree + selected node properties). */
    bool sceneOpen();
    /** @brief Closes the scene panel. */
    void sceneClose();
    /** @brief Selects a scene node by id. */
    bool sceneSelectNode(const std::string &id);
    /**
     * @brief Registers the script callback for the scene panel Pick button.
     * The callback (stored as eve._scenePickHandler) receives the node id and
     * typically maps it to a script instance, then calls ui.inspectObject().
     */
    bool sceneSetPickHandler(ssq::Function fn);

private:
    WidgetDesc &currentParent();
    void pushOpen(WidgetDesc d);
    bool buildComplete() const;
    UIHost *ensureSelected(const std::string &preferredName = "");

    std::unique_ptr<UIBackend> backend_;
    UIHost *selected_ = nullptr;

    std::vector<WidgetDesc> openStack_;
    WidgetDesc builtRoot_;
    bool hasBuiltRoot_ = false;

    struct ScriptHandler {
        ScriptHandler(std::string host, std::string node, std::string k, ssq::Function f)
            : hostName(std::move(host)), nodeId(std::move(node)), kind(std::move(k)),
              fn(std::move(f)) {}
        std::string hostName;
        std::string nodeId;
        std::string kind;  // "click" | "toggle" | "value" | "text"
        ssq::Function fn;
    };
    std::vector<ScriptHandler> scriptHandlers_;
    void fireScriptHandlers(const UIEvent &ev);

    struct HostTween {
        UIHost *host = nullptr;
        float fromX = 0.f;
        float fromY = 0.f;
        float toX = 0.f;
        float toY = 0.f;
        double startMs = 0.0;
        double durationMs = 0.0;
    };
    std::vector<HostTween> hostTweens_;
    void updateHostTweens();
    ssq::Object callPickHandler();
    void callScenePickHandler(const std::string &nodeId);
    std::unique_ptr<Inspector> inspector_;
    std::unique_ptr<DatabasePanel> databasePanel_;
    std::unique_ptr<EditorShell> editorShell_;
    std::unique_ptr<ScenePanel> scenePanel_;
};

}  // namespace eve::ui
