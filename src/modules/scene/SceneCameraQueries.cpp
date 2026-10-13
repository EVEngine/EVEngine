#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "scene/Scene.h"
#include "scene/SceneBounds.h"
#include "scene/SceneCameraProjection.h"
#include "scene/SceneComponent.h"
#include "scene/SceneHost.h"
#include "scene/SceneLink.h"
#include "scene/SceneNodeRef.h"
#include "scene/TransformSystem.h"

// SceneCameraQueries.cpp
// Picking / culling entry points whose camera math belongs to graphics.
//
// The traversal and mutation stay here because the scene owns them; only the
// screen ray and the clip matrix come from the ISceneCameraProjection
// capability (graphics/ScenePicking.cpp registers it). Querying a capability
// instead of calling graphics keeps this module free of an upward include while
// still defining these symbols in scene, which a per-layer DLL split needs:
// graphics (L4) cannot be imported by scene (L1).
//
// Split out of Scene.cpp (already past the ~1000 line limit) rather than
// appended to it.


namespace eve::scene {

std::string Scene::pickScreenAt(const std::string &hostName, graphics::Camera3D *cam, float screenX, float screenY,
                                float viewW, float viewH) const {
    if (!cam) return {};
    auto *projection = eve::cap::query<ISceneCameraProjection>();
    if (!projection) return {};
    const auto ray = projection->screenRay(*cam, screenX, screenY, viewW, viewH);
    if (!ray) return {};
    return pickRayAt(hostName, ray->origin.x, ray->origin.y, ray->origin.z, ray->direction.x, ray->direction.y,
                     ray->direction.z);
}

std::vector<std::string> Scene::collectFrustumIdsAt(const std::string &hostName, graphics::Camera3D *cam, float viewW,
                                                    float viewH) const {
    SceneHost               *h = resolveHost(hostName);
    std::vector<std::string> out;
    if (!h || !cam || viewW <= 0.f || viewH <= 0.f) return out;
    auto *projection = eve::cap::query<ISceneCameraProjection>();
    if (!projection) return out;
    const auto clip = projection->clipForViewport(*cam, viewW, viewH);
    if (!clip) return out;

    h->walkDepthFirst([&](SceneHost *, int, SceneNode &n) {
        if (!n.hasBounds) return;
        if (aabbIntersectsFrustum(clip->clip, clip->inverse, worldBoundsOf(n))) {
            out.push_back(n.id);
        }
    });
    return out;
}

Result<int> Scene::applyPcgTerrainCullingAt(const std::string &hostName, graphics::Camera3D *cam, float viewW,
                                            float viewH, const std::string &tag) {
    SceneHost *host = resolveHost(hostName);
    if (!host || !cam || !std::isfinite(viewW) || !std::isfinite(viewH) || viewW <= 0.f || viewH <= 0.f || tag.empty())
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Pcg terrain culling requires host, camera, positive viewport and tag",
            "scene.pcgTerrainCulling"));
    auto *projection = eve::cap::query<ISceneCameraProjection>();
    if (!projection)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "Pcg terrain culling needs the graphics camera projection capability",
            "scene.pcgTerrainCulling"));
    const auto clip = projection->clipForViewport(*cam, viewW, viewH);
    if (!clip)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "Pcg terrain culling requires a usable camera viewport",
                                                      "scene.pcgTerrainCulling"));

    std::vector<std::pair<std::string, bool>> changes;
    host->walkDepthFirst([&](SceneHost *, int, SceneNode &node) {
        if (!node.hasBounds || std::find(node.tags.begin(), node.tags.end(), tag) == node.tags.end()) return;
        const bool visible = aabbIntersectsFrustum(clip->clip, clip->inverse, worldBoundsOf(node));
        if (node.visible != visible) changes.emplace_back(node.id, visible);
    });
    for (const auto &[id, visible] : changes) {
        auto node = host->findById(id);
        if (!node) continue;
        node.value()->visible = visible;
        host->markSubtreeDirtyById(id);
    }
    return Result<int>::success(static_cast<int>(changes.size()));
}

}  // namespace eve::scene

// SceneComponent.cpp
#include "scene/SceneComponent.h"
#include <vector>

#include "scene/Scene.h"
#include "scene/TransformSystem.h"

#include <utility>

namespace eve::scene {
namespace detail {
struct ComponentSchedule {
    SceneComponent *component = nullptr;  // Owner invalidates before destruction; never exposed as a handle.
    bool            queued    = false;
};
namespace {
std::vector<std::weak_ptr<ComponentSchedule>> pending;
std::vector<std::weak_ptr<ComponentSchedule>> draining;
bool                                          flushing = false;
}  // namespace
void flushPendingComponents() {
    if (flushing) return;
    flushing = true;
    struct FlushGuard {
        ~FlushGuard() {
            draining.clear();
            flushing = false;
        }
    } guard;
    draining.swap(pending);
    for (size_t i = 0; i < draining.size(); ++i) {
        auto &entry        = draining[i];
        auto  registration = entry.lock();
        if (!registration || !registration->component) continue;
        registration->queued = false;
        try {
            registration->component->updateIfDirty();
        } catch (...) {
            pending.insert(pending.end(), draining.begin() + i + 1, draining.end());
            throw;
        }
    }
}
}  // namespace detail

SceneComponent::SceneComponent() : schedule_(std::make_shared<detail::ComponentSchedule>()) {
    schedule_->component = this;
}
SceneComponent::~SceneComponent() { schedule_->component = nullptr; }
void SceneComponent::markDirty() {
    dirty_ = true;
    if (schedule_->queued) return;
    detail::pending.push_back(schedule_);
    schedule_->queued = true;
}


SceneHost *SceneComponent::host() const { return dynamic_cast<SceneHost *>(ecs::try_get(hostHandle_)); }

void SceneComponent::attach(SceneHost *nextHost) {
    if (host() != nextHost) {
        hostHandle_ = ecs::handle_of(nextHost);
        onMount(nextHost);
    }
    markDirty();
}

void SceneComponent::mountAs(const std::string &hostName) {
    SceneHost *h     = nullptr;
    auto       found = Scene::create()->findHost(hostName);
    if (found.ok()) {
        h = std::move(found).takeValue();
    } else {
        auto created = SceneHost::createHost(hostName);
        if (!created.ok()) return;
        h = std::move(created).takeValue();
    }
    attach(h);
    rebuild(true);
}

void SceneComponent::rebuild(bool forceFull) {
    if (!host() || building_) return;
    building_ = true;
    dirty_ = false;
    try {
        auto  tree   = build();
        auto *target = host();
        if (target) {
            if (forceFull)
                target->setTree(std::move(tree));
            else
                target->setTreeReconcile(std::move(tree));
            TransformSystem::updateHost(target);
        }
    } catch (...) {
        building_ = false;
        markDirty();
        throw;
    }
    building_ = false;
}

bool SceneComponent::updateIfDirty() {
    if (building_ || !dirty_ || !host()) return false;
    rebuild(false);
    return true;
}

}  // namespace eve::scene

// SceneLink.cpp
namespace eve::scene {
namespace {

struct Entry {
    std::string name;
    LinkOps     ops;
};

/**
 * Function-local so registration from another module's static initialiser
 * cannot run before the table exists. Ids are indices and never reused, so a
 * SceneLink's kind stays valid for the life of the process.
 */
std::vector<Entry> &table() {
    static std::vector<Entry> t;
    return t;
}

}  // namespace

int registerLinkKind(const LinkOps &ops) {
    if (!ops.kind || !ops.pushWorld) return -1;
    const int existing = findLinkKind(ops.kind);
    if (existing >= 0) return existing;
    table().push_back({ops.kind, ops});
    // Point the stored ops at our own copy of the name so a caller may pass a
    // temporary.
    Entry &e   = table().back();
    e.ops.kind = e.name.c_str();
    return int(table().size()) - 1;
}

int findLinkKind(const char *kind) {
    if (!kind) return -1;
    const auto &t = table();
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i].name == kind) return int(i);
    return -1;
}

const LinkOps *linkOps(int kindId) {
    const auto &t = table();
    if (kindId < 0 || size_t(kindId) >= t.size()) return nullptr;
    return &t[size_t(kindId)].ops;
}

const char *linkKindName(int kindId) {
    const LinkOps *ops = linkOps(kindId);
    return ops ? ops->kind : "";
}

}  // namespace eve::scene

// SceneNodeRef.cpp
namespace eve::scene {
namespace {

template <class T>
T *borrowSceneResult(eve::Result<T *> result) {
    if (!result.ok()) return nullptr;
    return std::move(result).takeValue();
}

SceneHost *resolveHost(const std::string &hostName) {
    Scene *s = Scene::create();
    if (!s) return nullptr;
    if (hostName.empty()) return s->current();
    return borrowSceneResult(s->findHost(hostName));
}

SceneNode *resolveNode(const std::string &hostName, const std::string &nodeId) {
    if (nodeId.empty()) return nullptr;
    SceneHost *h = resolveHost(hostName);
    return h ? borrowSceneResult(h->findById(nodeId)) : nullptr;
}

glm::vec3 normalizedColumn(const glm::mat4 &m, int col) {
    glm::vec3   v(m[col]);
    const float len = glm::length(v);
    if (len > 1e-8f) v /= len;
    return v;
}

}  // namespace

bool SceneNodeRef::isValid() const { return resolveNode(hostName_, nodeId_) != nullptr; }

eve::SceneObjectId SceneNodeRef::persistentId() const {
    const SceneNode *node = resolveNode(hostName_, nodeId_);
    return node ? node->persistentId : eve::SceneObjectId::nil();
}

Scene *SceneNodeRef::getScene() const { return Scene::create(); }

bool SceneNodeRef::setPosition(float x, float y, float z) {
    SceneHost *h = resolveHost(hostName_);
    SceneNode *n = resolveNode(hostName_, nodeId_);
    if (!h || !n) return false;
    n->x = x;
    n->y = y;
    n->z = z;
    h->markSubtreeDirtyById(nodeId_);
    return true;
}

float SceneNodeRef::getPositionX() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->x : 0.f;
}

float SceneNodeRef::getPositionY() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->y : 0.f;
}

float SceneNodeRef::getPositionZ() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->z : 0.f;
}

std::vector<float> SceneNodeRef::getPosition() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    if (!n) return {0.f, 0.f, 0.f};
    return {n->x, n->y, n->z};
}

bool SceneNodeRef::setRotation(float yaw, float pitch, float roll) {
    SceneHost *h = resolveHost(hostName_);
    SceneNode *n = resolveNode(hostName_, nodeId_);
    if (!h || !n) return false;
    n->yaw   = yaw;
    n->pitch = pitch;
    n->roll  = roll;
    h->markSubtreeDirtyById(nodeId_);
    return true;
}

float SceneNodeRef::getRotationYaw() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->yaw : 0.f;
}

float SceneNodeRef::getRotationPitch() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->pitch : 0.f;
}

float SceneNodeRef::getRotationRoll() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->roll : 0.f;
}

std::vector<float> SceneNodeRef::getRotation() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    if (!n) return {0.f, 0.f, 0.f};
    return {n->yaw, n->pitch, n->roll};
}

bool SceneNodeRef::setScale(float sx, float sy, float sz) {
    SceneHost *h = resolveHost(hostName_);
    SceneNode *n = resolveNode(hostName_, nodeId_);
    if (!h || !n) return false;
    n->sx = sx;
    n->sy = sy;
    n->sz = sz;
    h->markSubtreeDirtyById(nodeId_);
    return true;
}

float SceneNodeRef::getScaleX() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->sx : 0.f;
}

float SceneNodeRef::getScaleY() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->sy : 0.f;
}

float SceneNodeRef::getScaleZ() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->sz : 0.f;
}

std::vector<float> SceneNodeRef::getScale() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    if (!n) return {0.f, 0.f, 0.f};
    return {n->sx, n->sy, n->sz};
}

bool SceneNodeRef::setVisible(bool v) {
    SceneNode *n = resolveNode(hostName_, nodeId_);
    if (!n) return false;
    n->visible   = v;
    SceneHost *h = resolveHost(hostName_);
    if (h) h->markSubtreeDirtyById(nodeId_);
    return true;
}

bool SceneNodeRef::isVisible() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->visible : false;
}

float SceneNodeRef::getWorldPositionX() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->world[3][0] : 0.f;
}

float SceneNodeRef::getWorldPositionY() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->world[3][1] : 0.f;
}

float SceneNodeRef::getWorldPositionZ() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    return n ? n->world[3][2] : 0.f;
}

std::vector<float> SceneNodeRef::getWorldPosition() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    if (!n) return {0.f, 0.f, 0.f};
    return {n->world[3][0], n->world[3][1], n->world[3][2]};
}

std::vector<float> SceneNodeRef::getWorldMatrix() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    if (!n) return std::vector<float>(16, 0.f);
    std::vector<float> out;
    out.reserve(16);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) out.push_back(n->world[c][r]);
    return out;
}

std::vector<float> SceneNodeRef::getForward() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    if (!n) return {0.f, 0.f, 1.f};
    glm::vec3 v = normalizedColumn(n->world, 2);
    return {v.x, v.y, v.z};
}

std::vector<float> SceneNodeRef::getRight() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    if (!n) return {1.f, 0.f, 0.f};
    glm::vec3 v = normalizedColumn(n->world, 0);
    return {v.x, v.y, v.z};
}

std::vector<float> SceneNodeRef::getUp() const {
    const SceneNode *n = resolveNode(hostName_, nodeId_);
    if (!n) return {0.f, 1.f, 0.f};
    glm::vec3 v = normalizedColumn(n->world, 1);
    return {v.x, v.y, v.z};
}

std::string SceneNodeRef::getParentId() const {
    SceneHost *h = resolveHost(hostName_);
    if (!h) return {};
    SceneNode *p = h->getParentById(nodeId_);
    return p ? p->id : std::string{};
}

int SceneNodeRef::getChildCount() const {
    SceneHost *h = resolveHost(hostName_);
    return h ? h->getChildCountById(nodeId_) : 0;
}

std::string SceneNodeRef::getChildIdAt(int ordinal) const {
    SceneHost *h = resolveHost(hostName_);
    if (!h) return {};
    SceneNode *c = h->getChildAtById(nodeId_, ordinal);
    return c ? c->id : std::string{};
}

std::string SceneNodeRef::getPath() const {
    SceneHost *h = resolveHost(hostName_);
    return h ? h->getPathById(nodeId_) : std::string{};
}

}  // namespace eve::scene
