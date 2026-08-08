#pragma once

#include "common/ECS.h"

#include <cstdint>
#include <functional>
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
    /** First node whose name equals `name` (DFS from root). */
    SceneNode *findByName(const std::string &name);
    /** Path of ids joined by '/', e.g. "root/player/weapon". Relative to host root. */
    SceneNode *findByPath(const std::string &path);
    int findIndexById(const std::string &id);
    int findIndexByKey(const std::string &key);
    int findIndexByName(const std::string &name);
    int findIndexByPath(const std::string &path);

    bool hasNode(const std::string &id);
    int getNodeCount();
    int getRootIndex();
    SceneNode *getRoot();
    SceneNode *getNode(int index);

    int getParentIndex(int nodeIndex);
    SceneNode *getParent(int nodeIndex);
    SceneNode *getParentById(const std::string &id);
    int getChildCount(int nodeIndex);
    int getChildCountById(const std::string &id);
    /** Child ordinal 0..count-1; -1 if out of range. */
    int getChildIndexAt(int parentIndex, int childOrdinal);
    SceneNode *getChildAt(int parentIndex, int childOrdinal);
    SceneNode *getChildAtById(const std::string &parentId, int childOrdinal);
    std::vector<int> getChildIndices(int nodeIndex);
    std::vector<SceneNode *> getChildren(int nodeIndex);
    std::vector<std::string> getChildIds(const std::string &parentId);

    /** Id path "a/b/c" from root to node; empty if invalid. */
    std::string getPath(int nodeIndex);
    std::string getPathById(const std::string &id);

    bool isAncestorOf(int ancestorIndex, int nodeIndex);
    bool isAncestorOfById(const std::string &ancestorId, const std::string &nodeId);
    bool isDescendantOf(int nodeIndex, int ancestorIndex);
    bool isDescendantOfById(const std::string &nodeId, const std::string &ancestorId);

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

    /** Depth-first visit of arena indices under `nodeIndex` (inclusive). C callback. */
    void forEachDepthFirst(int nodeIndex, void (*fn)(SceneHost *, int, void *), void *user);

    using NodeVisitFn = std::function<void(SceneHost *host, int index, SceneNode &node)>;
    using NodePredFn = std::function<bool(SceneHost *host, int index, const SceneNode &node)>;

    /** DFS from root (or from `nodeIndex`), inclusive. */
    void walkDepthFirst(NodeVisitFn fn);
    void walkDepthFirstFrom(int nodeIndex, NodeVisitFn fn);
    /** BFS from root (or from `nodeIndex`), inclusive. */
    void walkBreadthFirst(NodeVisitFn fn);
    void walkBreadthFirstFrom(int nodeIndex, NodeVisitFn fn);
    /** Direct children only. */
    void walkChildren(int parentIndex, NodeVisitFn fn);
    /** Parent chain upward, excluding self. */
    void walkAncestors(int nodeIndex, NodeVisitFn fn);

    /** First DFS match; nullptr if none. */
    SceneNode *findIf(NodePredFn pred);
    SceneNode *findIfFrom(int nodeIndex, NodePredFn pred);
    std::vector<SceneNode *> filter(NodePredFn pred);
    std::vector<SceneNode *> filterFrom(int nodeIndex, NodePredFn pred);
    std::vector<int> filterIndices(NodePredFn pred);
    std::vector<int> filterIndicesFrom(int nodeIndex, NodePredFn pred);

    std::vector<SceneNode *> findAllByName(const std::string &name);
    std::vector<SceneNode *> findAllByKey(const std::string &key);
    std::vector<SceneNode *> findAllVisible(bool visible = true);
    std::vector<SceneNode *> findAllBySpace(const std::string &space);
    std::vector<SceneNode *> findAllLinked();

    std::vector<std::string> collectIds();
    std::vector<std::string> collectIdsFrom(int nodeIndex);
    std::vector<std::string> collectIdsWhere(NodePredFn pred);
    std::vector<std::string> collectIdsByName(const std::string &name);
    std::vector<std::string> collectIdsVisible(bool visible = true);

    /**
     * Link a graphics renderable to a node id. Survives reconcile/rebuild by id.
     * After TransformSystem, world TRS is written into the renderable transform.
     */
    bool linkRenderable2D(const std::string &nodeId, graphics::Renderable2D *r);
    bool linkRenderable3D(const std::string &nodeId, graphics::Renderable3D *r);
    bool unlink(const std::string &nodeId);
};

}  // namespace eve::scene
