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

struct UIEvent;

/**
 * Declarative UI module (eve.UI).
 *
 * ECS: named UIHost panels. Builder + Component.build (C++/script) + list/when/theme.
 */
class UI : public Module {
public:
    Module_REG(UI);
    UI();
    ~UI() override;

    bool initBackend();
    void shutdownBackend();
    bool isBackendReady() const;

    void processEvent(const SDL_Event *event);
    void beginFrameAndRender();
    void dispatchEvents();

    bool wantCaptureMouse() const;
    bool wantCaptureKeyboard() const;

    UIHost *mountAs(const std::string &name, WidgetDesc root);
    UIHost *mount(WidgetDesc root);
    UIHost *remount(WidgetDesc root);
    /** Remount with key reconcile (props-only when structure matches). */
    UIHost *remountReconcile(WidgetDesc root);
    UIHost *remountAs(const std::string &name, WidgetDesc root);

    bool select(const std::string &name);
    UIHost *findHost(const std::string &name) const;
    UIHost *findHostByOwner(uint32_t ownerId) const;
    UIHost *current() const { return selected_; }
    void bindOwner(uint32_t ownerId);

    void beginBuild();
    void beginWindow(const std::string &title, const std::string &id = "root");
    void beginGroup(const std::string &id = "");
    void beginList(const std::string &id);
    void beginCollapsing(const std::string &label, const std::string &id = "", bool open = true);
    void beginChild(const std::string &id, float width = 0.f, float height = 120.f);
    /** Virtualized scroll list; rows are the children added before end(). */
    void beginScrollList(const std::string &id = "", float height = 0.f, float itemHeight = 0.f);
    /**
     * Begin a Flex container.
     * @param direction "row" / "column" (case-insensitive; default row)
     * @param id stable node id
     * @param gap spacing between children; <0 uses theme ItemSpacing
     */
    void beginFlex(const std::string &direction = "row", const std::string &id = "",
                   float gap = -1.f);
    void beginRow(const std::string &id = "", float gap = -1.f);
    void beginColumn(const std::string &id = "", float gap = -1.f);
    void end();
    void addText(const std::string &content, const std::string &id = "");
    /** Text with an explicit wrap width (0 = no wrap). */
    void addTextWrapped(const std::string &content, float width, const std::string &id = "");
    void addButton(const std::string &label, const std::string &id = "");
    void addSameLine(const std::string &id = "");
    void addSeparator(const std::string &id = "");
    void addCheckbox(const std::string &label, bool checked, const std::string &id = "");
    void addSlider(const std::string &label, float value, float minV, float maxV,
                   const std::string &id = "");
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
    void addInputText(const std::string &label, const std::string &value, const std::string &id = "");
    /** Flexible empty space inside Flex (default grow=1). */
    void addSpacer(const std::string &id = "", float grow = 1.f);
    /**
     * Set flex item props on the most recently added child of the current open container.
     * No-op if there is no current child.
     */
    void setItemFlexGrow(float grow);
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
    void setFlexAlign(const std::string &align);
    void setFlexJustify(const std::string &justify);
    /** Append one list row button (call inside beginList). */
    void addListItem(const std::string &label, const std::string &id = "");

    bool mountBuild();
    bool mountBuildAs(const std::string &name);
    /** Like mountBuildAs but reconciles by key when possible. */
    bool remountBuildAs(const std::string &name);

    /**
     * Replace children of a Group `listId` with buttons for each label (reconcile).
     * Host must already exist and contain a group with that id.
     */
    bool setListItems(const std::string &listId, const std::vector<std::string> &items);

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
    void setHostVisible(bool visible);
    void setHostLayer(int layer);
    void setHostModal(bool modal);
    void setHostOverlay(bool overlay);
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
    std::string consumeClick();
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

    void setThemeDark();
    void setThemeLight();
    /** Named preset: "dark" / "light" (case-insensitive). Returns false if unknown. */
    bool setTheme(const std::string &name);
    std::string getTheme() const;
    void setNavKeyboard(bool enabled);
    void setNavGamepad(bool enabled);
    void setScale(float scale);
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
};

}  // namespace eve::ui
