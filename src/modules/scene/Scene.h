#pragma once

#include "common/Module.h"
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
    /** Remove every link on the node. */
    bool unlinkNodeAt(const std::string &hostName, const std::string &nodeId);
    /** Remove links of one kind ("renderable2d"|"renderable3d"|"physics2d"|...). */
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

    /** Reparent by id; empty parentId detaches. Cycle-safe. */
    bool setNodeParentAt(const std::string &hostName, const std::string &childId,
                         const std::string &parentId);
    /** Detach node from its parent (arena node stays; rebuild to delete). */
    bool removeNodeAt(const std::string &hostName, const std::string &nodeId);
    bool addChildAt(const std::string &hostName, const std::string &parentId,
                    const std::string &childId);
    bool removeChildAt(const std::string &hostName, const std::string &parentId,
                       const std::string &childId);

    bool setNodeQuaternionAt(const std::string &hostName, const std::string &nodeId,
                             float qx, float qy, float qz, float qw);
    std::vector<float> getNodeQuaternionAt(const std::string &hostName,
                                           const std::string &nodeId) const;
    /** Orient node so its local +Z axis points at (tx,ty,tz). */
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
     * Register a script callback for node lifecycle events on a host:
     * cb(action, nodeId, parentId) with action in
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
    /** Nearest node id hit by a world ray (nodes with bounds), or "". */
    std::string pickRayAt(const std::string &hostName, float ox, float oy, float oz,
                          float dx, float dy, float dz) const;
    /** Screen-space picking through a Camera3D (camera.screenToRay). */
    std::string pickScreenAt(const std::string &hostName, graphics::Camera3D *cam,
                             float screenX, float screenY, float viewW, float viewH) const;

    // --- Culling / spatial index ---
    /** Ids of nodes whose world AABB intersects the camera frustum. */
    std::vector<std::string> collectFrustumIdsAt(const std::string &hostName,
                                                 graphics::Camera3D *cam, float viewW,
                                                 float viewH) const;
    /** Insert every bounded node's world AABB into an octree (id = arena index). */
    bool syncSpatialIndexAt(const std::string &hostName, spatial::Octree *ot) const;
    /** Map a spatial-index id (arena index) back to a node id. */
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

    // ------------------------------------------------------------------
    // Per-node script entities (eve.SceneEntity). Native primitives called by
    // script wrappers injected in expose(); see kSceneEntityScript.
    // ------------------------------------------------------------------

    /** Host-qualified resolution: empty hostName → currently selected host. */
    SceneHost *resolveHost(const std::string &hostName) const;

    /** Root one script instance on a node (creates SceneObject lazily). */
    bool rootEntity(const std::string &hostName, const std::string &nodeId,
                    ssq::Object instance);
    /** Remove a rooted script instance by its index in the binding list. */
    bool unrootEntityAt(const std::string &hostName, const std::string &nodeId,
                        int index);
    /** Call cb(instance, index) for every rooted instance; returns count. */
    int forEachEntity(const std::string &hostName, const std::string &nodeId,
                      ssq::Object cb);
    /** updateTransformsAll() + call update(dt) on every rooted instance. */
    void updateScripts(float dt);

    std::string currentHostName() const;
    SceneNodeRef *getNodeRefAt(const std::string &hostName,
                               const std::string &nodeId) const;
    SceneNodeRef *getNodeRefByPathAt(const std::string &hostName,
                                     const std::string &path) const;

private:
    SceneHost *ensureSelected(const std::string &preferredName = "");
    NodeDesc &currentParent();
    void pushOpen(NodeDesc d);
    bool buildComplete() const;

    SceneObject *findSceneObjectById(uint32_t id) const;
    SceneObject *ensureSceneObject(SceneHost *host, SceneNode *node,
                                   const std::string &hostName);
    /** Destroy bindings whose host/node no longer resolves; self-heal meta. */
    void pruneOrphanObjects();
    /** Fire onDetach + destroy() and release every rooted instance. */
    void teardownBindings(SceneObject *obj);
    /** Sync script instance hostName/nodeId fields from SceneObject::Meta. */
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
