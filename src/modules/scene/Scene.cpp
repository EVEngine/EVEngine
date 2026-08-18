#include "scene/Scene.h"

#include "scene/TransformSystem.h"

#include "graphics/ClipSpace.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "spatial/Octree.h"

#ifdef EVENGINE_SCENE_JSON
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#endif

#include <simplesquirrel/simplesquirrel.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstring>
#include <limits>
#include <sstream>
#include <utility>
#include <stdexcept>

namespace eve::scene {

Module_IMPL(Scene, new Scene());

namespace {

SceneHost *findHostByName(const std::string &name) {
    if (name.empty()) return nullptr;
    if (ecs::current()->getManager<SceneHost>() == nullptr) return nullptr;
    auto view = ecs::View<SceneHost, SceneHost::Meta>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta] = *it;
        if (meta->entity && meta->name == name) return meta->entity;
    }
    return nullptr;
}

SceneHost *findHostByOwnerId(uint32_t ownerId) {
    if (ownerId == 0) return nullptr;
    if (ecs::current()->getManager<SceneHost>() == nullptr) return nullptr;
    auto view = ecs::View<SceneHost, SceneHost::Meta>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta] = *it;
        if (meta->entity && meta->ownerId == ownerId) return meta->entity;
    }
    return nullptr;
}

/** Set a string field on a script instance (existing slot, newslot fallback). */
void setStringField(HSQUIRRELVM vm, HSQOBJECT inst, const char *name,
                    const std::string &value) {
    if (!vm || inst._type != OT_INSTANCE) return;
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, inst);
    sq_pushstring(vm, name, -1);
    sq_pushstring(vm, value.c_str(), -1);
    if (SQ_FAILED(sq_set(vm, -3))) {
        sq_settop(vm, top);
        sq_pushobject(vm, inst);
        sq_pushstring(vm, name, -1);
        sq_pushstring(vm, value.c_str(), -1);
        sq_newslot(vm, -3, SQFalse);
    }
    sq_settop(vm, top);
}

bool sameScriptObject(const HSQOBJECT &a, const HSQOBJECT &b) {
    return a._type == b._type &&
           std::memcmp(&a._unVal, &b._unVal, sizeof(a._unVal)) == 0;
}

LinkKind linkKindFromString(const std::string &kind) {
    if (kind == "renderable2d") return LinkKind::Renderable2D;
    if (kind == "renderable3d") return LinkKind::Renderable3D;
    if (kind == "physics2d") return LinkKind::Physics2D;
    if (kind == "physics3d") return LinkKind::Physics3D;
    if (kind == "camera3d") return LinkKind::Camera3D;
    if (kind == "audio3d") return LinkKind::Audio3D;
    return LinkKind::None;
}

int syncModeFromString(const std::string &mode) {
    return mode == "body" ? 1 : 0;
}

glm::quat nodeQuaternion(const SceneNode &n) {
    return glm::angleAxis(n.yaw, glm::vec3(0.f, 1.f, 0.f)) *
           glm::angleAxis(n.pitch, glm::vec3(1.f, 0.f, 0.f)) *
           glm::angleAxis(n.roll, glm::vec3(0.f, 0.f, 1.f));
}

/** Decompose a world matrix into position / euler(yaw,pitch,roll) / scale. */
void decomposeWorld(const glm::mat4 &w, glm::vec3 &pos, glm::vec3 &euler,
                    glm::vec3 &scale) {
    pos = glm::vec3(w[3]);
    glm::vec3 c0(w[0]), c1(w[1]), c2(w[2]);
    scale = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));
    if (scale.x > 1e-8f) c0 /= scale.x;
    if (scale.y > 1e-8f) c1 /= scale.y;
    if (scale.z > 1e-8f) c2 /= scale.z;
    glm::mat3 rot(c0, c1, c2);
    glm::quat q = glm::quat_cast(rot);
    glm::vec3 e = glm::eulerAngles(q);  // pitch(x), yaw(y), roll(z)
    euler = glm::vec3(e.y, e.x, e.z);
}

struct AABB3f {
    glm::vec3 min;
    glm::vec3 max;
};

/** World-space AABB of a node's local bounds (8 corners through world matrix). */
AABB3f worldBoundsOf(const SceneNode &n) {
    glm::vec3 lo(n.bminX, n.bminY, n.bminZ);
    glm::vec3 hi(n.bmaxX, n.bmaxY, n.bmaxZ);
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i) {
        glm::vec3 p((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y,
                    (i & 4) ? hi.z : lo.z);
        glm::vec4 w = n.world * glm::vec4(p, 1.f);
        for (int c = 0; c < 3; ++c) {
            mn[c] = std::min(mn[c], w[c]);
            mx[c] = std::max(mx[c], w[c]);
        }
    }
    return {mn, mx};
}

/** Slab ray-AABB intersection; fills entry/exit t (t0 <= t1). */
bool rayAABB(const glm::vec3 &o, const glm::vec3 &d, const AABB3f &b, float &t0,
             float &t1) {
    t0 = -std::numeric_limits<float>::max();
    t1 = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        const float oa = o[axis];
        const float da = d[axis];
        const float mn = b.min[axis];
        const float mx = b.max[axis];
        if (std::fabs(da) < 1e-12f) {
            if (oa < mn || oa > mx) return false;
        } else {
            const float inv = 1.f / da;
            float tA = (mn - oa) * inv;
            float tB = (mx - oa) * inv;
            if (tA > tB) std::swap(tA, tB);
            t0 = std::max(t0, tA);
            t1 = std::min(t1, tB);
            if (t0 > t1) return false;
        }
    }
    return true;
}

bool cornerInsideClip(const glm::mat4 &m, const glm::vec3 &p) {
    const glm::vec4 c = m * glm::vec4(p, 1.f);
    return c.w > 1e-8f && std::fabs(c.x) <= c.w && std::fabs(c.y) <= c.w &&
           c.z >= 0.f && c.z <= c.w;
}

/** Conservative AABB ↔ frustum overlap (AABB corners + frustum corners). */
bool aabbIntersectsFrustum(const glm::mat4 &clip, const glm::mat4 &invClip,
                           const AABB3f &b) {
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 p((i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y,
                          (i & 4) ? b.max.z : b.min.z);
        if (cornerInsideClip(clip, p)) return true;
    }
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 ndc((i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f,
                            (i & 4) ? 1.f : 0.f);
        const glm::vec4 w = invClip * glm::vec4(ndc, 1.f);
        if (std::fabs(w.w) < 1e-8f) continue;
        const glm::vec3 q = glm::vec3(w) / w.w;
        if (q.x >= b.min.x && q.x <= b.max.x && q.y >= b.min.y && q.y <= b.max.y &&
            q.z >= b.min.z && q.z <= b.max.z) {
            return true;
        }
    }
    return false;
}

// --- JSON serialization helpers ---

#ifdef EVENGINE_SCENE_JSON
Poco::JSON::Object::Ptr nodeToJson(const SceneHost::Tree &tree, const SceneNode *n) {
    Poco::JSON::Object::Ptr o = new Poco::JSON::Object;
    o->set("id", n->id);
    o->set("key", n->key);
    o->set("name", n->name);
    o->set("space", n->space);
    o->set("visible", n->visible);
    o->set("layer", n->layer);
    o->set("x", n->x);
    o->set("y", n->y);
    o->set("z", n->z);
    o->set("yaw", n->yaw);
    o->set("pitch", n->pitch);
    o->set("roll", n->roll);
    o->set("sx", n->sx);
    o->set("sy", n->sy);
    o->set("sz", n->sz);
    if (n->hasBounds) {
        o->set("bminX", n->bminX);
        o->set("bminY", n->bminY);
        o->set("bminZ", n->bminZ);
        o->set("bmaxX", n->bmaxX);
        o->set("bmaxY", n->bmaxY);
        o->set("bmaxZ", n->bmaxZ);
    }
    if (!n->tags.empty()) {
        Poco::JSON::Array::Ptr arr = new Poco::JSON::Array;
        for (const auto &t : n->tags) arr->add(t);
        o->set("tags", arr);
    }
    if (n->firstChild >= 0) {
        Poco::JSON::Array::Ptr kids = new Poco::JSON::Array;
        for (int c = n->firstChild; c >= 0;
             c = tree.nodes[size_t(c)].nextSibling) {
            kids->add(nodeToJson(tree, &tree.nodes[size_t(c)]));
        }
        o->set("children", kids);
    }
    return o;
}

NodeDesc nodeFromJson(const Poco::JSON::Object::Ptr &o) {
    NodeDesc d;
    d.id = o->optValue<std::string>("id", "");
    d.key = o->optValue<std::string>("key", d.id);
    d.name = o->optValue<std::string>("name", d.id);
    d.space = o->optValue<std::string>("space", "3d");
    d.visible = o->optValue<bool>("visible", true);
    d.layer = o->optValue<int>("layer", 0);
    d.x = float(o->optValue<double>("x", 0.0));
    d.y = float(o->optValue<double>("y", 0.0));
    d.z = float(o->optValue<double>("z", 0.0));
    d.yaw = float(o->optValue<double>("yaw", 0.0));
    d.pitch = float(o->optValue<double>("pitch", 0.0));
    d.roll = float(o->optValue<double>("roll", 0.0));
    d.sx = float(o->optValue<double>("sx", 1.0));
    d.sy = float(o->optValue<double>("sy", 1.0));
    d.sz = float(o->optValue<double>("sz", 1.0));
    if (o->has("bminX")) {
        d.bminX = float(o->optValue<double>("bminX", 0.0));
        d.bminY = float(o->optValue<double>("bminY", 0.0));
        d.bminZ = float(o->optValue<double>("bminZ", 0.0));
        d.bmaxX = float(o->optValue<double>("bmaxX", 0.0));
        d.bmaxY = float(o->optValue<double>("bmaxY", 0.0));
        d.bmaxZ = float(o->optValue<double>("bmaxZ", 0.0));
        d.hasBounds = true;
    }
    if (o->has("tags")) {
        Poco::JSON::Array::Ptr arr = o->getArray("tags");
        for (size_t i = 0; i < arr->size(); ++i) {
            d.tags.push_back(arr->getElement<std::string>(i));
        }
    }
    if (o->has("children")) {
        Poco::JSON::Array::Ptr kids = o->getArray("children");
        for (size_t i = 0; i < kids->size(); ++i) {
            d.children.push_back(nodeFromJson(kids->getObject(i)));
        }
    }
    return d;
}
#endif

/** Script base: `class X extends eve.SceneComponent { function build() { ... } }`. */
const char *kSceneComponentScript = R"SQ(
eve.SceneComponent <- class {
    hostName = ""
    dirty = true
    forceFull = false
    _scene = null
    _mounted = false

    constructor(sceneInstance = null) {
        _scene = sceneInstance
        hostName = ""
        dirty = true
        forceFull = false
        _mounted = false
    }

    function setScene(sceneInstance) { _scene = sceneInstance }

    function mountAs(name) {
        hostName = name
        dirty = true
        forceFull = true
        updateIfDirty()
    }

    function setState() { dirty = true }
    function markDirty() { dirty = true }
    function onMount() {}

    // Override: call this.scene().beginNode / addNode / end ...
    function build() {}

    function scene() {
        if (_scene != null) return _scene
        try {
            if (::scene != null) return ::scene
        } catch (e) {}
        _scene = ::eve.Scene()
        return _scene
    }

    function updateIfDirty() {
        if (!dirty) return false
        local s = scene()
        s.beginBuild()
        build()
        local name = hostName
        if (name == null || name == "") name = "default"
        if (forceFull) {
            s.mountBuildAs(name)
            forceFull = false
        } else {
            s.remountBuildAs(name)
        }
        if (!_mounted) {
            _mounted = true
            onMount()
        }
        dirty = false
        return true
    }

    function rebuild(force = false) {
        dirty = true
        forceFull = force
        return updateIfDirty()
    }
}
)SQ";

void injectSceneComponentClass(ssq::Table &eveTable) {
    HSQUIRRELVM vm = eveTable.getHandle();
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, kSceneComponentScript,
                                   static_cast<SQInteger>(std::strlen(kSceneComponentScript)),
                                   "SceneComponent.nut", SQTrue))) {
        sq_settop(vm, top);
        return;
    }
    sq_pushroottable(vm);
    sq_call(vm, 1, SQFalse, SQTrue);
    sq_settop(vm, top);
}

/**
 * Per-node script entities (eve.SceneEntity) + the eve.Scene / eve.SceneNodeRef
 * wrappers over native primitives. Injected by a post-ECS hook so eve.Entity
 * (and its create()/view machinery) already exists.
 */
const char *kSceneEntityScript = R"SQ(
// Per-node script entity base: nodes get gameplay logic through the script ECS.
eve.SceneEntity <- class extends eve.Entity {
    _scene = null
    hostName = ""
    nodeId = ""

    function scene() { return _scene }
    function node() {
        if (_scene == null) return null
        return _scene.getNodeRefAt(hostName, nodeId)
    }
    function onAttach() {}
    function onDetach() {}
    function update(dt) {}
}

// ---- eve.Scene: per-node entity API (script wrappers over native primitives) ----

eve.Scene["getNodeRef"] <- function(nodeId, hostName = null) {
    if (hostName == null) hostName = currentHostName()
    return getNodeRefAt(hostName, nodeId)
}

eve.Scene["getNodeRefByPath"] <- function(path) {
    return getNodeRefByPathAt(currentHostName(), path)
}

eve.Scene["attachEntity"] <- function(nodeId, cls) {
    return attachEntityAt(currentHostName(), nodeId, cls)
}

eve.Scene["attachEntityAt"] <- function(hostName, nodeId, cls) {
    if (typeof cls != "class") return null
    if (!eve.isSubclass(cls, eve.Entity)) return null
    local old = getEntityAt(hostName, nodeId, cls)
    if (old != null) detachEntityAt(hostName, nodeId, old)
    local inst = cls.create()
    inst._scene = this
    inst.hostName = hostName
    inst.nodeId = nodeId
    if (!rootEntity(hostName, nodeId, inst)) {
        inst.destroy()
        return null
    }
    inst.onAttach()
    return inst
}

eve.Scene["detachEntity"] <- function(nodeId, inst) {
    return detachEntityAt(currentHostName(), nodeId, inst)
}

eve.Scene["detachEntityAt"] <- function(hostName, nodeId, inst) {
    if (inst == null) return false
    local idx = -1
    forEachEntity(hostName, nodeId, function(e, index) {
        if (idx < 0 && e == inst) idx = index
    })
    if (idx < 0) return false
    inst.onDetach()
    inst.destroy()
    return unrootEntityAt(hostName, nodeId, idx)
}

eve.Scene["getEntity"] <- function(nodeId, cls) {
    return getEntityAt(currentHostName(), nodeId, cls)
}

eve.Scene["getEntityAt"] <- function(hostName, nodeId, cls) {
    local out = null
    forEachEntity(hostName, nodeId, function(e) {
        if (out == null && e instanceof cls) out = e
    })
    return out
}

eve.Scene["hasEntity"] <- function(nodeId, cls) {
    return getEntityAt(currentHostName(), nodeId, cls) != null
}

eve.Scene["hasEntityAt"] <- function(hostName, nodeId, cls) {
    return getEntityAt(hostName, nodeId, cls) != null
}

eve.Scene["entitiesOf"] <- function(nodeId) {
    return entitiesOfAt(currentHostName(), nodeId)
}

eve.Scene["entitiesOfAt"] <- function(hostName, nodeId) {
    local out = []
    forEachEntity(hostName, nodeId, function(e) { out.push(e) })
    return out
}

eve.Scene["update"] <- function(dt) {
    updateScripts(dt)
}

// ---- eve.Scene: generic link system (selected-host convenience) ----

eve.Scene["linkPhysics2D"] <- function(nodeId, body, mode = "node") {
    return linkPhysics2DAt(currentHostName(), nodeId, body, mode)
}
eve.Scene["linkPhysics3D"] <- function(nodeId, body, mode = "node") {
    return linkPhysics3DAt(currentHostName(), nodeId, body, mode)
}
eve.Scene["linkCamera3D"] <- function(nodeId, cam) {
    return linkCamera3DAt(currentHostName(), nodeId, cam)
}
eve.Scene["linkAudio3D"] <- function(nodeId, source) {
    return linkAudio3DAt(currentHostName(), nodeId, source)
}
eve.Scene["unlinkNodeKind"] <- function(nodeId, kind) {
    return unlinkNodeKindAt(currentHostName(), nodeId, kind)
}
eve.Scene["linkCount"] <- function(nodeId) {
    return linkCountAt(currentHostName(), nodeId)
}

// ---- eve.SceneNodeRef: node-level entity forwarding ----

eve.SceneNodeRef["attachEntity"] <- function(cls) {
    return getScene().attachEntityAt(getHostName(), getNodeId(), cls)
}
eve.SceneNodeRef["detachEntity"] <- function(inst) {
    return getScene().detachEntityAt(getHostName(), getNodeId(), inst)
}
eve.SceneNodeRef["getEntity"] <- function(cls) {
    return getScene().getEntityAt(getHostName(), getNodeId(), cls)
}
eve.SceneNodeRef["hasEntity"] <- function(cls) {
    return getScene().hasEntityAt(getHostName(), getNodeId(), cls)
}
eve.SceneNodeRef["entitiesOf"] <- function() {
    return getScene().entitiesOfAt(getHostName(), getNodeId())
}

// ---- eve.SceneNodeRef: generic link system ----

eve.SceneNodeRef["linkRenderable2D"] <- function(r) {
    return getScene().linkRenderable2DAt(getHostName(), getNodeId(), r)
}
eve.SceneNodeRef["linkRenderable3D"] <- function(r) {
    return getScene().linkRenderable3DAt(getHostName(), getNodeId(), r)
}
eve.SceneNodeRef["linkPhysics2D"] <- function(body, mode = "node") {
    return getScene().linkPhysics2DAt(getHostName(), getNodeId(), body, mode)
}
eve.SceneNodeRef["linkPhysics3D"] <- function(body, mode = "node") {
    return getScene().linkPhysics3DAt(getHostName(), getNodeId(), body, mode)
}
eve.SceneNodeRef["linkCamera3D"] <- function(cam) {
    return getScene().linkCamera3DAt(getHostName(), getNodeId(), cam)
}
eve.SceneNodeRef["linkAudio3D"] <- function(source) {
    return getScene().linkAudio3DAt(getHostName(), getNodeId(), source)
}
eve.SceneNodeRef["unlinkNode"] <- function() {
    return getScene().unlinkNodeAt(getHostName(), getNodeId())
}
eve.SceneNodeRef["unlinkNodeKind"] <- function(kind) {
    return getScene().unlinkNodeKindAt(getHostName(), getNodeId(), kind)
}
eve.SceneNodeRef["linkCount"] <- function() {
    return getScene().linkCountAt(getHostName(), getNodeId())
}

// ---- eve.Scene: script-API completeness (selected-host convenience) ----

eve.Scene["_requireNode"] <- function(nodeId) {
    if (!hasNode(nodeId)) throw "scene: node '" + nodeId + "' not found"
}
eve.Scene["getNodePosition"] <- function(nodeId) {
    _requireNode(nodeId)
    return getNodePositionAt(currentHostName(), nodeId)
}
eve.Scene["getNodeRotation"] <- function(nodeId) {
    _requireNode(nodeId)
    return getNodeRotationAt(currentHostName(), nodeId)
}
eve.Scene["getNodeScale"] <- function(nodeId) {
    _requireNode(nodeId)
    return getNodeScaleAt(currentHostName(), nodeId)
}
eve.Scene["getNodeVisible"] <- function(nodeId) {
    _requireNode(nodeId)
    return getNodeVisibleAt(currentHostName(), nodeId)
}
eve.Scene["getNodeWorldPosition"] <- function(nodeId) {
    _requireNode(nodeId)
    return getNodeWorldPositionAt(currentHostName(), nodeId)
}
eve.Scene["getNodeWorldRotation"] <- function(nodeId) {
    _requireNode(nodeId)
    return getNodeWorldRotationAt(currentHostName(), nodeId)
}
eve.Scene["getNodeWorldScale"] <- function(nodeId) {
    _requireNode(nodeId)
    return getNodeWorldScaleAt(currentHostName(), nodeId)
}
eve.Scene["localToWorld"] <- function(nodeId, x, y, z) {
    _requireNode(nodeId)
    return localToWorldAt(currentHostName(), nodeId, x, y, z)
}
eve.Scene["worldToLocal"] <- function(nodeId, x, y, z) {
    _requireNode(nodeId)
    return worldToLocalAt(currentHostName(), nodeId, x, y, z)
}
eve.Scene["setNodeParent"] <- function(nodeId, parentId) { return setNodeParentAt(currentHostName(), nodeId, parentId) }
eve.Scene["removeNode"] <- function(nodeId) { return removeNodeAt(currentHostName(), nodeId) }
eve.Scene["addNodeChild"] <- function(parentId, childId) { return addChildAt(currentHostName(), parentId, childId) }
eve.Scene["removeNodeChild"] <- function(parentId, childId) { return removeChildAt(currentHostName(), parentId, childId) }
eve.Scene["setNodeQuaternion"] <- function(nodeId, qx, qy, qz, qw) { return setNodeQuaternionAt(currentHostName(), nodeId, qx, qy, qz, qw) }
eve.Scene["getNodeQuaternion"] <- function(nodeId) {
    _requireNode(nodeId)
    return getNodeQuaternionAt(currentHostName(), nodeId)
}
eve.Scene["setNodeLookAt"] <- function(nodeId, tx, ty, tz) { return setNodeLookAtAt(currentHostName(), nodeId, tx, ty, tz) }
eve.Scene["addNodeTag"] <- function(nodeId, tag) { return addNodeTagAt(currentHostName(), nodeId, tag) }
eve.Scene["removeNodeTag"] <- function(nodeId, tag) { return removeNodeTagAt(currentHostName(), nodeId, tag) }
eve.Scene["hasNodeTag"] <- function(nodeId, tag) { return hasNodeTagAt(currentHostName(), nodeId, tag) }
eve.Scene["getNodeTags"] <- function(nodeId) {
    _requireNode(nodeId)
    return getNodeTagsAt(currentHostName(), nodeId)
}
eve.Scene["collectIdsByTag"] <- function(tag) { return collectIdsByTagAt(currentHostName(), tag) }
eve.Scene["setNodeLayer"] <- function(nodeId, layer) { return setNodeLayerAt(currentHostName(), nodeId, layer) }
eve.Scene["getNodeLayer"] <- function(nodeId) {
    _requireNode(nodeId)
    return getNodeLayerAt(currentHostName(), nodeId)
}
eve.Scene["setNodeEventHandler"] <- function(cb) {
    return setNodeEventHandlerAt(currentHostName(), cb)
}

// ---- eve.SceneNodeRef: script-API completeness forwarding ----

eve.SceneNodeRef["_requireValid"] <- function() {
    if (!isValid()) throw "scene: node not found (" + getHostName() + "/" + getNodeId() + ")"
}
eve.SceneNodeRef["localToWorld"] <- function(x, y, z) {
    _requireValid()
    return getScene().localToWorldAt(getHostName(), getNodeId(), x, y, z)
}
eve.SceneNodeRef["worldToLocal"] <- function(x, y, z) {
    _requireValid()
    return getScene().worldToLocalAt(getHostName(), getNodeId(), x, y, z)
}
eve.SceneNodeRef["setParent"] <- function(parentId) { return getScene().setNodeParentAt(getHostName(), getNodeId(), parentId) }
eve.SceneNodeRef["removeNode"] <- function() { return getScene().removeNodeAt(getHostName(), getNodeId()) }
eve.SceneNodeRef["setQuaternion"] <- function(qx, qy, qz, qw) { return getScene().setNodeQuaternionAt(getHostName(), getNodeId(), qx, qy, qz, qw) }
eve.SceneNodeRef["getQuaternion"] <- function() {
    _requireValid()
    return getScene().getNodeQuaternionAt(getHostName(), getNodeId())
}
eve.SceneNodeRef["lookAt"] <- function(tx, ty, tz) { return getScene().setNodeLookAtAt(getHostName(), getNodeId(), tx, ty, tz) }
eve.SceneNodeRef["addTag"] <- function(tag) { return getScene().addNodeTagAt(getHostName(), getNodeId(), tag) }
eve.SceneNodeRef["removeTag"] <- function(tag) { return getScene().removeNodeTagAt(getHostName(), getNodeId(), tag) }
eve.SceneNodeRef["hasTag"] <- function(tag) { return getScene().hasNodeTagAt(getHostName(), getNodeId(), tag) }
eve.SceneNodeRef["getTags"] <- function() {
    _requireValid()
    return getScene().getNodeTagsAt(getHostName(), getNodeId())
}
eve.SceneNodeRef["setLayer"] <- function(layer) { return getScene().setNodeLayerAt(getHostName(), getNodeId(), layer) }
eve.SceneNodeRef["getLayer"] <- function() {
    _requireValid()
    return getScene().getNodeLayerAt(getHostName(), getNodeId())
}

// ---- bounds / serialization / picking / culling ----

eve.Scene["setNodeBounds"] <- function(nodeId, minX, minY, minZ, maxX, maxY, maxZ) {
    return setNodeBoundsAt(currentHostName(), nodeId, minX, minY, minZ, maxX, maxY, maxZ)
}
eve.Scene["hasNodeBounds"] <- function(nodeId) {
    return hasNodeBoundsAt(currentHostName(), nodeId)
}
eve.Scene["getNodeBounds"] <- function(nodeId) {
    _requireNode(nodeId)
    return getNodeBoundsAt(currentHostName(), nodeId)
}
eve.Scene["serializeHost"] <- function() {
    return serializeHostAt(currentHostName())
}
eve.Scene["deserializeHost"] <- function(json) {
    return deserializeHostAt(currentHostName(), json)
}
eve.Scene["pickRay"] <- function(ox, oy, oz, dx, dy, dz) {
    return pickRayAt(currentHostName(), ox, oy, oz, dx, dy, dz)
}
eve.Scene["pickScreen"] <- function(cam, screenX, screenY, viewW, viewH) {
    return pickScreenAt(currentHostName(), cam, screenX, screenY, viewW, viewH)
}
eve.Scene["collectFrustumIds"] <- function(cam, viewW, viewH) {
    return collectFrustumIdsAt(currentHostName(), cam, viewW, viewH)
}
eve.Scene["syncSpatialIndex"] <- function(octree) {
    return syncSpatialIndexAt(currentHostName(), octree)
}
eve.Scene["nodeIdFromSpatialId"] <- function(index) {
    return nodeIdFromSpatialIdAt(currentHostName(), index)
}

eve.SceneNodeRef["setBounds"] <- function(minX, minY, minZ, maxX, maxY, maxZ) {
    return getScene().setNodeBoundsAt(getHostName(), getNodeId(), minX, minY, minZ, maxX, maxY, maxZ)
}
eve.SceneNodeRef["hasBounds"] <- function() {
    return getScene().hasNodeBoundsAt(getHostName(), getNodeId())
}
eve.SceneNodeRef["getBounds"] <- function() {
    _requireValid()
    return getScene().getNodeBoundsAt(getHostName(), getNodeId())
}
)SQ";

void injectSceneEntityScript(ssq::Table &eveTable) {
    HSQUIRRELVM vm = eveTable.getHandle();
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, kSceneEntityScript,
                                   static_cast<SQInteger>(std::strlen(kSceneEntityScript)),
                                   "SceneEntity.nut", SQTrue))) {
        sq_settop(vm, top);
        return;
    }
    sq_pushroottable(vm);
    sq_call(vm, 1, SQFalse, SQTrue);
    sq_settop(vm, top);
}

bool g_sceneEntityHookRegistered = false;

}  // namespace

SceneHost *Scene::ensureSelected(const std::string &preferredName) {
    if (selected_) return selected_;
    if (!preferredName.empty()) {
        selected_ = findHostByName(preferredName);
        if (selected_) return selected_;
        selected_ = SceneHost::createHost(preferredName);
        return selected_;
    }
    selected_ = SceneHost::createHost("default");
    return selected_;
}

SceneHost *Scene::mountAs(const std::string &name, NodeDesc root) {
    SceneHost *h = findHostByName(name);
    if (!h) h = SceneHost::createHost(name);
    h->setTree(std::move(root));
    TransformSystem::updateHost(h);
    pruneOrphanObjects();
    selected_ = h;
    return h;
}

SceneHost *Scene::mount(NodeDesc root) { return mountAs("default", std::move(root)); }

SceneHost *Scene::remount(NodeDesc root) {
    SceneHost *h = ensureSelected("default");
    h->setTree(std::move(root));
    TransformSystem::updateHost(h);
    pruneOrphanObjects();
    return h;
}

SceneHost *Scene::remountReconcile(NodeDesc root) {
    SceneHost *h = ensureSelected("default");
    h->setTreeReconcile(std::move(root));
    TransformSystem::updateHost(h);
    pruneOrphanObjects();
    return h;
}

SceneHost *Scene::remountAs(const std::string &name, NodeDesc root) {
    return mountAs(name, std::move(root));
}

bool Scene::select(const std::string &name) {
    SceneHost *h = findHostByName(name);
    if (!h) return false;
    selected_ = h;
    return true;
}

SceneHost *Scene::findHost(const std::string &name) const { return findHostByName(name); }

SceneHost *Scene::findHostByOwner(uint32_t ownerId) const { return findHostByOwnerId(ownerId); }

void Scene::bindOwner(uint32_t ownerId) {
    SceneHost *h = ensureSelected();
    h->setOwnerId(ownerId);
}

void Scene::setHostVisible(bool visible) {
    if (auto *h = ensureSelected()) h->setVisible(visible);
}

void Scene::setHostLayer(int layer) {
    if (auto *h = ensureSelected()) h->setLayer(layer);
}

void Scene::updateTransforms() {
    if (selected_) TransformSystem::updateHost(selected_);
    else TransformSystem::updateAll();
}

void Scene::updateTransformsAll() { TransformSystem::updateAll(); }

bool Scene::setNodePosition(const std::string &id, float x, float y, float z) {
    SceneHost *h = selected_;
    if (!h) return false;
    SceneNode *n = h->findById(id);
    if (!n) return false;
    n->x = x;
    n->y = y;
    n->z = z;
    h->markSubtreeDirtyById(id);
    return true;
}

bool Scene::setNodeRotation(const std::string &id, float yaw, float pitch, float roll) {
    SceneHost *h = selected_;
    if (!h) return false;
    SceneNode *n = h->findById(id);
    if (!n) return false;
    n->yaw = yaw;
    n->pitch = pitch;
    n->roll = roll;
    h->markSubtreeDirtyById(id);
    return true;
}

bool Scene::setNodeScale(const std::string &id, float sx, float sy, float sz) {
    SceneHost *h = selected_;
    if (!h) return false;
    SceneNode *n = h->findById(id);
    if (!n) return false;
    n->sx = sx;
    n->sy = sy;
    n->sz = sz;
    h->markSubtreeDirtyById(id);
    return true;
}

bool Scene::setNodeVisible(const std::string &id, bool visible) {
    SceneHost *h = selected_;
    if (!h) return false;
    SceneNode *n = h->findById(id);
    if (!n) return false;
    n->visible = visible;
    h->markSubtreeDirtyById(id);  // resync renderable visibility via links
    return true;
}

bool Scene::linkRenderable2D(const std::string &nodeId, graphics::Renderable2D *r) {
    return linkRenderable2DAt(currentHostName(), nodeId, r);
}

bool Scene::linkRenderable3D(const std::string &nodeId, graphics::Renderable3D *r) {
    return linkRenderable3DAt(currentHostName(), nodeId, r);
}

bool Scene::unlinkNode(const std::string &nodeId) {
    return unlinkNodeAt(currentHostName(), nodeId);
}

// ---------------------------------------------------------------------------
// Generic link system (host-scoped primitives)
// ---------------------------------------------------------------------------

bool Scene::linkRenderable2DAt(const std::string &hostName, const std::string &nodeId,
                               graphics::Renderable2D *r) {
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    if (!h->linkRenderable2D(nodeId, r)) return false;
    TransformSystem::updateHost(h);
    return true;
}

bool Scene::linkRenderable3DAt(const std::string &hostName, const std::string &nodeId,
                               graphics::Renderable3D *r) {
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    if (!h->linkRenderable3D(nodeId, r)) return false;
    TransformSystem::updateHost(h);
    return true;
}

bool Scene::linkPhysics2DAt(const std::string &hostName, const std::string &nodeId,
                            physics::Body *b, const std::string &mode) {
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    if (!h->linkPhysics2D(nodeId, b, syncModeFromString(mode))) return false;
    TransformSystem::updateHost(h);
    return true;
}

bool Scene::linkPhysics3DAt(const std::string &hostName, const std::string &nodeId,
                            physics::Body3D *b, const std::string &mode) {
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    if (!h->linkPhysics3D(nodeId, b, syncModeFromString(mode))) return false;
    TransformSystem::updateHost(h);
    return true;
}

bool Scene::linkCamera3DAt(const std::string &hostName, const std::string &nodeId,
                           graphics::Camera3D *c) {
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    if (!h->linkCamera3D(nodeId, c)) return false;
    TransformSystem::updateHost(h);
    return true;
}

bool Scene::linkAudio3DAt(const std::string &hostName, const std::string &nodeId,
                          audio::Source *s) {
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    if (!h->linkAudio3D(nodeId, s)) return false;
    TransformSystem::updateHost(h);
    return true;
}

bool Scene::unlinkNodeAt(const std::string &hostName, const std::string &nodeId) {
    SceneHost *h = resolveHost(hostName);
    return h ? h->unlink(nodeId) : false;
}

bool Scene::unlinkNodeKindAt(const std::string &hostName, const std::string &nodeId,
                             const std::string &kind) {
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    const LinkKind k = linkKindFromString(kind);
    if (k == LinkKind::None) return false;
    const bool removed = h->unlink(nodeId, k);
    if (removed) TransformSystem::updateHost(h);
    return removed;
}

int Scene::linkCountAt(const std::string &hostName, const std::string &nodeId) {
    SceneHost *h = resolveHost(hostName);
    return h ? h->linkCount(nodeId) : 0;
}

// ---------------------------------------------------------------------------
// Script-API completeness: transform getters / hierarchy / math / tags / layer
// ---------------------------------------------------------------------------

std::vector<float> Scene::getNodePositionAt(const std::string &hostName,
                                            const std::string &nodeId) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n) return {0.f, 0.f, 0.f};
    return {n->x, n->y, n->z};
}

std::vector<float> Scene::getNodeRotationAt(const std::string &hostName,
                                            const std::string &nodeId) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n) return {0.f, 0.f, 0.f};
    return {n->yaw, n->pitch, n->roll};
}

std::vector<float> Scene::getNodeScaleAt(const std::string &hostName,
                                         const std::string &nodeId) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n) return {0.f, 0.f, 0.f};
    return {n->sx, n->sy, n->sz};
}

bool Scene::getNodeVisibleAt(const std::string &hostName,
                             const std::string &nodeId) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    return n ? n->visible : false;
}

std::vector<float> Scene::getNodeWorldPositionAt(const std::string &hostName,
                                                 const std::string &nodeId) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n) return {0.f, 0.f, 0.f};
    return {n->world[3][0], n->world[3][1], n->world[3][2]};
}

std::vector<float> Scene::getNodeWorldRotationAt(const std::string &hostName,
                                                 const std::string &nodeId) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n) return {0.f, 0.f, 0.f};
    glm::vec3 pos, euler, scale;
    decomposeWorld(n->world, pos, euler, scale);
    return {euler.x, euler.y, euler.z};
}

std::vector<float> Scene::getNodeWorldScaleAt(const std::string &hostName,
                                              const std::string &nodeId) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n) return {0.f, 0.f, 0.f};
    glm::vec3 pos, euler, scale;
    decomposeWorld(n->world, pos, euler, scale);
    return {scale.x, scale.y, scale.z};
}

std::vector<float> Scene::localToWorldAt(const std::string &hostName,
                                         const std::string &nodeId, float x, float y,
                                         float z) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n) return {x, y, z};
    glm::vec4 p = n->world * glm::vec4(x, y, z, 1.f);
    return {p.x, p.y, p.z};
}

std::vector<float> Scene::worldToLocalAt(const std::string &hostName,
                                         const std::string &nodeId, float x, float y,
                                         float z) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n) return {x, y, z};
    glm::vec4 p = glm::inverse(n->world) * glm::vec4(x, y, z, 1.f);
    return {p.x, p.y, p.z};
}

bool Scene::setNodeParentAt(const std::string &hostName, const std::string &childId,
                            const std::string &parentId) {
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    const bool ok = h->setParentById(childId, parentId);
    if (ok) TransformSystem::updateHost(h);
    return ok;
}

bool Scene::removeNodeAt(const std::string &hostName, const std::string &nodeId) {
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    const bool ok = h->setParentById(nodeId, "");
    if (ok) TransformSystem::updateHost(h);
    return ok;
}

bool Scene::addChildAt(const std::string &hostName, const std::string &parentId,
                       const std::string &childId) {
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    const bool ok = h->setParentById(childId, parentId);
    if (ok) TransformSystem::updateHost(h);
    return ok;
}

bool Scene::removeChildAt(const std::string &hostName, const std::string &parentId,
                          const std::string &childId) {
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    const bool ok = h->removeChildById(parentId, childId);
    if (ok) TransformSystem::updateHost(h);
    return ok;
}

bool Scene::setNodeQuaternionAt(const std::string &hostName, const std::string &nodeId,
                                float qx, float qy, float qz, float qw) {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!h || !n) return false;
    glm::quat q(qw, qx, qy, qz);
    glm::vec3 e = glm::eulerAngles(q);  // pitch(x), yaw(y), roll(z)
    n->yaw = e.y;
    n->pitch = e.x;
    n->roll = e.z;
    h->markSubtreeDirtyById(nodeId);
    return true;
}

std::vector<float> Scene::getNodeQuaternionAt(const std::string &hostName,
                                              const std::string &nodeId) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n) return {0.f, 0.f, 0.f, 1.f};
    glm::quat q = nodeQuaternion(*n);
    return {q.x, q.y, q.z, q.w};
}

bool Scene::setNodeLookAtAt(const std::string &hostName, const std::string &nodeId,
                            float tx, float ty, float tz) {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!h || !n) return false;
    glm::vec3 pos(n->x, n->y, n->z);
    glm::vec3 target(tx, ty, tz);
    glm::vec3 f = target - pos;
    if (glm::dot(f, f) < 1e-8f) return false;
    f = glm::normalize(f);
    glm::vec3 up(0.f, 1.f, 0.f);
    glm::vec3 right = glm::normalize(glm::cross(up, f));
    if (glm::dot(right, right) < 1e-8f) right = glm::vec3(1.f, 0.f, 0.f);  // f ∥ up
    glm::vec3 up2 = glm::normalize(glm::cross(f, right));
    glm::mat3 rot(right, up2, f);
    glm::quat q = glm::quat_cast(rot);
    glm::vec3 e = glm::eulerAngles(q);
    n->yaw = e.y;
    n->pitch = e.x;
    n->roll = e.z;
    h->markSubtreeDirtyById(nodeId);
    return true;
}

bool Scene::addNodeTagAt(const std::string &hostName, const std::string &nodeId,
                         const std::string &tag) {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    return h ? h->addTag(n, tag) : false;
}

bool Scene::removeNodeTagAt(const std::string &hostName, const std::string &nodeId,
                            const std::string &tag) {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    return h ? h->removeTag(n, tag) : false;
}

bool Scene::hasNodeTagAt(const std::string &hostName, const std::string &nodeId,
                         const std::string &tag) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    return h ? h->hasTag(n, tag) : false;
}

std::vector<std::string> Scene::getNodeTagsAt(const std::string &hostName,
                                              const std::string &nodeId) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n) return {};
    return n->tags;
}

std::vector<std::string> Scene::collectIdsByTagAt(const std::string &hostName,
                                                  const std::string &tag) const {
    SceneHost *h = resolveHost(hostName);
    if (!h) return {};
    std::vector<std::string> out;
    for (SceneNode *n : h->findAllByTag(tag)) {
        if (n) out.push_back(n->id);
    }
    return out;
}

bool Scene::setNodeLayerAt(const std::string &hostName, const std::string &nodeId,
                           int layer) {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n) return false;
    n->layer = layer;
    return true;
}

int Scene::getNodeLayerAt(const std::string &hostName, const std::string &nodeId) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    return n ? n->layer : 0;
}

bool Scene::setNodeEventHandlerAt(const std::string &hostName, ssq::Object cb) {
    if (!vm_) return false;
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    const std::string name = hostName.empty() ? h->getName() : hostName;

    auto it = eventCbs_.find(name);
    if (it != eventCbs_.end()) {
        sq_release(vm_, &it->second);
        eventCbs_.erase(it);
    }
    HSQOBJECT raw = cb.getRaw();
    if (raw._type == OT_NULL) {  // clear handler
        h->setEventHandler({});
        return true;
    }
    if (raw._type != OT_CLOSURE && raw._type != OT_NATIVECLOSURE) return false;
    sq_addref(vm_, &raw);
    eventCbs_[name] = raw;
    h->setEventHandler(
        [this, name](SceneHost *, const std::string &action, const std::string &nodeId,
                     const std::string &parentId) {
            callEventCallback(name, action, nodeId, parentId);
        });
    return true;
}

void Scene::callEventCallback(const std::string &hostName, const std::string &action,
                              const std::string &nodeId, const std::string &parentId) {
    if (!vm_) return;
    auto it = eventCbs_.find(hostName);
    if (it == eventCbs_.end()) return;
    const SQInteger top = sq_gettop(vm_);
    sq_pushobject(vm_, it->second);
    sq_pushstring(vm_, action.c_str(), -1);
    sq_pushstring(vm_, nodeId.c_str(), -1);
    sq_pushstring(vm_, parentId.c_str(), -1);
    if (SQ_FAILED(sq_call(vm_, 3, SQFalse, SQTrue))) {
        sq_settop(vm_, top);
        return;
    }
    sq_settop(vm_, top);
}

// ---------------------------------------------------------------------------
// Bounds / serialization / picking / culling
// ---------------------------------------------------------------------------

bool Scene::setNodeBoundsAt(const std::string &hostName, const std::string &nodeId,
                            float minX, float minY, float minZ, float maxX, float maxY,
                            float maxZ) {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n) return false;
    n->bminX = minX;
    n->bminY = minY;
    n->bminZ = minZ;
    n->bmaxX = maxX;
    n->bmaxY = maxY;
    n->bmaxZ = maxZ;
    n->hasBounds = true;
    return true;
}

bool Scene::hasNodeBoundsAt(const std::string &hostName, const std::string &nodeId) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    return n ? n->hasBounds : false;
}

std::vector<float> Scene::getNodeBoundsAt(const std::string &hostName,
                                          const std::string &nodeId) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->findById(nodeId) : nullptr;
    if (!n || !n->hasBounds) return {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    return {n->bminX, n->bminY, n->bminZ, n->bmaxX, n->bmaxY, n->bmaxZ};
}

#ifdef EVENGINE_SCENE_JSON
std::string Scene::serializeHostAt(const std::string &hostName) const {
    SceneHost *h = resolveHost(hostName);
    if (!h || !h->getRoot()) return "{}";
    Poco::JSON::Object root;
    root.set("host", h->getName());
    root.set("root", nodeToJson(*h->tree(), h->getRoot()));
    std::ostringstream oss;
    Poco::JSON::Stringifier::stringify(root, oss);
    return oss.str();
}

bool Scene::deserializeHostAt(const std::string &hostName, const std::string &json) {
    try {
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(json);
        Poco::JSON::Object::Ptr root = result.extract<Poco::JSON::Object::Ptr>();
        if (!root) return false;
        Poco::JSON::Object::Ptr tree = root->getObject("root");
        if (!tree) return false;
        NodeDesc desc = nodeFromJson(tree);
        mountAs(hostName.empty() ? "default" : hostName, std::move(desc));
        return true;
    } catch (...) {
        return false;
    }
}
#else
std::string Scene::serializeHostAt(const std::string &hostName) const {
    (void)hostName;
    return "{}";  // Poco JSON is not available on this build (e.g. WASM trim)
}

bool Scene::deserializeHostAt(const std::string &hostName, const std::string &json) {
    (void)hostName;
    (void)json;
    return false;
}
#endif

std::string Scene::pickRayAt(const std::string &hostName, float ox, float oy, float oz,
                             float dx, float dy, float dz) const {
    SceneHost *h = resolveHost(hostName);
    if (!h) return {};
    const glm::vec3 origin(ox, oy, oz);
    glm::vec3 dir(dx, dy, dz);
    if (glm::dot(dir, dir) < 1e-12f) return {};
    dir = glm::normalize(dir);

    float bestT = std::numeric_limits<float>::max();
    std::string best;
    h->walkDepthFirst([&](SceneHost *, int, SceneNode &n) {
        if (!n.hasBounds) return;
        const AABB3f w = worldBoundsOf(n);
        float t0 = 0.f, t1 = 0.f;
        if (rayAABB(origin, dir, w, t0, t1) && t1 >= 0.f && t0 < bestT) {
            bestT = std::max(t0, 0.f);
            best = n.id;
        }
    });
    return best;
}

std::string Scene::pickScreenAt(const std::string &hostName, graphics::Camera3D *cam,
                                float screenX, float screenY, float viewW,
                                float viewH) const {
    if (!cam) return {};
    cam->screenToRay(screenX, screenY, viewW, viewH);
    return pickRayAt(hostName, cam->getScreenRayOriginX(), cam->getScreenRayOriginY(),
                     cam->getScreenRayOriginZ(), cam->getScreenRayDirX(),
                     cam->getScreenRayDirY(), cam->getScreenRayDirZ());
}

std::vector<std::string> Scene::collectFrustumIdsAt(const std::string &hostName,
                                                    graphics::Camera3D *cam, float viewW,
                                                    float viewH) const {
    SceneHost *h = resolveHost(hostName);
    std::vector<std::string> out;
    if (!h || !cam || viewW <= 0.f || viewH <= 0.f) return out;
    auto d = cam->data();
    const glm::vec3 eye(d->eyeX, d->eyeY, d->eyeZ);
    const glm::vec3 target(d->targetX, d->targetY, d->targetZ);
    const glm::vec3 up(d->upX, d->upY, d->upZ);
    const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
    const glm::mat4 projM =
        graphics::perspectiveVulkanRH_ZO(glm::radians(d->fovYDeg), viewW / viewH,
                                         d->nearZ, d->farZ);
    const glm::mat4 clip = projM * viewM;
    const glm::mat4 invClip = glm::inverse(clip);

    h->walkDepthFirst([&](SceneHost *, int, SceneNode &n) {
        if (!n.hasBounds) return;
        if (aabbIntersectsFrustum(clip, invClip, worldBoundsOf(n))) {
            out.push_back(n.id);
        }
    });
    return out;
}

bool Scene::syncSpatialIndexAt(const std::string &hostName, spatial::Octree *ot) const {
    SceneHost *h = resolveHost(hostName);
    if (!h || !ot) return false;
    bool any = false;
    h->walkDepthFirst([&](SceneHost *, int index, SceneNode &n) {
        if (!n.hasBounds) return;
        const AABB3f w = worldBoundsOf(n);
        ot->insert(index, w.min.x, w.min.y, w.min.z, w.max.x, w.max.y, w.max.z);
        any = true;
    });
    return any;
}

std::string Scene::nodeIdFromSpatialIdAt(const std::string &hostName, int index) const {
    SceneHost *h = resolveHost(hostName);
    SceneNode *n = h ? h->getNode(index) : nullptr;
    return n ? n->id : std::string{};
}

bool Scene::hasNode(const std::string &id) {
    SceneHost *h = selected_;
    return h ? h->hasNode(id) : false;
}

int Scene::getNodeCount() {
    SceneHost *h = selected_;
    return h ? h->getNodeCount() : 0;
}

std::string Scene::getRootId() {
    SceneHost *h = selected_;
    if (!h) return {};
    SceneNode *r = h->getRoot();
    return r ? r->id : std::string{};
}

std::string Scene::getParentId(const std::string &id) {
    SceneHost *h = selected_;
    if (!h) return {};
    SceneNode *p = h->getParentById(id);
    return p ? p->id : std::string{};
}

int Scene::getChildCount(const std::string &id) {
    SceneHost *h = selected_;
    return h ? h->getChildCountById(id) : 0;
}

std::string Scene::getChildIdAt(const std::string &parentId, int childOrdinal) {
    SceneHost *h = selected_;
    if (!h) return {};
    SceneNode *c = h->getChildAtById(parentId, childOrdinal);
    return c ? c->id : std::string{};
}

std::string Scene::findIdByName(const std::string &name) {
    SceneHost *h = selected_;
    if (!h) return {};
    SceneNode *n = h->findByName(name);
    return n ? n->id : std::string{};
}

std::string Scene::findIdByPath(const std::string &path) {
    SceneHost *h = selected_;
    if (!h) return {};
    SceneNode *n = h->findByPath(path);
    return n ? n->id : std::string{};
}

std::string Scene::getNodePath(const std::string &id) {
    SceneHost *h = selected_;
    return h ? h->getPathById(id) : std::string{};
}

bool Scene::isAncestor(const std::string &ancestorId, const std::string &nodeId) {
    SceneHost *h = selected_;
    return h ? h->isAncestorOfById(ancestorId, nodeId) : false;
}

bool Scene::isDescendant(const std::string &nodeId, const std::string &ancestorId) {
    SceneHost *h = selected_;
    return h ? h->isDescendantOfById(nodeId, ancestorId) : false;
}

std::vector<std::string> Scene::collectIds() {
    SceneHost *h = selected_;
    return h ? h->collectIds() : std::vector<std::string>{};
}

std::vector<std::string> Scene::collectIdsFrom(const std::string &id) {
    SceneHost *h = selected_;
    if (!h) return {};
    return h->collectIdsFrom(h->findIndexById(id));
}

std::vector<std::string> Scene::collectIdsByName(const std::string &name) {
    SceneHost *h = selected_;
    return h ? h->collectIdsByName(name) : std::vector<std::string>{};
}

std::vector<std::string> Scene::collectIdsVisible(bool visible) {
    SceneHost *h = selected_;
    return h ? h->collectIdsVisible(visible) : std::vector<std::string>{};
}

std::vector<std::string> Scene::collectChildIds(const std::string &parentId) {
    SceneHost *h = selected_;
    return h ? h->getChildIds(parentId) : std::vector<std::string>{};
}

std::vector<std::string> Scene::walkDepthFirstIds() { return collectIds(); }

std::vector<std::string> Scene::walkBreadthFirstIds() {
    SceneHost *h = selected_;
    if (!h) return {};
    std::vector<std::string> out;
    h->walkBreadthFirst([&](SceneHost *, int, SceneNode &n) { out.push_back(n.id); });
    return out;
}

void Scene::beginBuild() {
    openStack_.clear();
    hasBuiltRoot_ = false;
    builtRoot_ = NodeDesc{};
}

void Scene::pushOpen(NodeDesc d) { openStack_.push_back(std::move(d)); }

NodeDesc &Scene::currentParent() {
    if (openStack_.empty()) throw std::runtime_error("scene: node outside beginNode/beginGroup");
    return openStack_.back();
}

void Scene::beginNode(const std::string &id, const std::string &name) {
    if (openStack_.empty() && hasBuiltRoot_) beginBuild();
    pushOpen(node(id, {}, name));
}

void Scene::beginGroup(const std::string &id) {
    if (openStack_.empty() && hasBuiltRoot_) beginBuild();
    pushOpen(group({}, id));
}

void Scene::end() {
    if (openStack_.empty()) throw std::runtime_error("scene: end() without begin");
    NodeDesc finished = std::move(openStack_.back());
    openStack_.pop_back();
    if (openStack_.empty()) {
        builtRoot_ = std::move(finished);
        hasBuiltRoot_ = true;
    } else {
        openStack_.back().children.push_back(std::move(finished));
    }
}

void Scene::addNode(const std::string &id, const std::string &name) {
    currentParent().children.push_back(node(id, {}, name));
}

void Scene::setBuildPosition(float x, float y, float z) {
    NodeDesc &d = currentParent();
    d.x = x;
    d.y = y;
    d.z = z;
}

void Scene::setBuildRotation(float yaw, float pitch, float roll) {
    NodeDesc &d = currentParent();
    d.yaw = yaw;
    d.pitch = pitch;
    d.roll = roll;
}

void Scene::setBuildScale(float sx, float sy, float sz) {
    NodeDesc &d = currentParent();
    d.sx = sx;
    d.sy = sy;
    d.sz = sz;
}

void Scene::setBuildSpace(const std::string &space) { currentParent().space = space; }

void Scene::setBuildVisible(bool visible) { currentParent().visible = visible; }

bool Scene::buildComplete() const { return openStack_.empty() && hasBuiltRoot_; }

bool Scene::mountBuild() {
    if (!buildComplete()) return false;
    remount(std::move(builtRoot_));
    hasBuiltRoot_ = false;
    builtRoot_ = NodeDesc{};
    return true;
}

bool Scene::mountBuildAs(const std::string &name) {
    if (!buildComplete()) return false;
    mountAs(name, std::move(builtRoot_));
    hasBuiltRoot_ = false;
    builtRoot_ = NodeDesc{};
    return true;
}

bool Scene::remountBuildAs(const std::string &name) {
    if (!buildComplete()) return false;
    SceneHost *h = findHost(name);
    if (!h) h = SceneHost::createHost(name);
    h->setTreeReconcile(std::move(builtRoot_));
    TransformSystem::updateHost(h);
    pruneOrphanObjects();
    selected_ = h;
    hasBuiltRoot_ = false;
    builtRoot_ = NodeDesc{};
    return true;
}

// ---------------------------------------------------------------------------
// Per-node script entities (native primitives; script wrappers see expose())
// ---------------------------------------------------------------------------

SceneHost *Scene::resolveHost(const std::string &hostName) const {
    if (hostName.empty()) return selected_;
    return findHostByName(hostName);
}

SceneObject *Scene::findSceneObjectById(uint32_t id) const {
    if (id == 0) return nullptr;
    if (ecs::current()->getManager<SceneObject>() == nullptr) return nullptr;
    auto view = ecs::View<SceneObject, SceneObject::Meta>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta] = *it;
        if (meta->entity && uint32_t(meta->entity->id) == id) return meta->entity;
    }
    return nullptr;
}

SceneObject *Scene::ensureSceneObject(SceneHost *host, SceneNode *node,
                                      const std::string &hostName) {
    if (node->objectId != 0) {
        if (SceneObject *o = findSceneObjectById(node->objectId)) return o;
        node->objectId = 0;  // stale id (entity was released)
    }
    SceneObject *o = SceneObject::createObject(hostName, node->id);
    node->objectId = uint32_t(o->id);
    return o;
}

bool Scene::rootEntity(const std::string &hostName, const std::string &nodeId,
                       ssq::Object instance) {
    if (!vm_) return false;
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    SceneNode *n = h->findById(nodeId);
    if (!n) return false;
    HSQOBJECT raw = instance.getRaw();
    if (raw._type != OT_INSTANCE) return false;
    const std::string hName = hostName.empty() ? h->getName() : hostName;
    SceneObject *obj = ensureSceneObject(h, n, hName);
    if (!obj) return false;
    for (const auto &b : obj->scriptBindings()->instances) {
        if (sameScriptObject(b, raw)) return false;  // already rooted
    }
    sq_addref(vm_, &raw);
    obj->scriptBindings()->instances.push_back(raw);
    return true;
}

bool Scene::unrootEntityAt(const std::string &hostName, const std::string &nodeId,
                           int index) {
    if (!vm_) return false;
    SceneHost *h = resolveHost(hostName);
    if (!h) return false;
    SceneNode *n = h->findById(nodeId);
    if (!n) return false;
    SceneObject *obj = n->objectId ? findSceneObjectById(n->objectId) : nullptr;
    if (!obj) return false;
    auto &vec = obj->scriptBindings()->instances;
    if (index < 0 || index >= int(vec.size())) return false;
    HSQOBJECT raw = vec[size_t(index)];
    vec.erase(vec.begin() + index);
    sq_release(vm_, &raw);
    if (vec.empty()) {
        n->objectId = 0;
        obj->release();
    }
    return true;
}

int Scene::forEachEntity(const std::string &hostName, const std::string &nodeId,
                         ssq::Object cb) {
    if (!vm_) return 0;
    SceneHost *h = resolveHost(hostName);
    if (!h) return 0;
    SceneNode *n = h->findById(nodeId);
    if (!n) return 0;
    SceneObject *obj = n->objectId ? findSceneObjectById(n->objectId) : nullptr;
    if (!obj) return 0;
    // Copy + addref: the callback may attach/detach and mutate the live list.
    auto vec = obj->scriptBindings()->instances;
    for (auto &b : vec) sq_addref(vm_, &b);
    HSQOBJECT fn = cb.getRaw();
    sq_addref(vm_, &fn);
    int count = 0;
    for (size_t i = 0; i < vec.size(); ++i) {
        if (callCallback(fn, vec[i], int(i))) ++count;
    }
    sq_release(vm_, &fn);
    for (auto &b : vec) sq_release(vm_, &b);
    return count;
}

void Scene::updateScripts(float dt) {
    updateTransformsAll();
    if (!vm_) return;
    if (ecs::current()->getManager<SceneObject>() == nullptr) return;
    // Snapshot + addref so script update(dt) may attach/detach safely.
    std::vector<HSQOBJECT> snapshot;
    {
        auto view = ecs::View<SceneObject, SceneObject::Meta, SceneObject::ScriptBindings>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [meta, sb] = *it;
            if (!meta->entity) continue;
            for (auto &inst : sb->instances) snapshot.push_back(inst);
        }
    }
    for (auto &h : snapshot) sq_addref(vm_, &h);
    for (auto &h : snapshot) callMethod(h, "update", dt);
    for (auto &h : snapshot) sq_release(vm_, &h);
}

std::string Scene::currentHostName() const {
    return selected_ ? selected_->getName() : std::string{};
}

SceneNodeRef *Scene::getNodeRefAt(const std::string &hostName,
                                  const std::string &nodeId) const {
    std::string h = hostName.empty()
                        ? (selected_ ? selected_->getName() : std::string{})
                        : hostName;
    return new SceneNodeRef(std::move(h), nodeId);
}

SceneNodeRef *Scene::getNodeRefByPathAt(const std::string &hostName,
                                        const std::string &path) const {
    std::string h = hostName.empty()
                        ? (selected_ ? selected_->getName() : std::string{})
                        : hostName;
    SceneHost *host = resolveHost(hostName);
    if (!host) return new SceneNodeRef(std::move(h), "");
    SceneNode *n = host->findByPath(path);
    return new SceneNodeRef(std::move(h), n ? n->id : std::string{});
}

void Scene::pruneOrphanObjects() {
    if (ecs::current()->getManager<SceneObject>() == nullptr) return;
    std::vector<SceneObject *> orphans;
    {
        auto view = ecs::View<SceneObject, SceneObject::Meta, SceneObject::ScriptBindings>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [meta, sb] = *it;
            if (!meta->entity) continue;
            SceneHost *h = findHostByName(meta->hostName);
            if (!h) {
                orphans.push_back(meta->entity);
                continue;
            }
            SceneNode *n = h->findById(meta->nodeId);
            if (!n || n->objectId != uint32_t(meta->entity->id)) {
                orphans.push_back(meta->entity);
                continue;
            }
            (void)sb;
            // Self-heal: node id / host renamed (reconcile patch), keep binding.
            if (meta->hostName != h->getName() || meta->nodeId != n->id) {
                meta->hostName = h->getName();
                meta->nodeId = n->id;
                syncBindingRefs(meta->entity);
            }
        }
    }
    for (SceneObject *o : orphans) teardownBindings(o);
}

void Scene::syncBindingRefs(SceneObject *obj) {
    if (!vm_ || !obj) return;
    for (auto &inst : obj->scriptBindings()->instances) {
        setStringField(vm_, inst, "hostName", obj->meta()->hostName);
        setStringField(vm_, inst, "nodeId", obj->meta()->nodeId);
    }
}

void Scene::teardownBindings(SceneObject *obj) {
    if (!obj) return;
    auto &vec = obj->scriptBindings()->instances;
    if (vm_) {
        for (auto &inst : vec) {
            callMethod0(inst, "onDetach");
            callMethod0(inst, "destroy");
            sq_release(vm_, &inst);
        }
    }
    vec.clear();
    obj->release();
}

bool Scene::callMethod(HSQOBJECT inst, const char *name, float dt) {
    if (!vm_ || inst._type != OT_INSTANCE) return false;
    const SQInteger top = sq_gettop(vm_);
    sq_pushobject(vm_, inst);
    sq_pushstring(vm_, name, -1);
    if (SQ_FAILED(sq_get(vm_, -2))) {
        sq_settop(vm_, top);
        return false;
    }
    const SQObjectType t = sq_gettype(vm_, -1);
    if (t != OT_CLOSURE && t != OT_NATIVECLOSURE) {
        sq_settop(vm_, top);
        return false;
    }
    sq_pushobject(vm_, inst);   // this
    sq_pushfloat(vm_, SQFloat(dt));
    if (SQ_FAILED(sq_call(vm_, 2, SQFalse, SQTrue))) {
        sq_settop(vm_, top);
        return false;
    }
    sq_settop(vm_, top);
    return true;
}

bool Scene::callMethod0(HSQOBJECT inst, const char *name) {
    if (!vm_ || inst._type != OT_INSTANCE) return false;
    const SQInteger top = sq_gettop(vm_);
    sq_pushobject(vm_, inst);
    sq_pushstring(vm_, name, -1);
    if (SQ_FAILED(sq_get(vm_, -2))) {
        sq_settop(vm_, top);
        return false;
    }
    const SQObjectType t = sq_gettype(vm_, -1);
    if (t != OT_CLOSURE && t != OT_NATIVECLOSURE) {
        sq_settop(vm_, top);
        return false;
    }
    sq_pushobject(vm_, inst);   // this
    if (SQ_FAILED(sq_call(vm_, 1, SQFalse, SQTrue))) {
        sq_settop(vm_, top);
        return false;
    }
    sq_settop(vm_, top);
    return true;
}

bool Scene::callCallback(HSQOBJECT fn, HSQOBJECT inst, int index) {
    if (!vm_) return false;
    const SQInteger top = sq_gettop(vm_);
    sq_pushobject(vm_, fn);
    sq_pushobject(vm_, inst);
    sq_pushinteger(vm_, SQInteger(index));
    if (SQ_FAILED(sq_call(vm_, 2, SQFalse, SQTrue))) {
        sq_settop(vm_, top);
        return false;
    }
    sq_settop(vm_, top);
    return true;
}

void Scene::expose(ssq::Table &table) {
    if (Scene *self = Scene::create()) self->vm_ = table.getHandle();
    auto cls = table.addClass(name, Scene::create, false);
    expose(cls);

    // Lightweight node handle (owned by script; created via getNodeRef*).
    auto refCls = table.addClass<SceneNodeRef>(
        "SceneNodeRef",
        std::function<SceneNodeRef *()>([]() -> SceneNodeRef * { return nullptr; }), true);
    refCls.addFunc("getNodeId", &SceneNodeRef::getNodeId);
    refCls.addFunc("getHostName", &SceneNodeRef::getHostName);
    refCls.addFunc("isValid", &SceneNodeRef::isValid);
    refCls.addFunc("getScene", &SceneNodeRef::getScene);
    refCls.addFunc("setPosition", &SceneNodeRef::setPosition);
    refCls.addFunc("getPositionX", &SceneNodeRef::getPositionX);
    refCls.addFunc("getPositionY", &SceneNodeRef::getPositionY);
    refCls.addFunc("getPositionZ", &SceneNodeRef::getPositionZ);
    refCls.addFunc("getPosition", &SceneNodeRef::getPosition);
    refCls.addFunc("setRotation", &SceneNodeRef::setRotation);
    refCls.addFunc("getRotationYaw", &SceneNodeRef::getRotationYaw);
    refCls.addFunc("getRotationPitch", &SceneNodeRef::getRotationPitch);
    refCls.addFunc("getRotationRoll", &SceneNodeRef::getRotationRoll);
    refCls.addFunc("getRotation", &SceneNodeRef::getRotation);
    refCls.addFunc("setScale", &SceneNodeRef::setScale);
    refCls.addFunc("getScaleX", &SceneNodeRef::getScaleX);
    refCls.addFunc("getScaleY", &SceneNodeRef::getScaleY);
    refCls.addFunc("getScaleZ", &SceneNodeRef::getScaleZ);
    refCls.addFunc("getScale", &SceneNodeRef::getScale);
    refCls.addFunc("setVisible", &SceneNodeRef::setVisible);
    refCls.addFunc("isVisible", &SceneNodeRef::isVisible);
    refCls.addFunc("getWorldPositionX", &SceneNodeRef::getWorldPositionX);
    refCls.addFunc("getWorldPositionY", &SceneNodeRef::getWorldPositionY);
    refCls.addFunc("getWorldPositionZ", &SceneNodeRef::getWorldPositionZ);
    refCls.addFunc("getWorldPosition", &SceneNodeRef::getWorldPosition);
    refCls.addFunc("getWorldMatrix", &SceneNodeRef::getWorldMatrix);
    refCls.addFunc("getForward", &SceneNodeRef::getForward);
    refCls.addFunc("getRight", &SceneNodeRef::getRight);
    refCls.addFunc("getUp", &SceneNodeRef::getUp);
    refCls.addFunc("getParentId", &SceneNodeRef::getParentId);
    refCls.addFunc("getChildCount", &SceneNodeRef::getChildCount);
    refCls.addFunc("getChildIdAt", &SceneNodeRef::getChildIdAt);
    refCls.addFunc("getPath", &SceneNodeRef::getPath);

    injectSceneComponentClass(table);

    // Register a hook so eve.SceneEntity (extends eve.Entity) is injected only
    // after exposeECS() has defined the script ECS base classes.
    if (!g_sceneEntityHookRegistered) {
        g_sceneEntityHookRegistered = true;
        eve::registerPostEcsHook([](ssq::Table &t) { injectSceneEntityScript(t); });
    }
}

void Scene::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Scene::getName);
    cls.addFunc("select", &Scene::select);
    cls.addFunc("bindOwner", &Scene::bindOwner);
    cls.addFunc("setHostVisible", &Scene::setHostVisible);
    cls.addFunc("setHostLayer", &Scene::setHostLayer);
    cls.addFunc("currentHostName", &Scene::currentHostName);
    cls.addFunc("updateTransforms", &Scene::updateTransforms);
    cls.addFunc("updateTransformsAll", &Scene::updateTransformsAll);
    cls.addFunc("updateScripts", &Scene::updateScripts);
    cls.addFunc("setNodePosition", &Scene::setNodePosition);
    cls.addFunc("setNodeRotation", &Scene::setNodeRotation);
    cls.addFunc("setNodeScale", &Scene::setNodeScale);
    cls.addFunc("setNodeVisible", &Scene::setNodeVisible);
    cls.addFunc("linkRenderable2D", &Scene::linkRenderable2D);
    cls.addFunc("linkRenderable3D", &Scene::linkRenderable3D);
    cls.addFunc("unlinkNode", &Scene::unlinkNode);

    // Generic link system primitives (host-scoped; script wrappers below)
    cls.addFunc("linkRenderable2DAt", &Scene::linkRenderable2DAt);
    cls.addFunc("linkRenderable3DAt", &Scene::linkRenderable3DAt);
    cls.addFunc("linkPhysics2DAt", &Scene::linkPhysics2DAt);
    cls.addFunc("linkPhysics3DAt", &Scene::linkPhysics3DAt);
    cls.addFunc("linkCamera3DAt", &Scene::linkCamera3DAt);
    cls.addFunc("linkAudio3DAt", &Scene::linkAudio3DAt);
    cls.addFunc("unlinkNodeAt", &Scene::unlinkNodeAt);
    cls.addFunc("unlinkNodeKindAt", &Scene::unlinkNodeKindAt);
    cls.addFunc("linkCountAt", &Scene::linkCountAt);

    // Script-API completeness primitives (host-scoped)
    cls.addFunc("getNodePositionAt", &Scene::getNodePositionAt);
    cls.addFunc("getNodeRotationAt", &Scene::getNodeRotationAt);
    cls.addFunc("getNodeScaleAt", &Scene::getNodeScaleAt);
    cls.addFunc("getNodeVisibleAt", &Scene::getNodeVisibleAt);
    cls.addFunc("getNodeWorldPositionAt", &Scene::getNodeWorldPositionAt);
    cls.addFunc("getNodeWorldRotationAt", &Scene::getNodeWorldRotationAt);
    cls.addFunc("getNodeWorldScaleAt", &Scene::getNodeWorldScaleAt);
    cls.addFunc("localToWorldAt", &Scene::localToWorldAt);
    cls.addFunc("worldToLocalAt", &Scene::worldToLocalAt);
    cls.addFunc("setNodeParentAt", &Scene::setNodeParentAt);
    cls.addFunc("removeNodeAt", &Scene::removeNodeAt);
    cls.addFunc("addChildAt", &Scene::addChildAt);
    cls.addFunc("removeChildAt", &Scene::removeChildAt);
    cls.addFunc("setNodeQuaternionAt", &Scene::setNodeQuaternionAt);
    cls.addFunc("getNodeQuaternionAt", &Scene::getNodeQuaternionAt);
    cls.addFunc("setNodeLookAtAt", &Scene::setNodeLookAtAt);
    cls.addFunc("addNodeTagAt", &Scene::addNodeTagAt);
    cls.addFunc("removeNodeTagAt", &Scene::removeNodeTagAt);
    cls.addFunc("hasNodeTagAt", &Scene::hasNodeTagAt);
    cls.addFunc("getNodeTagsAt", &Scene::getNodeTagsAt);
    cls.addFunc("collectIdsByTagAt", &Scene::collectIdsByTagAt);
    cls.addFunc("setNodeLayerAt", &Scene::setNodeLayerAt);
    cls.addFunc("getNodeLayerAt", &Scene::getNodeLayerAt);
    cls.addFunc("setNodeEventHandlerAt",
                [](Scene *self, std::string hostName, ssq::Object cb) {
                    return self->setNodeEventHandlerAt(hostName, cb);
                });

    // Bounds / serialization / picking / culling
    cls.addFunc("setNodeBoundsAt", &Scene::setNodeBoundsAt);
    cls.addFunc("hasNodeBoundsAt", &Scene::hasNodeBoundsAt);
    cls.addFunc("getNodeBoundsAt", &Scene::getNodeBoundsAt);
    cls.addFunc("serializeHostAt", &Scene::serializeHostAt);
    cls.addFunc("deserializeHostAt", &Scene::deserializeHostAt);
    cls.addFunc("pickRayAt", &Scene::pickRayAt);
    cls.addFunc("pickScreenAt", &Scene::pickScreenAt);
    cls.addFunc("collectFrustumIdsAt", &Scene::collectFrustumIdsAt);
    cls.addFunc("syncSpatialIndexAt", &Scene::syncSpatialIndexAt);
    cls.addFunc("nodeIdFromSpatialIdAt", &Scene::nodeIdFromSpatialIdAt);

    // Per-node script entity primitives (called from injected script wrappers).
    // ssq::Object params are bound via lambdas (member-pointer path may not
    // support raw script objects; lambda path is used by eve.component too).
    cls.addFunc("rootEntity",
                [](Scene *self, std::string hostName, std::string nodeId,
                   ssq::Object instance) {
                    return self->rootEntity(hostName, nodeId, instance);
                });
    cls.addFunc("unrootEntityAt", &Scene::unrootEntityAt);
    cls.addFunc("forEachEntity",
                [](Scene *self, std::string hostName, std::string nodeId,
                   ssq::Object cb) {
                    return self->forEachEntity(hostName, nodeId, cb);
                });
    cls.addFunc("getNodeRefAt", &Scene::getNodeRefAt);
    cls.addFunc("getNodeRefByPathAt", &Scene::getNodeRefByPathAt);

    cls.addFunc("hasNode", &Scene::hasNode);
    cls.addFunc("getNodeCount", &Scene::getNodeCount);
    cls.addFunc("getRootId", &Scene::getRootId);
    cls.addFunc("getParentId", &Scene::getParentId);
    cls.addFunc("getChildCount", &Scene::getChildCount);
    cls.addFunc("getChildIdAt", &Scene::getChildIdAt);
    cls.addFunc("findIdByName", &Scene::findIdByName);
    cls.addFunc("findIdByPath", &Scene::findIdByPath);
    cls.addFunc("getNodePath", &Scene::getNodePath);
    cls.addFunc("isAncestor", &Scene::isAncestor);
    cls.addFunc("isDescendant", &Scene::isDescendant);
    cls.addFunc("collectIds", &Scene::collectIds);
    cls.addFunc("collectIdsFrom", &Scene::collectIdsFrom);
    cls.addFunc("collectIdsByName", &Scene::collectIdsByName);
    cls.addFunc("collectIdsVisible", &Scene::collectIdsVisible);
    cls.addFunc("collectChildIds", &Scene::collectChildIds);
    cls.addFunc("walkDepthFirstIds", &Scene::walkDepthFirstIds);
    cls.addFunc("walkBreadthFirstIds", &Scene::walkBreadthFirstIds);

    cls.addFunc("beginBuild", &Scene::beginBuild);
    cls.addFunc("beginNode", &Scene::beginNode);
    cls.addFunc("beginGroup", &Scene::beginGroup);
    cls.addFunc("end", &Scene::end);
    cls.addFunc("addNode", &Scene::addNode);
    cls.addFunc("setBuildPosition", &Scene::setBuildPosition);
    cls.addFunc("setBuildRotation", &Scene::setBuildRotation);
    cls.addFunc("setBuildScale", &Scene::setBuildScale);
    cls.addFunc("setBuildSpace", &Scene::setBuildSpace);
    cls.addFunc("setBuildVisible", &Scene::setBuildVisible);
    cls.addFunc("mountBuild", &Scene::mountBuild);
    cls.addFunc("mountBuildAs", &Scene::mountBuildAs);
    cls.addFunc("remountBuildAs", &Scene::remountBuildAs);
}

}  // namespace eve::scene
