#pragma once

#include "common/Module.h"
#include "ui/UIBackend.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"

#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::ui {

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
    /** @brief Adds a button to the current container. */
    void addButton(const std::string &label, const std::string &id = "");
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
    void setVisible(const std::string &id, bool visible);
    void setChecked(const std::string &id, bool checked);
    void setValue(const std::string &id, float value);
    void setValueText(const std::string &id, const std::string &value);
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
    /** @brief Returns the id of the clicked widget since the last frame (or ""). */
    std::string consumeClick();
    /** @brief Returns the id of the changed widget since the last frame (or ""). */
    std::string consumeChange();

    /** @brief Applies the dark/light built-in theme. */
    void setThemeDark();
    void setThemeLight();
    /** @brief Named preset: "dark" / "light" (case-insensitive). Returns false if unknown. */
    bool setTheme(const std::string &name);
    /** @brief Name of the active theme. */
    std::string getTheme() const;
    /** @brief Enables/disables keyboard navigation support. */
    void setNavKeyboard(bool enabled);
    /** @brief Global UI scale factor (default 1). */
    void setScale(float scale);
    /** @brief Current UI scale factor. */
    float getScale() const;

    /** @brief One-shot convenience: a window with a label and a button. */
    void mountSimple(const std::string &title, const std::string &labelText,
                     const std::string &buttonText);

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
};

}  // namespace eve::ui
