// Registers the scene link kinds backed by graphics: 2D and 3D renderables and
// the 3D camera.
//
// The world-matrix decomposition used to live in scene/TransformSystem.cpp,
// which forced scene to include graphics. Keeping it here puts the casts where
// the types are complete and lets scene stay unaware of what it is driving.

#include "common/ECS.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "scene/SceneHost.h"
#include "scene/SceneLink.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace eve::graphics {
namespace {

using eve::scene::LinkOps;
using eve::scene::SceneNode;

void pushRenderable3D(const SceneNode &n, void *target) {
    auto *r = static_cast<Renderable3D *>(target);
    auto t = r->transform();
    const glm::mat4 &w = n.world;
    t->x = w[3][0];
    t->y = w[3][1];
    t->z = w[3][2];

    glm::vec3 c0(w[0]);
    glm::vec3 c1(w[1]);
    glm::vec3 c2(w[2]);
    t->sx = glm::length(c0);
    t->sy = glm::length(c1);
    t->sz = glm::length(c2);
    if (t->sx > 1e-8f) c0 /= t->sx;
    if (t->sy > 1e-8f) c1 /= t->sy;
    if (t->sz > 1e-8f) c2 /= t->sz;

    glm::mat3 rot(c0, c1, c2);
    glm::quat q = glm::quat_cast(rot);
    glm::vec3 euler = glm::eulerAngles(q);  // pitch(x), yaw(y), roll(z)
    t->pitch = euler.x;
    t->yaw = euler.y;
    t->roll = euler.z;

    r->meshRenderer()->visible = n.visible;
}

void pushRenderable2D(const SceneNode &n, void *target) {
    auto *r = static_cast<Renderable2D *>(target);
    auto t = r->transform();
    const glm::mat4 &w = n.world;
    t->x = w[3][0];
    t->y = w[3][1];

    glm::vec3 c0(w[0]);
    glm::vec3 c1(w[1]);
    t->sx = glm::length(c0);
    t->sy = glm::length(c1);
    if (t->sx > 1e-8f) c0 /= t->sx;
    // 2D rotation from first column in XY
    t->rot = std::atan2(c0.y, c0.x);

    r->sprite()->visible = n.visible;
}

void pushCamera3D(const SceneNode &n, void *target) {
    auto *c = static_cast<Camera3D *>(target);
    c->setEye(n.world[3][0], n.world[3][1], n.world[3][2]);
}

/** These are all ECS entities, destroyed through ecs::DestroyEntity. */
template <class T>
bool entityAlive(const void *target) {
    return ecs::is_entity_visible(static_cast<const ecs::Entity *>(static_cast<const T *>(target)));
}

struct Register {
    Register() {
        eve::scene::registerLinkKind(
            {"renderable2d", &pushRenderable2D, nullptr, &entityAlive<Renderable2D>});
        eve::scene::registerLinkKind(
            {"renderable3d", &pushRenderable3D, nullptr, &entityAlive<Renderable3D>});
        eve::scene::registerLinkKind(
            {"camera3d", &pushCamera3D, nullptr, &entityAlive<Camera3D>});
    }
} g_register;

}  // namespace
}  // namespace eve::graphics
