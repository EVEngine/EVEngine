#include "scene/TransformSystem.h"

#include "scene/SceneHost.h"

#ifdef EVENGINE_SCENE_AUDIO
#include "audio/Source.h"
#endif
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "physics/Body.h"
#include "physics/Body3D.h"

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

/** syncMode == 1: pull target state into the node before world propagation. */
void pullTargets(SceneHost *host) {
    if (!host) return;
    auto t = host->tree();
    for (size_t i = 0; i < t->nodes.size(); ++i) {
        SceneNode &n = t->nodes[i];
        for (auto &l : n.links) {
            if (l.syncMode != 1 || !l.target) continue;
            switch (l.kind) {
                case LinkKind::Physics2D: {
                    auto *b = static_cast<physics::Body *>(l.target);
                    n.x = b->getX();
                    n.y = b->getY();
                    n.roll = b->getAngle();
                    break;
                }
                case LinkKind::Physics3D: {
                    auto *b = static_cast<physics::Body3D *>(l.target);
                    n.x = b->getX();
                    n.y = b->getY();
                    n.z = b->getZ();
                    glm::quat q(b->getRotW(), b->getRotX(), b->getRotY(), b->getRotZ());
                    glm::vec3 e = glm::eulerAngles(q);  // pitch(x), yaw(y), roll(z)
                    n.pitch = e.x;
                    n.yaw = e.y;
                    n.roll = e.z;
                    break;
                }
                default:
                    break;
            }
            host->markSubtreeDirty(int(i));
        }
    }
}

/** Incremental propagation: recompute only dirty subtrees; sync their links. */
void pushTarget(const SceneNode &n, const SceneLink &l);
void updateNodeIncremental(SceneHost::Tree &tree, int nodeIndex,
                           const glm::mat4 &parentWorld, bool force) {
    SceneNode &n = tree.nodes[size_t(nodeIndex)];
    const bool need = force || n.subtreeDirty;
    if (need) {
        n.world = parentWorld * localMatrix(n);
        n.localDirty = false;
        n.subtreeDirty = false;
        for (auto &l : n.links) {
            if (l.syncMode == 0) pushTarget(n, l);
        }
    }
    for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
        updateNodeIncremental(tree, c, n.world, need);
    }
}

/** syncMode == 0: push node state into the linked target. */
void pushTarget(const SceneNode &n, const SceneLink &l) {
    if (!l.target) return;
    switch (l.kind) {
        case LinkKind::Renderable2D:
            writeWorldToRenderable2D(n, static_cast<graphics::Renderable2D *>(l.target));
            break;
        case LinkKind::Renderable3D:
            writeWorldToRenderable3D(n, static_cast<graphics::Renderable3D *>(l.target));
            break;
        case LinkKind::Physics2D: {
            auto *b = static_cast<physics::Body *>(l.target);
            b->setPosition(n.x, n.y);
            b->setAngle(n.roll);
            break;
        }
        case LinkKind::Physics3D: {
            auto *b = static_cast<physics::Body3D *>(l.target);
            b->setPosition(n.x, n.y, n.z);
            glm::quat q = glm::angleAxis(n.yaw, glm::vec3(0.f, 1.f, 0.f)) *
                          glm::angleAxis(n.pitch, glm::vec3(1.f, 0.f, 0.f)) *
                          glm::angleAxis(n.roll, glm::vec3(0.f, 0.f, 1.f));
            b->setRotation(q.x, q.y, q.z, q.w);
            break;
        }
        case LinkKind::Camera3D: {
            auto *c = static_cast<graphics::Camera3D *>(l.target);
            c->setEye(n.world[3][0], n.world[3][1], n.world[3][2]);
            break;
        }
        case LinkKind::Audio3D: {
#ifdef EVENGINE_SCENE_AUDIO
            auto *s = static_cast<audio::Source *>(l.target);
            s->setPosition(n.world[3][0], n.world[3][1], n.world[3][2]);
#endif
            break;
        }
        default:
            break;
    }
}

void syncLinks(SceneHost *host) {
    if (!host) return;
    auto t = host->tree();
    for (auto &n : t->nodes) {
        for (auto &l : n.links) {
            if (l.syncMode == 0) pushTarget(n, l);
        }
    }
}

/**
 * Best-effort liveness check for a linked target. ECS entities (renderables,
 * cameras) die via ecs::DestroyEntity; physics bodies expose raw()/isValid().
 * audio::Source has no liveness API, so it is always assumed alive.
 */
bool linkTargetAlive(const SceneLink &l) {
    if (!l.target) return false;
    switch (l.kind) {
        case LinkKind::Renderable2D:
        case LinkKind::Renderable3D:
        case LinkKind::Camera3D:
            return ecs::is_entity_visible(static_cast<const ecs::Entity *>(l.target));
        case LinkKind::Physics2D:
            return static_cast<const physics::Body *>(l.target)->raw() != nullptr;
        case LinkKind::Physics3D:
            return static_cast<const physics::Body3D *>(l.target)->isValid();
        case LinkKind::Audio3D:
        default:
            return true;
    }
}

/** Drop links whose target was destroyed (prevents dangling-pointer sync). */
void purgeDeadLinks(SceneHost::Tree &tree) {
    for (auto &n : tree.nodes) {
        for (auto it = n.links.begin(); it != n.links.end();) {
            if (linkTargetAlive(*it)) {
                ++it;
            } else {
                it = n.links.erase(it);
            }
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
    purgeDeadLinks(*t);
    pullTargets(host);

    // Legacy coarse path: tree-level transformDirty or direct localDirty writes
    // without subtree marking → full recompute + full link sync.
    bool legacy = t->transformDirty;
    if (!legacy) {
        for (const auto &n : t->nodes) {
            if (n.localDirty && !n.subtreeDirty) {
                legacy = true;
                break;
            }
        }
    }
    if (legacy) {
        updateNode(*t, t->root, glm::mat4(1.f));
        syncLinks(host);
        for (auto &n : t->nodes) {
            n.localDirty = false;
            n.subtreeDirty = false;
        }
        t->transformDirty = false;
        return;
    }

    // Incremental: only dirty subtrees recomputed, only their links synced.
    bool anySubtree = false;
    for (const auto &n : t->nodes) {
        if (n.subtreeDirty) {
            anySubtree = true;
            break;
        }
    }
    if (!anySubtree) {
        t->transformDirty = false;
        return;  // fully clean tree: zero work
    }
    updateNodeIncremental(*t, t->root, glm::mat4(1.f), false);
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
