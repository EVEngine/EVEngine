#include "ui/EditorShell.h"

#include "ui/UIHost.h"

namespace eve::ui {
namespace {

constexpr const char* kShellHostName = "eve_editor";

}  // namespace

EditorShell::~EditorShell() {
    if (host_) host_->setTree(window("", {}));
}

void EditorShell::open(UIHost *inspector, UIHost *database, UIHost *scene) {
    inspector_ = inspector;
    database_ = database;
    scene_ = scene;
    if (!host_) {
        host_ = UIHost::createHost(kShellHostName);
    }
    auto meta = host_->meta();
    meta->overlay = true;           // no title bar
    meta->hasPos = true;
    meta->posX = 0.f;
    meta->posY = 0.f;
    meta->hasSize = true;
    meta->sizeX = 0.f;              // width = full display (percentW below)
    meta->sizeY = 34.f;
    meta->percentW = 1.f;
    meta->visible = true;
    host_->setTree(build());
    relayout();
}

void EditorShell::close() {
    if (host_) host_->setVisible(false);
    if (inspector_) inspector_->setVisible(false);
    if (database_) database_->setVisible(false);
}

bool EditorShell::isOpen() const {
    return host_ && host_->meta()->visible;
}

bool EditorShell::selectPanel(const std::string &name) {
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

WidgetDesc EditorShell::build() {
    std::vector<WidgetDesc> menu;
    menu.push_back(text("DevTools", "lbl_title"));
    menu.push_back(spacer("menu_spacer"));
    menu.push_back(button("Inspector##menu", "menu_inspector",
                          [this]() { selectPanel("inspector"); }));
    menu.push_back(button("Database##menu", "menu_database",
                          [this]() { selectPanel("database"); }));
    menu.push_back(button("Scene##menu", "menu_scene",
                          [this]() { selectPanel("scene"); }));
    menu.push_back(button("Close##menu", "menu_close",
                          [this]() { selectPanel(""); }));
    return window("Editor", {row(std::move(menu), "menubar")}, "root");
}

void EditorShell::relayout() {
    // Simple dock (MVP): inspector / database / scene left-to-right, below the
    // menu bar. Panels use fixed pixel sizes so they never overlap on the
    // default window; a full flex dock layout can replace this later.
    if (inspector_) {
        auto meta = inspector_->meta();
        meta->hasPos = true;
        meta->posX = 0.f;
        meta->posY = 34.f;
        meta->hasSize = true;
        meta->sizeX = 280.f;
        meta->sizeY = 600.f;
        meta->percentW = 0.f;
        meta->percentH = 0.f;
    }
    if (database_) {
        auto meta = database_->meta();
        meta->hasPos = true;
        meta->posX = 280.f;
        meta->posY = 34.f;
        meta->hasSize = true;
        meta->sizeX = 280.f;
        meta->sizeY = 600.f;
        meta->percentW = 0.f;
        meta->percentH = 0.f;
    }
    if (scene_) {
        auto meta = scene_->meta();
        meta->hasPos = true;
        meta->posX = 560.f;
        meta->posY = 34.f;
        meta->hasSize = true;
        meta->sizeX = 280.f;
        meta->sizeY = 600.f;
        meta->percentW = 0.f;
        meta->percentH = 0.f;
    }
}

}  // namespace eve::ui
