#pragma once
#include "common/Export.h"


#include "common/Module.h"
#include "common/Result.h"
#include "common/ScriptComponentSnapshot.h"
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
class EVENGINE_API_PLATFORM Scene : public Module {
public:
    Module_REG(Scene);
    /** @brief Scene. */
    Scene();
    /** @brief Tears down script bindings and delayed-detach state. */
    ~Scene() override;

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
    /** @brief Link renderable 2 d at. */
    bool linkRenderable2DAt(const std::string &hostName, const std::string &nodeId,
                            graphics::Renderable2D *r);
    /** @brief Link renderable 3 d at. */
    bool linkRenderable3DAt(const std::string &hostName, const std::string &nodeId,
                            graphics::Renderable3D *r);
    /** @brief Link physics 2 d at. */
    bool linkPhysics2DAt(const std::string &hostName, const std::string &nodeId,
                         physics::Body *b, const std::string &mode);
    /** @brief Link physics 3 d at. */
    bool linkPhysics3DAt(const std::string &hostName, const std::string &nodeId,
                         physics::Body3D *b, const std::string &mode);
    /** @brief Link camera 3 d at. */
    bool linkCamera3DAt(const std::string &hostName, const std::string &nodeId,
                        graphics::Camera3D *c);
    /** @brief Link audio 3 d at. */
    bool linkAudio3DAt(const std::string &hostName, const std::string &nodeId,
                       audio::Source *s);
    /** @brief Remove every link on the node. */
    bool unlinkNodeAt(const std::string &hostName, const std::string &nodeId);
    /** @brief Remove links of one kind ("renderable2d"|"renderable3d"|"physics2d"|...). */
    bool unlinkNodeKindAt(const std::string &hostName, const std::string &nodeId,
                          const std::string &kind);
    /** @brief Link count at. */
    int linkCountAt(const std::string &hostName, const std::string &nodeId);

    /**
     * @brief Link a published script provider to a node as the "script" link kind.
     *
     * The provider is published through `eve.provider` first; this call only
     * attaches it. The node then drives the provider through
     * `onNodeTransform(...)`, and a retired provider generation stops being driven
     * and its link is dropped.
     *
     * @param hostName Host that owns the node.
     * @param nodeId Node to attach to.
     * @param providerName Name the script provider was published under.
     * @param mode "node" (node drives the provider) or "body" (the provider's
     *        `nodeTransform()` drives the node).
     * @return Success, or `NotFound` when the host, the node or the published
     *         provider is missing.
     * @cost One link insert plus one transform update of the host. Link-time only;
     *       the per-frame cost is the dispatch described in `scene/ScriptLinks.cpp`.
     * @remarks The link target is a process-lifetime record, never a script
     *          pointer: a retired generation must stay answerable until the scene
     *          has purged the link.
     */
    [[nodiscard]] Result<void> linkScriptAt(const std::string& hostName, const std::string& nodeId,
                                            const std::string& providerName, const std::string& mode);

    // --- Script-API completeness (host-scoped primitives; script wrappers in
    // kSceneEntityScript provide selected-host convenience + NodeRef forms) ---

    /** @brief Returns the node position at. */
    std::vector<float> getNodePositionAt(const std::string &hostName,
                                         const std::string &nodeId) const;
    /** @brief Returns the node rotation at. */
    std::vector<float> getNodeRotationAt(const std::string &hostName,
                                         const std::string &nodeId) const;
    /** @brief Returns the node scale at. */
    std::vector<float> getNodeScaleAt(const std::string &hostName,
                                      const std::string &nodeId) const;
    /** @brief Returns the node visible at. */
    bool getNodeVisibleAt(const std::string &hostName, const std::string &nodeId) const;
    /** @brief Returns the node world position at. */
    std::vector<float> getNodeWorldPositionAt(const std::string &hostName,
                                              const std::string &nodeId) const;
    /** @brief Returns the node world rotation at. */
    std::vector<float> getNodeWorldRotationAt(const std::string &hostName,
                                              const std::string &nodeId) const;
    /** @brief Returns the node world scale at. */
    std::vector<float> getNodeWorldScaleAt(const std::string &hostName,
                                           const std::string &nodeId) const;

    /** @brief Local to world at. */
    std::vector<float> localToWorldAt(const std::string &hostName,
                                      const std::string &nodeId, float x, float y,
                                      float z) const;
    /** @brief World to local at. */
    std::vector<float> worldToLocalAt(const std::string &hostName,
                                      const std::string &nodeId, float x, float y,
                                      float z) const;

    /** @brief Reparent by id; empty parentId detaches. Cycle-safe. */
    bool setNodeParentAt(const std::string &hostName, const std::string &childId,
                         const std::string &parentId);
    /** @brief Detach node from its parent (arena node stays; rebuild to delete). */
    bool removeNodeAt(const std::string &hostName, const std::string &nodeId);
    /** @brief Adds child at. */
    bool addChildAt(const std::string &hostName, const std::string &parentId,
                    const std::string &childId);
    /** @brief Removes child at. */
    bool removeChildAt(const std::string &hostName, const std::string &parentId,
                       const std::string &childId);

    /** @brief Sets the node quaternion at. */
    bool setNodeQuaternionAt(const std::string &hostName, const std::string &nodeId,
                             float qx, float qy, float qz, float qw);
    /** @brief Returns the node quaternion at. */
    std::vector<float> getNodeQuaternionAt(const std::string &hostName,
                                           const std::string &nodeId) const;
    /** @brief Orient node so its local +Z axis points at (tx,ty,tz). */
    bool setNodeLookAtAt(const std::string &hostName, const std::string &nodeId,
                         float tx, float ty, float tz);

    /** @brief Adds node tag at. */
    bool addNodeTagAt(const std::string &hostName, const std::string &nodeId,
                      const std::string &tag);
    /** @brief Removes node tag at. */
    bool removeNodeTagAt(const std::string &hostName, const std::string &nodeId,
                         const std::string &tag);
    /** @brief True when node tag at. */
    bool hasNodeTagAt(const std::string &hostName, const std::string &nodeId,
                      const std::string &tag) const;
    /** @brief Returns the node tags at. */
    std::vector<std::string> getNodeTagsAt(const std::string &hostName,
                                           const std::string &nodeId) const;
    /** @brief Collect ids by tag at. */
    std::vector<std::string> collectIdsByTagAt(const std::string &hostName,
                                               const std::string &tag) const;
    /** @brief Sets the node layer at. */
    bool setNodeLayerAt(const std::string &hostName, const std::string &nodeId, int layer);
    /** @brief Returns the node layer at. */
    int getNodeLayerAt(const std::string &hostName, const std::string &nodeId) const;

    /**
     * @brief Register a script callback for node lifecycle events on a host:
     * The callback receives action, nodeId and parentId; action is one of
     * {"node_added","node_removed","node_moved","node_changed"}.
     * Pass null cb to clear. Rooted for the process lifetime.
     */
    bool setNodeEventHandlerAt(const std::string &hostName, ssq::Object cb);

    // --- Bounds (picking / culling / spatial index) ---
    /** @brief Sets the node bounds at. */
    bool setNodeBoundsAt(const std::string &hostName, const std::string &nodeId,
                         float minX, float minY, float minZ, float maxX, float maxY,
                         float maxZ);
    /** @brief True when node bounds at. */
    bool hasNodeBoundsAt(const std::string &hostName, const std::string &nodeId) const;
    /** @brief Returns the node bounds at. */
    std::vector<float> getNodeBoundsAt(const std::string &hostName,
                                       const std::string &nodeId) const;

    // --- Serialization (SceneHost ↔ JSON) ---
    /** @brief Serialize host at. */
    std::string serializeHostAt(const std::string &hostName) const;
    /** @brief Deserialize host at. */
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
    /**
     * @brief Apply PcgScenePlayer frustum visibility to bounded nodes carrying a tag.
     * @param hostName Scene host to mutate.
     * @param cam Borrowed camera used only during this call.
     * @param viewW Positive viewport width.
     * @param viewH Positive viewport height.
     * @param tag Terrain classification tag, such as `terrain` or `mesh-terrain`.
     * @return Number of visibility values changed.
     * @ownership The caller retains the camera; this function retains no pointer.
     * @lifetime Camera must remain valid for the duration of the call.
     */
    [[nodiscard]] Result<int> applyPcgTerrainCullingAt(const std::string &hostName,
        graphics::Camera3D *cam,float viewW,float viewH,const std::string &tag);
    /** @brief Insert every bounded node's world AABB into an octree (id = arena index). */
    bool syncSpatialIndexAt(const std::string &hostName, spatial::Octree *ot) const;
    /** @brief Map a spatial-index id (arena index) back to a node id. */
    std::string nodeIdFromSpatialIdAt(const std::string &hostName, int index) const;

    // --- Query / traverse (current host; script-friendly, id-based) ---
    /** @brief True when node. */
    bool hasNode(const std::string &id);
    /** @brief Returns the node count. */
    int getNodeCount();
    /** @brief Returns the root id. */
    std::string getRootId();
    /** @brief Returns the parent id. */
    std::string getParentId(const std::string &id);
    /** @brief Returns the child count. */
    int getChildCount(const std::string &id);
    /** @brief Returns the child id at. */
    std::string getChildIdAt(const std::string &parentId, int childOrdinal);
    /** @brief Finds id by name. */
    std::string findIdByName(const std::string &name);
    /** @brief Finds id by path. */
    std::string findIdByPath(const std::string &path);
    /** @brief Returns the node path. */
    std::string getNodePath(const std::string &id);
    /** @brief True when ancestor. */
    bool isAncestor(const std::string &ancestorId, const std::string &nodeId);
    /** @brief True when descendant. */
    bool isDescendant(const std::string &nodeId, const std::string &ancestorId);
    /** @brief Collect ids. */
    std::vector<std::string> collectIds();
    /** @brief Collect ids from. */
    std::vector<std::string> collectIdsFrom(const std::string &id);
    /** @brief Collect ids by name. */
    std::vector<std::string> collectIdsByName(const std::string &name);
    /** @brief Collect ids visible. */
    std::vector<std::string> collectIdsVisible(bool visible);
    /** @brief Collect child ids. */
    std::vector<std::string> collectChildIds(const std::string &parentId);
    /** @brief DFS order of ids under root (same as collectIds). Kept for script naming clarity. */
    std::vector<std::string> walkDepthFirstIds();
    /** @brief Walk breadth first ids. */
    std::vector<std::string> walkBreadthFirstIds();

    // --- Imperative builder (script / isomorphic to UI) ---
    /** @brief Begins build. */
    void beginBuild();
    /** @brief Begins node. */
    void beginNode(const std::string &id, const std::string &name = "");
    /** @brief Begins group. */
    void beginGroup(const std::string &id = "");
    /** @brief Ends . */
    void end();
    /** @brief Adds node. */
    void addNode(const std::string &id, const std::string &name = "");
    /** @brief Sets the build position. */
    void setBuildPosition(float x, float y, float z = 0.f);
    /** @brief Sets the build rotation. */
    void setBuildRotation(float yaw, float pitch = 0.f, float roll = 0.f);
    /** @brief Sets the build scale. */
    void setBuildScale(float sx, float sy, float sz = 1.f);
    /** @brief Sets the build space. */
    void setBuildSpace(const std::string &space);
    /** @brief Sets the build visible. */
    void setBuildVisible(bool visible);
    /** @brief Mounts build. */
    bool mountBuild();
    /** @brief Mounts build as. */
    bool mountBuildAs(const std::string &name);
    /** @brief Remount build as. */
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

    /**
     * @brief 取某个节点上挂着的脚本实体实例（`eve.SceneEntity` 子类实例）的**原生**快照。
     *
     * `forEachEntity` 要一个脚本闭包，原生工具（编辑器 inspector、MCP、诊断）拿不到实例；这条
     * 是同一份数据（`SceneObject::ScriptBindings::instances`）的原生只读入口，用于把"场景对象
     * ↔ 脚本实体 ↔ 引擎侧组件数据"这三段接起来（方案文档 §9.72）。
     *
     * @param hostName 宿主名；空串表示当前选中的宿主。
     * @param nodeId 节点在该宿主内的稳定 id。
     * @return 实例数组；宿主/节点不存在或没有挂脚本时返回**空数组**（不是错误——节点本来就可以
     *         没有脚本）。
     * @ownership 返回的每个 `ssq::Object` 各自持有一次引用（`sq_addref`），调用方可以跨栈使用，
     *            析构时自动释放。
     * @lifetime 实例本身由场景的 rooting 保活，直到 `unrootEntityAt` 或节点/宿主销毁；这里的
     *           返回值只保证那之后不再可用。
     * @thread 场景/主线程。
     * @cost 与节点上挂的脚本实体数成正比（一次拷贝 + 每个一次 addref）；供工具与诊断使用，不在
     *       逐帧路径上。
     */
    [[nodiscard]] std::vector<ssq::Object> scriptEntitiesAt(const std::string& hostName,
                                                            const std::string& nodeId) const;

    /**
     * @brief 一个节点上所有脚本实体的**已登记**组件状态（编辑器/工具面）。
     *
     * 与 `scriptEntitiesAt` 的区别是**不把 `ssq::Object` 交给调用方**：编辑器组（EVEditors）
     * 只是读一次组件数据，不该为此把 VM 类型拉进自己的链接单元——那会让 EVEditors.dll 成为
     * Squirrel 静态库的又一个导出者，与同样静态链接 Squirrel 的测试可执行文件撞成 LNK2005。
     * VM 侧的取数留在本模块（EVPlatform 本来就用 Squirrel）。
     *
     * @return 按 (名字, 版本) 升序的状态数组；节点上没有脚本实体（或场景不可用）时为空。
     * @cost 与节点上的脚本实体数成正比（一次拷贝 + 每个一次 addref）；工具/诊断路径用，不在逐帧路径上。
     */
    [[nodiscard]] std::vector<ScriptComponentInstanceState> scriptComponentStatesAt(const std::string& hostName,
                                                                                   const std::string& nodeId) const;

    /** @brief updateTransformsAll() + call update(dt) on every rooted instance. */
    void updateScripts(float dt);

    /**
     * @brief Queue a script instance for detach at the end of the current/next updateScripts.
     *
     * Safe to call from inside `update(dt)` / `onEnable` / `onDisable`. Immediate
     * `detachEntityAt` remains available for composition roots outside update.
     * @param hostName Host name; empty uses the selected host.
     * @param nodeId Stable node id.
     * @param instance Rooted `eve.SceneEntity` instance to detach.
     * @return Applied when queued (or already pending); NotFound / InvalidArgument on miss.
     * @cost O(bindings on the node) to locate the instance; flush is O(queued).
     */
    [[nodiscard]] eve::Result<void> scheduleDetachEntityAt(const std::string &hostName,
                                                           const std::string &nodeId,
                                                           ssq::Object instance);
    /** @brief Drain the delayed-detach queue (called automatically by updateScripts). */
    void flushDelayedDetaches();

    /** @brief Current host name. */
    std::string currentHostName() const;
    /** @brief Returns the node ref at. */
    [[nodiscard]] SceneNodeRef *getNodeRefAt(const std::string &hostName, const std::string &nodeId) const;
    /** @brief Returns the node ref by path at. */
    [[nodiscard]] SceneNodeRef *getNodeRefByPathAt(const std::string &hostName, const std::string &path) const;

private:
    struct DelayedDetach {
        std::string hostName;
        std::string nodeId;
        HSQOBJECT instance{};
    };
    [[nodiscard]] SceneHost *ensureSelected(const std::string &preferredName = "");
    NodeDesc &currentParent();
    /**
     * @brief Node that setBuildPosition/Rotation/Scale configure.
     *
     * The builder's setBuild* calls target the node created most recently, whether
     * by `beginNode`/`beginGroup` (still open) or by `addNode` (last child of the
     * open parent). Targeting only the open node silently moved the *parent* for the
     * documented `addNode(...); setBuildPosition(...)` order.
     */
    NodeDesc& buildTarget();
    /** @brief Which node buildTarget() resolves to. */
    enum class BuildCursor { None, OpenNode, LastAddedChild };
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
    std::vector<DelayedDetach> delayedDetaches_;
    bool flushingDelayedDetaches_ = false;

    std::vector<NodeDesc> openStack_;
    NodeDesc builtRoot_;
    bool hasBuiltRoot_ = false;
    BuildCursor           buildCursor_  = BuildCursor::None;
};

}  // namespace eve::scene
