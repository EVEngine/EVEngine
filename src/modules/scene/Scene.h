#pragma once

#include "common/Module.h"
#include "common/Result.h"
#include "scene/NodeDesc.h"
#include "scene/SceneHost.h"
#include "scene/SceneNodeRef.h"
#include "scene/SceneObject.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ssq {
class Object;
}

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

namespace eve::spatial {
class Octree;
}

namespace eve::scene {

class SceneObject;

/**
 * @brief Declarative scene module (eve.Scene).
 *
 * Isomorphic to eve::ui::UI: named SceneHost graphs, NodeDesc + SceneComponent.build,
 * mount / remountReconcile / beginBuild. TransformSystem propagates world matrices
 * and syncs linked Renderable2D/3D transforms.
 */
class Scene : public Module {
public:
    Module_REG(Scene);
    Scene();
    ~Scene() override = default;

    /**
     * @brief Creates/replaces a named host from a NodeDesc tree and selects it.
     * @return A borrowed host owned by the ECS scene table, or a structured failure.
     * @remarks The returned pointer remains valid until that host is destroyed;
     *          nodes are invalidated by a subsequent tree replacement/reconcile.
     */
    [[nodiscard]] eve::Result<SceneHost *> mountAs(const std::string &name, NodeDesc root);
    /**
     * @brief Mounts the tree as the default host and selects it.
     * @return Borrowed ECS-owned host, or a structured failure.
     */
    [[nodiscard]] eve::Result<SceneHost *> mount(NodeDesc root);
    /**
     * @brief Replaces the selected host's tree.
     * @return Borrowed ECS-owned host, or a structured failure.
     */
    [[nodiscard]] eve::Result<SceneHost *> remount(NodeDesc root);
    /**
     * @brief Reconciles the selected host, reporting whether replacement failed.
     * @return Borrowed ECS-owned host, or a structured failure.
     */
    [[nodiscard]] eve::Result<SceneHost *> remountReconcile(NodeDesc root);
    /**
     * @brief Creates/replaces a named host through the canonical mount path.
     * @return Borrowed ECS-owned host, or a structured failure.
     */
    [[nodiscard]] eve::Result<SceneHost *> remountAs(const std::string &name, NodeDesc root);

    /** @brief Selects a named host; false when it does not exist. */
    bool select(const std::string &name);
    /**
     * @brief Finds a host by name with an explicit not-found status.
     * @return Borrowed ECS-owned host, or a structured not-found/argument failure.
     */
    [[nodiscard]] eve::Result<SceneHost *> findHost(const std::string &name) const;
    /**
     * @brief Finds the host bound to an owner id with an explicit status.
     * @return Borrowed ECS-owned host, or a structured not-found/argument failure.
     */
    [[nodiscard]] eve::Result<SceneHost *> findHostByOwner(uint32_t ownerId) const;
    /** @brief Currently selected borrowed host, or nullptr. */
    [[nodiscard]] SceneHost *current() const noexcept { return selected_; }
    /** @brief Binds the selected host to a UI/scene owner id. */
    void bindOwner(uint32_t ownerId);

    /** @brief Shows/hides every host (or the selected one when a host is selected). */
    void setHostVisible(bool visible);
    /** @brief Sets the render layer of every host (or the selected one). */
    void setHostLayer(int layer);

    /** @brief Propagate transforms (+ link sync) for all hosts (or current if selected). */
    void updateTransforms();
    /** @brief Propagate transforms for every host regardless of selection. */
    void updateTransformsAll();

    /** @brief Script-friendly TRS setters on the current host; each marks the transform dirty. */
    bool setNodePosition(const std::string &id, float x, float y, float z);
    /** @brief Sets the local rotation (yaw/pitch/roll in degrees) of a node. */
    bool setNodeRotation(const std::string &id, float yaw, float pitch, float roll);
    /** @brief Sets the local scale of a node. */
    bool setNodeScale(const std::string &id, float sx, float sy, float sz);
    /** @brief Shows/hides a node. */
    bool setNodeVisible(const std::string &id, bool visible);

    /** @brief Links a 2D renderable to a node in the current host. */
    bool linkRenderable2D(const std::string &nodeId, graphics::Renderable2D *r);
    /** @brief Links a 3D renderable to a node in the current host. */
    bool linkRenderable3D(const std::string &nodeId, graphics::Renderable3D *r);
    /** @brief Removes every link on a node in the current host. */
    bool unlinkNode(const std::string &nodeId);

    // --- Generic link system (host-scoped primitives; script wrappers in
    // kSceneEntityScript provide selected-host convenience + NodeRef forms) ---
    bool linkRenderable2DAt(const std::string &hostName, const std::string &nodeId,
                            graphics::Renderable2D *r);
    bool linkRenderable3DAt(const std::string &hostName, const std::string &nodeId,
                            graphics::Renderable3D *r);
    bool linkPhysics2DAt(const std::string &hostName, const std::string &nodeId,
                         physics::Body *b, const std::string &mode);
    bool linkPhysics3DAt(const std::string &hostName, const std::string &nodeId,
                         physics::Body3D *b, const std::string &mode);
    bool linkCamera3DAt(const std::string &hostName, const std::string &nodeId,
                        graphics::Camera3D *c);
    bool linkAudio3DAt(const std::string &hostName, const std::string &nodeId,
                       audio::Source *s);
    /** @brief Remove every link on the node. */
    bool unlinkNodeAt(const std::string &hostName, const std::string &nodeId);
    /** @brief Remove links of one kind ("renderable2d"|"renderable3d"|"physics2d"|...). */
    bool unlinkNodeKindAt(const std::string &hostName, const std::string &nodeId,
                          const std::string &kind);
    int linkCountAt(const std::string &hostName, const std::string &nodeId);

    // --- Script-API completeness (host-scoped primitives; script wrappers in
    // kSceneEntityScript provide selected-host convenience + NodeRef forms) ---

    std::vector<float> getNodePositionAt(const std::string &hostName,
                                         const std::string &nodeId) const;
    std::vector<float> getNodeRotationAt(const std::string &hostName,
                                         const std::string &nodeId) const;
    std::vector<float> getNodeScaleAt(const std::string &hostName,
                                      const std::string &nodeId) const;
    bool getNodeVisibleAt(const std::string &hostName, const std::string &nodeId) const;
    std::vector<float> getNodeWorldPositionAt(const std::string &hostName,
                                              const std::string &nodeId) const;
    std::vector<float> getNodeWorldRotationAt(const std::string &hostName,
                                              const std::string &nodeId) const;
    std::vector<float> getNodeWorldScaleAt(const std::string &hostName,
                                           const std::string &nodeId) const;

    std::vector<float> localToWorldAt(const std::string &hostName,
                                      const std::string &nodeId, float x, float y,
                                      float z) const;
    std::vector<float> worldToLocalAt(const std::string &hostName,
                                      const std::string &nodeId, float x, float y,
                                      float z) const;

    /** @brief Reparent by id; empty parentId detaches. Cycle-safe. */
    bool setNodeParentAt(const std::string &hostName, const std::string &childId,
                         const std::string &parentId);
    /** @brief Detach node from its parent (arena node stays; rebuild to delete). */
    bool removeNodeAt(const std::string &hostName, const std::string &nodeId);
    bool addChildAt(const std::string &hostName, const std::string &parentId,
                    const std::string &childId);
    bool removeChildAt(const std::string &hostName, const std::string &parentId,
                       const std::string &childId);

    bool setNodeQuaternionAt(const std::string &hostName, const std::string &nodeId,
                             float qx, float qy, float qz, float qw);
    std::vector<float> getNodeQuaternionAt(const std::string &hostName,
                                           const std::string &nodeId) const;
    /** @brief Orient node so its local +Z axis points at (tx,ty,tz). */
    bool setNodeLookAtAt(const std::string &hostName, const std::string &nodeId,
                         float tx, float ty, float tz);

    bool addNodeTagAt(const std::string &hostName, const std::string &nodeId,
                      const std::string &tag);
    bool removeNodeTagAt(const std::string &hostName, const std::string &nodeId,
                         const std::string &tag);
    bool hasNodeTagAt(const std::string &hostName, const std::string &nodeId,
                      const std::string &tag) const;
    std::vector<std::string> getNodeTagsAt(const std::string &hostName,
                                           const std::string &nodeId) const;
    std::vector<std::string> collectIdsByTagAt(const std::string &hostName,
                                               const std::string &tag) const;
    bool setNodeLayerAt(const std::string &hostName, const std::string &nodeId, int layer);
    int getNodeLayerAt(const std::string &hostName, const std::string &nodeId) const;

    /**
     * @brief Register a script callback for node lifecycle events on a host:
     * The callback receives action, nodeId and parentId; action is one of
     * {"node_added","node_removed","node_moved","node_changed"}.
     * Pass null cb to clear. Rooted for the process lifetime.
     */
    bool setNodeEventHandlerAt(const std::string &hostName, ssq::Object cb);

    // --- Bounds (picking / culling / spatial index) ---
    bool setNodeBoundsAt(const std::string &hostName, const std::string &nodeId,
                         float minX, float minY, float minZ, float maxX, float maxY,
                         float maxZ);
    bool hasNodeBoundsAt(const std::string &hostName, const std::string &nodeId) const;
    std::vector<float> getNodeBoundsAt(const std::string &hostName,
                                       const std::string &nodeId) const;

    // --- Serialization (SceneHost ↔ JSON) ---
    std::string serializeHostAt(const std::string &hostName) const;
    bool deserializeHostAt(const std::string &hostName, const std::string &json);

    // --- Picking ---
    /** @brief Nearest node id hit by a world ray (nodes with bounds), or "". */
    std::string pickRayAt(const std::string &hostName, float ox, float oy, float oz,
                          float dx, float dy, float dz) const;
    /** @brief Screen-space picking through a Camera3D (camera.screenToRay). */
    std::string pickScreenAt(const std::string &hostName, graphics::Camera3D *cam,
                             float screenX, float screenY, float viewW, float viewH) const;

    // --- Culling / spatial index ---
    /** @brief Ids of nodes whose world AABB intersects the camera frustum. */
    std::vector<std::string> collectFrustumIdsAt(const std::string &hostName,
                                                 graphics::Camera3D *cam, float viewW,
                                                 float viewH) const;
    /** @brief Insert every bounded node's world AABB into an octree (id = arena index). */
    bool syncSpatialIndexAt(const std::string &hostName, spatial::Octree *ot) const;
    /** @brief Map a spatial-index id (arena index) back to a node id. */
    std::string nodeIdFromSpatialIdAt(const std::string &hostName, int index) const;

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
    /** @brief DFS order of ids under root (same as collectIds). Kept for script naming clarity. */
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

    // ------------------------------------------------------------------
    // Per-node script entities (eve.SceneEntity). Native primitives called by
    // script wrappers injected in expose(); see kSceneEntityScript.
    // ------------------------------------------------------------------

    /** @brief Host-qualified resolution: empty hostName → currently selected host. */
    [[nodiscard]] SceneHost *resolveHost(const std::string &hostName) const;

    /** @brief Root one script instance on a node (creates SceneObject lazily). */
    bool rootEntity(const std::string &hostName, const std::string &nodeId,
                    ssq::Object instance);
    /** @brief Remove a rooted script instance by its index in the binding list. */
    bool unrootEntityAt(const std::string &hostName, const std::string &nodeId,
                        int index);
    /** @brief Call cb(instance, index) for every rooted instance; returns count. */
    int forEachEntity(const std::string &hostName, const std::string &nodeId,
                      ssq::Object cb);
    /** @brief updateTransformsAll() + call update(dt) on every rooted instance. */
    void updateScripts(float dt);

    std::string currentHostName() const;
    [[nodiscard]] SceneNodeRef *getNodeRefAt(const std::string &hostName, const std::string &nodeId) const;
    [[nodiscard]] SceneNodeRef *getNodeRefByPathAt(const std::string &hostName, const std::string &path) const;

private:
    [[nodiscard]] SceneHost *ensureSelected(const std::string &preferredName = "");
    NodeDesc &currentParent();
    void pushOpen(NodeDesc d);
    bool buildComplete() const;

    [[nodiscard]] SceneObject *findSceneObjectById(uint32_t id) const;
    [[nodiscard]] SceneObject *findSceneObjectByPersistentId(eve::SceneObjectId id) const;
    [[nodiscard]] SceneObject *ensureSceneObject(SceneHost *host, SceneNode *node, const std::string &hostName);
    /** @brief Destroy bindings whose host/node no longer resolves; self-heal meta. */
    void pruneOrphanObjects();
    /** @brief Fire onDetach + destroy() and release every rooted instance. */
    void teardownBindings(SceneObject *obj);
    /** @brief Sync script instance hostName/nodeId fields from SceneObject::Meta. */
    void syncBindingRefs(SceneObject *obj);

    bool callMethod(HSQOBJECT inst, const char *name, float dt);
    bool callMethod0(HSQOBJECT inst, const char *name);
    bool callCallback(HSQOBJECT fn, HSQOBJECT inst, int index);
    void callEventCallback(const std::string &hostName, const std::string &action,
                           const std::string &nodeId, const std::string &parentId);

    SceneHost *selected_ = nullptr;
    HSQUIRRELVM vm_ = nullptr;
    std::unordered_map<std::string, HSQOBJECT> eventCbs_;

    std::vector<NodeDesc> openStack_;
    NodeDesc builtRoot_;
    bool hasBuiltRoot_ = false;
};

}  // namespace eve::scene
