#include "scene/SceneNodeRef.h"

#include "scene/Scene.h"
#include "scene/SceneHost.h"

#include <glm/glm.hpp>

#include <cmath>
#include <utility>

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
    glm::vec3 v(m[col]);
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
    n->yaw = yaw;
    n->pitch = pitch;
    n->roll = roll;
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
    n->visible = v;
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
