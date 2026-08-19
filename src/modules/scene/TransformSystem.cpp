#include "scene/TransformSystem.h"

#include "scene/SceneHost.h"
#include "scene/SceneLink.h"

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

/** syncMode == 1: pull target state into the node before world propagation. */
void pullTargets(SceneHost *host) {
    if (!host) return;
    auto t = host->tree();
    for (size_t i = 0; i < t->nodes.size(); ++i) {
        SceneNode &n = t->nodes[i];
        for (auto &l : n.links) {
            if (l.syncMode != 1 || !l.target) continue;
            const LinkOps *ops = linkOps(l.kind);
            if (!ops || !ops->pullWorld) continue;
            ops->pullWorld(n, l.target);
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
    const LinkOps *ops = linkOps(l.kind);
    if (ops) ops->pushWorld(n, l.target);
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
 * Best-effort liveness check for a linked target. Each kind knows how to answer
 * for its own type; a kind that cannot tell (an audio source has no liveness
 * API) leaves `alive` null and is assumed alive.
 */
bool linkTargetAlive(const SceneLink &l) {
    if (!l.target) return false;
    const LinkOps *ops = linkOps(l.kind);
    if (!ops || !ops->alive) return true;
    return ops->alive(l.target);
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
