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
    void end();
    void addText(const std::string &content, const std::string &id = "");
    void addButton(const std::string &label, const std::string &id = "");
    void addSameLine(const std::string &id = "");
    void addSeparator(const std::string &id = "");
    void addCheckbox(const std::string &label, bool checked, const std::string &id = "");
    void addSlider(const std::string &label, float value, float minV, float maxV,
                   const std::string &id = "");
    void addProgress(float fraction, const std::string &id = "", const std::string &overlay = "");
    void addInputText(const std::string &label, const std::string &value, const std::string &id = "");
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
    void setVisible(const std::string &id, bool visible);
    void setChecked(const std::string &id, bool checked);
    void setValue(const std::string &id, float value);
    void setValueText(const std::string &id, const std::string &value);
    float getValue(const std::string &id) const;
    std::string getValueText(const std::string &id) const;
    bool getChecked(const std::string &id) const;
    void setHostVisible(bool visible);
    void setHostLayer(int layer);
    void setHostModal(bool modal);
    void setHostOverlay(bool overlay);
    void setHostPos(float x, float y, float pivotX = 0.f, float pivotY = 0.f);
    std::string consumeClick();
    std::string consumeChange();

    void setThemeDark();
    void setThemeLight();
    /** Named preset: "dark" / "light" (case-insensitive). Returns false if unknown. */
    bool setTheme(const std::string &name);
    std::string getTheme() const;
    void setNavKeyboard(bool enabled);
    void setScale(float scale);
    float getScale() const;

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
