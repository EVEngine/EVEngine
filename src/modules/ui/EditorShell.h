#pragma once

#include "common/Export.h"
#include "ui/Widget.h"

#include <string>

namespace eve::ui {

class UIHost;

/**
 * @brief DevTools editor shell (main menu bar + docked panel layout).
 *
 * Owns a compact toolbar host ("eve_editor") with one command per tool panel.
 * Panel windows receive a responsive
 * initial three-column layout, then remain
 * user-movable/resizable so ImGui's ini persistence restores the
 * workspace.
 */
class EVENGINE_API EditorShell {
public:
    EditorShell() = default;
    ~EditorShell();
    EditorShell(const EditorShell&)            = delete;
    EditorShell& operator=(const EditorShell&) = delete;

    /**
     * @brief Mounts the menu bar and docks the given panel hosts.
     * @param inspector Inspector panel host (may be null).
     * @param database  Database panel host (may be null).
     * @param scene     Scene panel host (may be null).
     */
    void open(UIHost* inspector, UIHost* database, UIHost* scene);
    /** @brief Hides the menu bar and every docked panel. */
    void close();
    /** @brief True while the shell is mounted and visible. */
    bool isOpen() const;
    /** @brief The menu-bar host (nullptr until open()); for tests/embedding. */
    UIHost* host() const { return host_; }

    /**
     * @brief Shows one panel and hides the others.
     * @param name "inspector" | "database" | "scene" ("" hides all panels).
     * @return False when the name is unknown or the shell is not open.
     */
    bool selectPanel(const std::string& name);

    /** @brief Toggles one panel without hiding the other docked panels. */
    bool togglePanel(const std::string& name);

    /** @brief Declarative tree of the menu bar. */
    WidgetDesc build();

private:
    void relayout();

    UIHost* host_          = nullptr;
    UIHost* inspector_     = nullptr;
    UIHost* database_      = nullptr;
    UIHost* scene_         = nullptr;
    float   toolbarHeight_ = 52.f;
};

}  // namespace eve::ui
