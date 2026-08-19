// Scene node / link / bounds / picking implementation.
//
// Split out of the original Scene.cpp so the script binding layer
// (Scene.cpp) and the node manipulation methods live in separate
// translation units. Pure move: no behavior change.

#include "scene/Scene.h"
#include "scene/SceneBounds.h"
#include "scene/SceneInternal.h"
#include "scene/TransformSystem.h"
#include "spatial/Octree.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <limits>
#include <string>
#include <vector>

namespace eve::scene {

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
    const int k = linkKindFromString(kind);
    if (k < 0) return false;
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

}  // namespace eve::scene
