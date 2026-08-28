#pragma once

#include "common/ECS.h"
#include "common/Identity.h"
#include "common/Result.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/mat4x4.hpp>

#include "scene/NodeDesc.h"
#include "scene/SceneLink.h"

// Only pointers to these cross the interface, so a forward declaration is
// enough: scene links carry them as void* and the owning module does the casts.
namespace eve::graphics {
class Renderable2D;
class Renderable3D;
class Camera3D;
}

namespace eve::physics {
class Body;
class Body3D;
}

namespace eve::audio {
class Source;
}

namespace eve::scene {

/**
 * @brief Retained scene node (arena). Conceptual GameObject; isomorphic to eve::ui::UINode.
 */
struct SceneNode {
    std::string id;
    /** @brief Persisted object identity; nil means the legacy locator is still in use. */
    eve::SceneObjectId persistentId;
    std::string key;
    std::string name;
    std::string space = "3d";  // "2d" | "3d"
    bool visible = true;
    /** @brief Gameplay groups (Godot-style): multiple string tags per node. */
    std::vector<std::string> tags;
    int layer = 0;
    /** @brief Local-space AABB (for picking / culling / spatial index). */
    float bminX = 0.f, bminY = 0.f, bminZ = 0.f;
    float bmaxX = 0.f, bmaxY = 0.f, bmaxZ = 0.f;
    bool hasBounds = false;

    float x = 0.f, y = 0.f, z = 0.f;
    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    float sx = 1.f, sy = 1.f, sz = 1.f;

    bool localDirty = true;
    /** @brief Subtree needs world recompute (API edits mark node + ancestors). */
    bool subtreeDirty = true;
    glm::mat4 world{1.f};

    /** @brief Generic links (renderable / physics / camera / audio). Target owned elsewhere. */
    std::vector<SceneLink> links;

    /** @brief Lazy companion SceneObject ECS runtime id; 0 = none. Never a persisted identity. */
    uint32_t objectId = 0;

    int firstChild = -1;
    int nextSibling = -1;
    int parent = -1;
};

/** @brief Alias: GameObject is the conceptual name for a SceneNode in a host tree. */
using GameObject = SceneNode;

/**
 * @brief ECS mount point for one scene graph (full scene or nested subtree root).
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
        bool indexValid = false;
        std::unordered_map<std::string, int> idIndex;
        std::vector<SceneNode> nodes;
        int root = -1;
    };

    COMPONENT(Meta, meta)
    COMPONENT(Tree, tree)

    /**
     * @brief Creates an ECS-owned scene host with an explicit Result contract.
     * @param name Stable host name; empty names receive a process-local name.
     * @return Borrowed host pointer, or a structured allocation/ownership failure.
     * @remarks The ECS table owns the host. The pointer becomes stale after
     *          entity destruction; retain a runtime handle across deferred work.
     */
    [[nodiscard]] static eve::Result<SceneHost *> createHost(const std::string &name = "");

    void setName(const std::string &name);
    const std::string &getName();
    void setOwnerId(uint32_t ownerId) { meta()->ownerId = ownerId; }
    uint32_t getOwnerId() { return meta()->ownerId; }

    /** @brief Full replace. */
    void setTree(NodeDesc root);
    /** @brief Key-aware patch when structure matches; else full replace. */
    bool setTreeReconcile(NodeDesc root);

    /**
     * @brief Finds a node by stable id with an explicit status.
     * @return Borrowed node, invalidated by the next tree replacement/reconcile, or a failure.
     */
    [[nodiscard]] eve::Result<SceneNode *> findById(const std::string &id);
    /**
     * @brief Finds a node by reconcile key with an explicit status.
     * @return Borrowed node, invalidated by the next tree replacement/reconcile, or a failure.
     */
    [[nodiscard]] eve::Result<SceneNode *> findByKey(const std::string &key);
    /**
     * @brief Finds the first node name match with an explicit status.
     * @return Borrowed node, invalidated by the next tree replacement/reconcile, or a failure.
     */
    [[nodiscard]] eve::Result<SceneNode *> findByName(const std::string &name);
    /**
     * @brief Finds a node path with an explicit status.
     * @return Borrowed node, invalidated by the next tree replacement/reconcile, or a failure.
     */
    [[nodiscard]] eve::Result<SceneNode *> findByPath(const std::string &path);
    int findIndexById(const std::string &id);
    int findIndexByKey(const std::string &key);
    int findIndexByName(const std::string &name);
    int findIndexByPath(const std::string &path);

    bool hasNode(const std::string &id);
    int getNodeCount();
    int getRootIndex();
    [[nodiscard]] SceneNode *getRoot();
    [[nodiscard]] SceneNode *getNode(int index);

    int getParentIndex(int nodeIndex);
    [[nodiscard]] SceneNode *getParent(int nodeIndex);
    [[nodiscard]] SceneNode *getParentById(const std::string &id);
    int getChildCount(int nodeIndex);
    int getChildCountById(const std::string &id);
    /** @brief Child ordinal 0..count-1; -1 if out of range. */
    int getChildIndexAt(int parentIndex, int childOrdinal);
    [[nodiscard]] SceneNode *getChildAt(int parentIndex, int childOrdinal);
    [[nodiscard]] SceneNode *getChildAtById(const std::string &parentId, int childOrdinal);
    std::vector<int> getChildIndices(int nodeIndex);
    std::vector<SceneNode *> getChildren(int nodeIndex);
    std::vector<std::string> getChildIds(const std::string &parentId);

    /** @brief Id path "a/b/c" from root to node; empty if invalid. */
    std::string getPath(int nodeIndex);
    std::string getPathById(const std::string &id);

    bool isAncestorOf(int ancestorIndex, int nodeIndex);
    bool isAncestorOfById(const std::string &ancestorId, const std::string &nodeId);
    bool isDescendantOf(int nodeIndex, int ancestorIndex);
    bool isDescendantOfById(const std::string &nodeId, const std::string &ancestorId);

    void markDirty() { tree()->dirty = true; }
    void markTransformDirty() { tree()->transformDirty = true; }
    /** @brief Mark a node's subtree for world recompute (recompute it + descendants). */
    void markSubtreeDirty(int nodeIndex);
    void markSubtreeDirtyById(const std::string &nodeId) {
        markSubtreeDirty(findIndexById(nodeId));
    }
    void invalidateIndex() { tree()->indexValid = false; }

    // --- Node lifecycle events ---
    using SceneEventFn = std::function<void(SceneHost *host, const std::string &action,
                                            const std::string &nodeId,
                                            const std::string &parentId)>;
    void setEventHandler(SceneEventFn fn) { eventHandler_ = std::move(fn); }
    /** @brief Fire "node_added" / "node_removed" / "node_moved" / "node_changed". */
    void fireEvent(const std::string &action, const std::string &nodeId,
                   const std::string &parentId = {});

    void setVisible(bool v) { meta()->visible = v; }
    void setLayer(int layer) { meta()->layer = layer; }

    /**
     * @brief Imperative parent link. Returns false on invalid indices or cycle.
     * Prefer declarative NodeDesc + reconcile for normal use.
     */
    bool setParent(int childIndex, int parentIndex);
    bool addChild(int parentIndex, int childIndex);
    bool removeChild(int parentIndex, int childIndex);

    /**
     * @brief Append one retained node without rebuilding existing nodes or links.
     * @param node Node value with a unique non-empty id; hierarchy indices are ignored.
     * @param parentId Existing parent id, or empty to append a detached root-level node.
     * @return True when the node was appended and linked successfully.
     * @remarks Appending may reallocate the node arena and invalidates borrowed SceneNode pointers.
     */
    bool appendNode(SceneNode node, const std::string &parentId = {});
    /**
     * @brief Remove a leaf node while preserving every other node and external link.
     * @param nodeId Stable id of a node without children.
     * @return True when the leaf existed and was removed.
     * @remarks Removal compacts the arena and invalidates borrowed SceneNode pointers.
     */
    bool removeLeaf(const std::string &nodeId);
    /** @brief Rename a retained node and emit a node_changed event. */
    bool renameNode(const std::string &nodeId, const std::string &name);
    /** @brief Replace a node's local TRS and mark its subtree dirty. */
    bool setLocalTransform(const std::string &nodeId, float x, float y, float z,
                           float yaw, float pitch, float roll, float sx, float sy, float sz);

    /** @brief Depth-first visit of arena indices under `nodeIndex` (inclusive). C callback. */
    void forEachDepthFirst(int nodeIndex, void (*fn)(SceneHost *, int, void *), void *user);

    using NodeVisitFn = std::function<void(SceneHost *host, int index, SceneNode &node)>;
    using NodePredFn = std::function<bool(SceneHost *host, int index, const SceneNode &node)>;

    /** @brief DFS from root (or from `nodeIndex`), inclusive. */
    void walkDepthFirst(NodeVisitFn fn);
    void walkDepthFirstFrom(int nodeIndex, NodeVisitFn fn);
    /** @brief BFS from root (or from `nodeIndex`), inclusive. */
    void walkBreadthFirst(NodeVisitFn fn);
    void walkBreadthFirstFrom(int nodeIndex, NodeVisitFn fn);
    /** @brief Direct children only. */
    void walkChildren(int parentIndex, NodeVisitFn fn);
    /** @brief Parent chain upward, excluding self. */
    void walkAncestors(int nodeIndex, NodeVisitFn fn);

    /** @brief First DFS match; nullptr if none. */
    [[nodiscard]] SceneNode *findIf(NodePredFn pred);
    [[nodiscard]] SceneNode *findIfFrom(int nodeIndex, NodePredFn pred);
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
     * @brief Attach an external object to a node id. Survives reconcile/rebuild by id.
     * After TransformSystem the link is synced per kind (and syncMode).
     *
     * `kind` is an id from scene::registerLinkKind, so any module can attach to
     * the tree; the typed helpers below are conveniences for the kinds the
     * engine ships. Returns false when the node or the kind is unknown.
     */
    bool link(const std::string &nodeId, int kind, void *target, int syncMode = 0);

    bool linkRenderable2D(const std::string &nodeId, graphics::Renderable2D *r);
    bool linkRenderable3D(const std::string &nodeId, graphics::Renderable3D *r);
    bool linkPhysics2D(const std::string &nodeId, physics::Body *b, int syncMode = 0);
    bool linkPhysics3D(const std::string &nodeId, physics::Body3D *b, int syncMode = 0);
    bool linkCamera3D(const std::string &nodeId, graphics::Camera3D *c);
    bool linkAudio3D(const std::string &nodeId, audio::Source *s);

    /** @brief Remove all links of a kind; returns true if a link was removed. */
    bool unlink(const std::string &nodeId, int kind);
    /** @brief Remove every link on the node. */
    bool unlink(const std::string &nodeId);

    [[nodiscard]] SceneLink *findLink(SceneNode *node, int kind);
    /**
     * @brief Find a link on a node without transferring ownership.
     * @return Borrowed nullable link owned by the node's host.
     * @ownership SceneHost owns link records; callers must not delete or mutate the result.
     * @lifetime Valid until link mutation, tree replacement, or host destruction.
     * @thread Call on the scene thread owning this host.
     * @reentrancy Do not retain across link callbacks or host mutation.
     */
    [[nodiscard]] const SceneLink *findLink(const SceneNode *node, int kind) const;
    int linkCount(const std::string &nodeId);

    /** @brief Reparent by id; empty parentId detaches (parent = -1). Cycle-safe. */
    bool setParentById(const std::string &childId, const std::string &parentId);
    bool removeChildById(const std::string &parentId, const std::string &childId);

    bool addTag(SceneNode *node, const std::string &tag);
    bool removeTag(SceneNode *node, const std::string &tag);
    bool hasTag(const SceneNode *node, const std::string &tag) const;
    std::vector<SceneNode *> findAllByTag(const std::string &tag);

private:
    SceneEventFn eventHandler_;
};

}  // namespace eve::scene
