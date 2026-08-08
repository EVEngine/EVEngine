#pragma once

#include "common/Module.h"
#include "scene/NodeDesc.h"
#include "scene/SceneHost.h"

#include <string>
#include <vector>

namespace eve::graphics {
class Renderable2D;
class Renderable3D;
}

namespace eve::scene {

/**
 * Declarative scene module (eve.Scene).
 *
 * Isomorphic to eve::ui::UI: named SceneHost graphs, NodeDesc + SceneComponent.build,
 * mount / remountReconcile / beginBuild. TransformSystem propagates world matrices
 * and syncs linked Renderable2D/3D transforms.
 */
class Scene : public Module {
public:
    Module_REG(Scene);
    Scene() = default;
    ~Scene() override = default;

    SceneHost *mountAs(const std::string &name, NodeDesc root);
    SceneHost *mount(NodeDesc root);
    SceneHost *remount(NodeDesc root);
    /** Remount with key reconcile (props-only when structure matches). */
    SceneHost *remountReconcile(NodeDesc root);
    SceneHost *remountAs(const std::string &name, NodeDesc root);

    bool select(const std::string &name);
    SceneHost *findHost(const std::string &name) const;
    SceneHost *findHostByOwner(uint32_t ownerId) const;
    SceneHost *current() const { return selected_; }
    void bindOwner(uint32_t ownerId);

    void setHostVisible(bool visible);
    void setHostLayer(int layer);

    /** Propagate transforms (+ link sync) for all hosts (or current if selected). */
    void updateTransforms();
    void updateTransformsAll();

    /** Script-friendly: set local TRS on node id in current host; marks transform dirty. */
    bool setNodePosition(const std::string &id, float x, float y, float z);
    bool setNodeRotation(const std::string &id, float yaw, float pitch, float roll);
    bool setNodeScale(const std::string &id, float sx, float sy, float sz);
    bool setNodeVisible(const std::string &id, bool visible);

    bool linkRenderable2D(const std::string &nodeId, graphics::Renderable2D *r);
    bool linkRenderable3D(const std::string &nodeId, graphics::Renderable3D *r);
    bool unlinkNode(const std::string &nodeId);

    // --- Query / traverse (current host; script-friendly, id-based) ---
    bool hasNode(const std::string &id);
    int getNodeCount();
    std::string getRootId();
    std::string getParentId(const std::string &id);
    int getChildCount(const std::string &id);
    std::string getChildIdAt(const std::string &parentId, int childOrdinal);
    std::string findIdByName(const std::string &name);
    std::string findIdByPath(const std::string &path);
    std::string getNodePath(const std::string &id);
    bool isAncestor(const std::string &ancestorId, const std::string &nodeId);
    bool isDescendant(const std::string &nodeId, const std::string &ancestorId);
    std::vector<std::string> collectIds();
    std::vector<std::string> collectIdsFrom(const std::string &id);
    std::vector<std::string> collectIdsByName(const std::string &name);
    std::vector<std::string> collectIdsVisible(bool visible);
    std::vector<std::string> collectChildIds(const std::string &parentId);
    /** DFS order of ids under root (same as collectIds). Kept for script naming clarity. */
    std::vector<std::string> walkDepthFirstIds();
    std::vector<std::string> walkBreadthFirstIds();

    // --- Imperative builder (script / isomorphic to UI) ---
    void beginBuild();
    void beginNode(const std::string &id, const std::string &name = "");
    void beginGroup(const std::string &id = "");
    void end();
    void addNode(const std::string &id, const std::string &name = "");
    void setBuildPosition(float x, float y, float z = 0.f);
    void setBuildRotation(float yaw, float pitch = 0.f, float roll = 0.f);
    void setBuildScale(float sx, float sy, float sz = 1.f);
    void setBuildSpace(const std::string &space);
    void setBuildVisible(bool visible);
    bool mountBuild();
    bool mountBuildAs(const std::string &name);
    bool remountBuildAs(const std::string &name);

private:
    SceneHost *ensureSelected(const std::string &preferredName = "");
    NodeDesc &currentParent();
    void pushOpen(NodeDesc d);
    bool buildComplete() const;

    SceneHost *selected_ = nullptr;

    std::vector<NodeDesc> openStack_;
    NodeDesc builtRoot_;
    bool hasBuiltRoot_ = false;
};

}  // namespace eve::scene
