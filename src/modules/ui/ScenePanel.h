#pragma once

#include "common/Export.h"
#include "ui/Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace eve {
struct SceneNodeInfo;
}  // namespace eve

namespace eve::ui {

class UIHost;

/**
 * @brief Scene hierarchy panel (DevTools, MVVM).
 *
 * Queries the optional scene module through the ISceneQuery capability (no
 * direct scene dependency): renders a collapsible node tree, lets the user
 * select a node and edit its transform / visibility, and offers a Pick button
 * that hands the selected node id to a script-side handler (which maps it to a
 * live script instance for the Inspector).
 */
class EVENGINE_API ScenePanel {
public:
    ScenePanel() = default;
    ~ScenePanel();
    ScenePanel(const ScenePanel&) = delete;
    ScenePanel& operator=(const ScenePanel&) = delete;

    /** @brief Mounts (or updates) the scene host on the UI ECS world. */
    void open();
    /** @brief Hides the scene host. */
    void close();
    /** @brief True while the scene host is mounted and visible. */
    bool isOpen() const;
    /** @brief The mounted host (nullptr until open()); for embedding/tests. */
    UIHost* host() const { return host_; }

    /** @brief Re-queries the scene graph and rebuilds the tree. */
    void refresh();
    /** @brief Selects a node by id; false when unknown or no scene module. */
    bool selectNode(const std::string& id);
    /** @brief Id of the selected node ("" when none). */
    const std::string& selectedNode() const { return selectedId_; }
    /**
     * @brief Registers the pick handler for the Pick button.
     * @param handler Receives the selected node id ("" = none).
     */
    void setPickHandler(std::function<void(const std::string&)> handler);

    /** @brief Declarative tree of the current scene state. */
    WidgetDesc build();

private:
    WidgetDesc nodeTree(const eve::SceneNodeInfo& node,
                        const std::vector<eve::SceneNodeInfo>& all);
    void rebuildHost();

    UIHost* host_ = nullptr;
    std::string selectedId_;
    std::function<void(const std::string&)> pickHandler_;
};

}  // namespace eve::ui
