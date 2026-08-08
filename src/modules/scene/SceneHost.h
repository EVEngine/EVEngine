#pragma once

#include "common/ECS.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

#include "scene/NodeDesc.h"

namespace eve::graphics {
class Renderable2D;
class Renderable3D;
}

namespace eve::scene {

/**
 * Retained scene node (arena). Conceptual GameObject; isomorphic to eve::ui::UINode.
 */
struct SceneNode {
    std::string id;
    std::string key;
    std::string name;
    std::string space = "3d";  // "2d" | "3d"
    bool visible = true;

    float x = 0.f, y = 0.f, z = 0.f;
    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    float sx = 1.f, sy = 1.f, sz = 1.f;

    bool localDirty = true;
    glm::mat4 world{1.f};

    /** Optional link: "renderable2d" | "renderable3d" | empty. Target owned elsewhere. */
    std::string linkKind;
    void *linkTarget = nullptr;

    int firstChild = -1;
    int nextSibling = -1;
    int parent = -1;
};

/** Alias: GameObject is the conceptual name for a SceneNode in a host tree. */
using GameObject = SceneNode;

/**
 * ECS mount point for one scene graph (full scene or nested subtree root).
 * Isomorphic to eve::ui::UIHost.
 */
class SceneHost : public ecs::Entity {
public:
    ENTITY(SceneHost, ecs::Entity)

    void release() override {}

    struct Meta {
        bool visible = true;
        int layer = 0;
        std::string name;
        uint32_t ownerId = 0;
        SceneHost *entity = nullptr;
    };

    struct Tree {
        bool dirty = true;
        bool transformDirty = true;
        std::vector<SceneNode> nodes;
        int root = -1;
    };

    COMPONENT(Meta, meta)
    COMPONENT(Tree, tree)

    static SceneHost *createHost(const std::string &name = "");

    void setName(const std::string &name);
    const std::string &getName();
    void setOwnerId(uint32_t id) { meta()->ownerId = id; }
    uint32_t getOwnerId() { return meta()->ownerId; }

    /** Full replace. */
    void setTree(NodeDesc root);
    /** Key-aware patch when structure matches; else full replace. */
    bool setTreeReconcile(NodeDesc root);

    SceneNode *findById(const std::string &id);
    SceneNode *findByKey(const std::string &key);
    int findIndexById(const std::string &id);

    void markDirty() { tree()->dirty = true; }
    void markTransformDirty() { tree()->transformDirty = true; }

    void setVisible(bool v) { meta()->visible = v; }
    void setLayer(int layer) { meta()->layer = layer; }

    /**
     * Imperative parent link. Returns false on invalid indices or cycle.
     * Prefer declarative NodeDesc + reconcile for normal use.
     */
    bool setParent(int childIndex, int parentIndex);
    bool addChild(int parentIndex, int childIndex);
    bool removeChild(int parentIndex, int childIndex);

    /** Depth-first visit of arena indices under root (inclusive). */
    void forEachDepthFirst(int nodeIndex, void (*fn)(SceneHost *, int, void *), void *user);

    /**
     * Link a graphics renderable to a node id. Survives reconcile/rebuild by id.
     * After TransformSystem, world TRS is written into the renderable transform.
     */
    bool linkRenderable2D(const std::string &nodeId, graphics::Renderable2D *r);
    bool linkRenderable3D(const std::string &nodeId, graphics::Renderable3D *r);
    bool unlink(const std::string &nodeId);
};

}  // namespace eve::scene
