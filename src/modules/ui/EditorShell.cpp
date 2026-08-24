#include "ui/EditorShell.h"

#include "ui/UIHost.h"

namespace eve::ui {
namespace {

constexpr const char* kShellHostName = "eve_editor";

}  // namespace

EditorShell::~EditorShell() {
    if (host_) host_->setTree(window("", {}));
}

void EditorShell::open(UIHost* inspector, UIHost* database, UIHost* scene) {
    inspector_ = inspector;
    database_  = database;
    scene_     = scene;
    if (!host_) {
        host_ = UIHost::createHost(kShellHostName);
    }
    auto meta            = host_->meta();
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
    host_->setTree(build());
    relayout();
    if (inspector_) inspector_->setVisible(true);
    if (database_) database_->setVisible(true);
    if (scene_) scene_->setVisible(true);
}

void EditorShell::close() {
    if (host_) host_->setVisible(false);
    if (inspector_) inspector_->setVisible(false);
    if (database_) database_->setVisible(false);
    if (scene_) scene_->setVisible(false);
}

bool EditorShell::isOpen() const { return host_ && host_->meta()->visible; }

bool EditorShell::selectPanel(const std::string& name) {
    if (!host_ || !host_->meta()->visible) return false;
    if (name == "inspector") {
        if (inspector_) inspector_->setVisible(true);
        if (database_) database_->setVisible(false);
        if (scene_) scene_->setVisible(false);
        return true;
    }
    if (name == "database") {
        if (database_) database_->setVisible(true);
        if (inspector_) inspector_->setVisible(false);
        if (scene_) scene_->setVisible(false);
        return true;
    }
    if (name == "scene") {
        if (scene_) scene_->setVisible(true);
        if (inspector_) inspector_->setVisible(false);
        if (database_) database_->setVisible(false);
        return true;
    }
    if (name.empty()) {
        if (inspector_) inspector_->setVisible(false);
        if (database_) database_->setVisible(false);
        if (scene_) scene_->setVisible(false);
        return true;
    }
    return false;
}

bool EditorShell::togglePanel(const std::string& name) {
    if (!host_ || !host_->meta()->visible) return false;
    UIHost* panel = nullptr;
    if (name == "inspector") panel = inspector_;
    if (name == "database") panel = database_;
    if (name == "scene") panel = scene_;
    if (!panel) return false;
    panel->setVisible(!panel->meta()->visible);
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
    if (inspector_) {
        auto meta      = inspector_->meta();
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
    if (database_) {
        auto meta      = database_->meta();
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
    if (scene_) {
        auto meta      = scene_->meta();
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
