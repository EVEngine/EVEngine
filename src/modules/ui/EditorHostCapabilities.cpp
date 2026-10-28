#include <string>
#include "common/Capability.h"
#include "common/EditorHost.h"
#include "ui/EditorHost.h"
#include "ui/EditorShell.h"
#include "ui/UIHost.h"

// EditorHostCapabilities.cpp
namespace eve::ui {
namespace {

class EditorHostImpl final : public eve::IEditorHost {
public:
    std::string status() const override { return EditorHost::instance().status(); }
    std::string openWindow(const std::string &title, int width, int height) override {
        return EditorHost::instance().openWindow(title, width, height);
    }
    std::string closeWindow() override { return EditorHost::instance().closeWindow(); }
    std::string windowState() const override { return EditorHost::instance().windowState(); }
    bool        isWindowOpen() const override { return EditorHost::instance().isWindowOpen(); }
    std::string applyEditor(const std::string &json) override { return EditorHost::instance().applyEditor(json); }
    std::string removeEditor(const std::string &id) override { return EditorHost::instance().removeEditor(id); }
    std::string listEditors() const override { return EditorHost::instance().listEditors(); }
    std::string editorState(const std::string &id) const override { return EditorHost::instance().editorState(id); }
    std::string setEditorValue(const std::string &editorId, const std::string &widgetId,
                               const std::string &value) override {
        return EditorHost::instance().setEditorValue(editorId, widgetId, value);
    }
    std::string consumeEvents(const std::string &editorId) override {
        return EditorHost::instance().consumeEvents(editorId);
    }
    std::string widgetRect(const std::string &editorId, const std::string &widgetId) const override {
        return EditorHost::instance().widgetRect(editorId, widgetId);
    }
    std::string registerVM(const std::string &name, const std::string &source) override {
        return EditorHost::instance().registerVM(name, source);
    }
    std::string unregisterVM(const std::string &name) override { return EditorHost::instance().unregisterVM(name); }
    std::string saveEditor(const std::string &id) override { return EditorHost::instance().saveEditor(id); }
    std::string unloadEditor(const std::string &id) override { return EditorHost::instance().unloadEditor(id); }
    std::string capture(const std::string &path) override { return EditorHost::instance().capture(path); }
    std::string runScript(const std::string &source) override { return EditorHost::instance().runScript(source); }
    std::string reloadResource(const std::string &path) override { return EditorHost::instance().reloadResource(path); }
    std::string hotReloadStatus() const override { return EditorHost::instance().hotReloadStatus(); }
    void        setHotReloadWatchCount(int count) override { EditorHost::instance().setHotReloadWatchCount(count); }
    void        requestExit() override { EditorHost::instance().requestExit(); }
    bool        exitRequested() override { return EditorHost::instance().exitRequested(); }
    void        frame() override { EditorHost::instance().frame(); }
    void        stop() override { EditorHost::instance().stop(); }
    void        start(ssq::VM &vm, const std::string &gameRoot, bool allowWindow) override {
        EditorHost::instance().start(vm, gameRoot, allowWindow);
    }
    void exposeScriptApi(ssq::VM &vm) override { EditorHost::instance().exposeScriptApi(vm); }
};

}  // namespace

void registerEditorHostCapabilities() {
    static EditorHostImpl impl;
    eve::cap::provide<eve::IEditorHost>(&impl);
}

}  // namespace eve::ui

// EditorShell.cpp
namespace eve::ui {
namespace {

constexpr const char *kShellHostName = "eve_editor";

void setHostVisible(UIHostHandle handle, bool visible) {
    auto host = UIHost::resolve(handle);
    if (host) host->get().setVisible(visible);
}

}  // namespace

EditorShell::~EditorShell() {
    auto host = UIHost::resolve(host_);
    if (host) host->get().setTree(window("", {}));
}

void EditorShell::open(UIHostHandle inspector, UIHostHandle database, UIHostHandle scene) {
    inspector_ = inspector;
    database_  = database;
    scene_     = scene;
    auto host  = UIHost::resolve(host_);
    if (!host) {
        host_ = UIHost::createHost(kShellHostName);
        host  = UIHost::resolve(host_);
    }
    if (!host) return;
    UIHost &shell        = host->get();
    auto    meta         = shell.meta();
    meta->overlay        = true;  // no title bar
    meta->hasPos         = true;
    meta->lockPos        = true;
    meta->posX           = 0.f;
    meta->posY           = 0.f;
    meta->hasSize        = true;
    meta->sizeX          = 0.f;  // width = full display (percentW below)
    meta->sizeY          = toolbarHeight_;
    meta->lockSize       = true;
    meta->percentW       = 1.f;
    meta->visible        = true;
    meta->overlayBgAlpha = 1.f;
    meta->overlayFlush   = true;
    shell.setTree(build());
    relayout();
    setHostVisible(inspector_, true);
    setHostVisible(database_, true);
    setHostVisible(scene_, true);
}

void EditorShell::close() {
    setHostVisible(host_, false);
    setHostVisible(inspector_, false);
    setHostVisible(database_, false);
    setHostVisible(scene_, false);
}

bool EditorShell::isOpen() const {
    auto host = UIHost::resolve(host_);
    return host && host->get().meta()->visible;
}

bool EditorShell::selectPanel(const std::string &name) {
    auto host = UIHost::resolve(host_);
    if (!host || !host->get().meta()->visible) return false;
    if (name == "inspector") {
        setHostVisible(inspector_, true);
        setHostVisible(database_, false);
        setHostVisible(scene_, false);
        return true;
    }
    if (name == "database") {
        setHostVisible(database_, true);
        setHostVisible(inspector_, false);
        setHostVisible(scene_, false);
        return true;
    }
    if (name == "scene") {
        setHostVisible(scene_, true);
        setHostVisible(inspector_, false);
        setHostVisible(database_, false);
        return true;
    }
    if (name.empty()) {
        setHostVisible(inspector_, false);
        setHostVisible(database_, false);
        setHostVisible(scene_, false);
        return true;
    }
    return false;
}

bool EditorShell::togglePanel(const std::string &name) {
    auto host = UIHost::resolve(host_);
    if (!host || !host->get().meta()->visible) return false;
    UIHostHandle handle{};
    if (name == "inspector") handle = inspector_;
    if (name == "database") handle = database_;
    if (name == "scene") handle = scene_;
    auto panel = UIHost::resolve(handle);
    if (!panel) return false;
    UIHost &panelHost = panel->get();
    panelHost.setVisible(!panelHost.meta()->visible);
    return true;
}

WidgetDesc EditorShell::build() {
    std::vector<WidgetDesc> commands;
    commands.push_back(icon(Icon::Cubes, "brand_icon"));
    commands.push_back(text("EVEngine Editor", "lbl_title"));
    commands.push_back(badge("Workspace", "workspace_badge"));
    commands.push_back(spacer("menu_spacer"));
    commands.push_back(iconButton(Icon::Sliders, "", "menu_inspector", [this]() {
                           togglePanel("inspector");
                       }).withTooltip("Show or hide the Inspector"));
    commands.push_back(iconButton(Icon::Database, "", "menu_database", [this]() {
                           togglePanel("database");
                       }).withTooltip("Show or hide the object database"));
    commands.push_back(iconButton(Icon::Layers, "", "menu_scene", [this]() {
                           togglePanel("scene");
                       }).withTooltip("Show or hide the scene hierarchy"));
    commands.push_back(iconButton(Icon::EyeSlash, "", "menu_close", [this]() {
                           selectPanel("");
                       }).withTooltip("Hide all editor panels"));
    return window("Editor", {toolbar(std::move(commands), "editor_toolbar")}, "root");
}

void EditorShell::relayout() {
    // Seed a responsive three-column workspace once. lockPos/lockSize are
    // disabled so subsequent user adjustments are stored/restored by ImGui's
    // standard ini workspace persistence.
    if (auto inspector = UIHost::resolve(inspector_)) {
        auto meta      = inspector->get().meta();
        meta->hasPos   = true;
        meta->posX     = 0.f;
        meta->posY     = toolbarHeight_;
        meta->anchorX  = 0.f;
        meta->anchorY  = 0.f;
        meta->lockPos  = false;
        meta->hasSize  = true;
        meta->sizeX    = 0.f;
        meta->sizeY    = 0.f;
        meta->percentW = 0.3f;
        meta->percentH = 0.9f;
        meta->lockSize = false;
    }
    if (auto database = UIHost::resolve(database_)) {
        auto meta      = database->get().meta();
        meta->hasPos   = true;
        meta->posX     = 0.f;
        meta->posY     = toolbarHeight_;
        meta->anchorX  = 0.3f;
        meta->anchorY  = 0.f;
        meta->lockPos  = false;
        meta->hasSize  = true;
        meta->sizeX    = 0.f;
        meta->sizeY    = 0.f;
        meta->percentW = 0.35f;
        meta->percentH = 0.9f;
        meta->lockSize = false;
    }
    if (auto scene = UIHost::resolve(scene_)) {
        auto meta      = scene->get().meta();
        meta->hasPos   = true;
        meta->posX     = 0.f;
        meta->posY     = toolbarHeight_;
        meta->anchorX  = 0.65f;
        meta->anchorY  = 0.f;
        meta->lockPos  = false;
        meta->hasSize  = true;
        meta->sizeX    = 0.f;
        meta->sizeY    = 0.f;
        meta->percentW = 0.35f;
        meta->percentH = 0.9f;
        meta->lockSize = false;
    }
}

}  // namespace eve::ui
