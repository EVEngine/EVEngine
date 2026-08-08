#include "scene/TransformSystem.h"

#include "scene/SceneHost.h"

#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace eve::scene {
namespace {

glm::mat4 localMatrix(const SceneNode &n) {
    glm::mat4 m(1.f);
    m = glm::translate(m, glm::vec3(n.x, n.y, n.z));
    if (n.space == "2d") {
        m = glm::rotate(m, n.roll, glm::vec3(0.f, 0.f, 1.f));
    } else {
        m = glm::rotate(m, n.yaw, glm::vec3(0.f, 1.f, 0.f));
        m = glm::rotate(m, n.pitch, glm::vec3(1.f, 0.f, 0.f));
        m = glm::rotate(m, n.roll, glm::vec3(0.f, 0.f, 1.f));
    }
    m = glm::scale(m, glm::vec3(n.sx, n.sy, n.sz));
    return m;
}

void updateNode(SceneHost::Tree &tree, int nodeIndex, const glm::mat4 &parentWorld) {
    SceneNode &n = tree.nodes[size_t(nodeIndex)];
    n.world = parentWorld * localMatrix(n);
    n.localDirty = false;
    for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
        updateNode(tree, c, n.world);
    }
}

void writeWorldToRenderable3D(const SceneNode &n, graphics::Renderable3D *r) {
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

void writeWorldToRenderable2D(const SceneNode &n, graphics::Renderable2D *r) {
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

void syncLinks(SceneHost *host) {
    if (!host) return;
    auto t = host->tree();
    for (auto &n : t->nodes) {
        if (!n.linkTarget || n.linkKind.empty()) continue;
        if (n.linkKind == "renderable3d") {
            writeWorldToRenderable3D(n, static_cast<graphics::Renderable3D *>(n.linkTarget));
        } else if (n.linkKind == "renderable2d") {
            writeWorldToRenderable2D(n, static_cast<graphics::Renderable2D *>(n.linkTarget));
        }
    }
}

}  // namespace

void TransformSystem::updateHost(SceneHost *host) {
    if (!host) return;
    auto t = host->tree();
    if (t->root < 0 || t->nodes.empty()) {
        t->transformDirty = false;
        return;
    }
    updateNode(*t, t->root, glm::mat4(1.f));
    syncLinks(host);
    t->transformDirty = false;
}

void TransformSystem::updateAll() {
    if (ecs::current()->getManager<SceneHost>() == nullptr) return;
    auto view = ecs::View<SceneHost, SceneHost::Meta, SceneHost::Tree>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta, tree] = *it;
        (void)tree;
        if (!meta->entity) continue;
        updateHost(meta->entity);
    }
}

}  // namespace eve::scene
