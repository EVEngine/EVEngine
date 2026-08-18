// Registers the scene link kinds backed by physics: 2D and 3D rigid bodies.
//
// These are the only kinds that can drive a node instead of following it
// (syncMode 1), so they are also the only ones supplying pullWorld.

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "scene/SceneHost.h"
#include "scene/SceneLink.h"

#include <glm/gtc/quaternion.hpp>

namespace eve::physics {
namespace {

using eve::scene::LinkOps;
using eve::scene::SceneNode;

void pushBody2D(const SceneNode &n, void *target) {
    auto *b = static_cast<Body *>(target);
    b->setPosition(n.x, n.y);
    b->setAngle(n.roll);
}

void pullBody2D(SceneNode &n, void *target) {
    auto *b = static_cast<Body *>(target);
    n.x = b->getX();
    n.y = b->getY();
    n.roll = b->getAngle();
}

bool aliveBody2D(const void *target) {
    return static_cast<const Body *>(target)->raw() != nullptr;
}

void pushBody3D(const SceneNode &n, void *target) {
    auto *b = static_cast<Body3D *>(target);
    b->setPosition(n.x, n.y, n.z);
    glm::quat q = glm::angleAxis(n.yaw, glm::vec3(0.f, 1.f, 0.f)) *
                  glm::angleAxis(n.pitch, glm::vec3(1.f, 0.f, 0.f)) *
                  glm::angleAxis(n.roll, glm::vec3(0.f, 0.f, 1.f));
    b->setRotation(q.x, q.y, q.z, q.w);
}

void pullBody3D(SceneNode &n, void *target) {
    auto *b = static_cast<Body3D *>(target);
    n.x = b->getX();
    n.y = b->getY();
    n.z = b->getZ();
    glm::quat q(b->getRotW(), b->getRotX(), b->getRotY(), b->getRotZ());
    glm::vec3 e = glm::eulerAngles(q);  // pitch(x), yaw(y), roll(z)
    n.pitch = e.x;
    n.yaw = e.y;
    n.roll = e.z;
}

bool aliveBody3D(const void *target) {
    return static_cast<const Body3D *>(target)->isValid();
}

struct Register {
    Register() {
        eve::scene::registerLinkKind({"physics2d", &pushBody2D, &pullBody2D, &aliveBody2D});
        eve::scene::registerLinkKind({"physics3d", &pushBody3D, &pullBody3D, &aliveBody3D});
    }
} g_register;

}  // namespace
}  // namespace eve::physics
