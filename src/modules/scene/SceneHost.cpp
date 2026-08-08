#include "scene/SceneHost.h"

#include "scene/NodeDesc.h"

#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"

namespace eve::scene {
namespace {

uint32_t g_anonHostSeq = 0;

bool isAncestor(const SceneHost::Tree &tree, int ancestor, int node) {
    for (int p = node; p >= 0; p = tree.nodes[size_t(p)].parent) {
        if (p == ancestor) return true;
    }
    return false;
}

void unlinkFromParent(SceneHost::Tree &tree, int childIndex) {
    SceneNode &child = tree.nodes[size_t(childIndex)];
    const int parent = child.parent;
    if (parent < 0) return;
    SceneNode &p = tree.nodes[size_t(parent)];
    if (p.firstChild == childIndex) {
        p.firstChild = child.nextSibling;
    } else {
        for (int c = p.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
            if (tree.nodes[size_t(c)].nextSibling == childIndex) {
                tree.nodes[size_t(c)].nextSibling = child.nextSibling;
                break;
            }
        }
    }
    child.nextSibling = -1;
    child.parent = -1;
}

void linkAsLastChild(SceneHost::Tree &tree, int parentIndex, int childIndex) {
    SceneNode &parent = tree.nodes[size_t(parentIndex)];
    SceneNode &child = tree.nodes[size_t(childIndex)];
    child.parent = parentIndex;
    child.nextSibling = -1;
    if (parent.firstChild < 0) {
        parent.firstChild = childIndex;
        return;
    }
    int c = parent.firstChild;
    while (tree.nodes[size_t(c)].nextSibling >= 0) c = tree.nodes[size_t(c)].nextSibling;
    tree.nodes[size_t(c)].nextSibling = childIndex;
}

void forEachDepthFirstImpl(SceneHost *host, int nodeIndex, void (*fn)(SceneHost *, int, void *),
                           void *user) {
    if (!host || nodeIndex < 0) return;
    fn(host, nodeIndex, user);
    auto t = host->tree();
    for (int c = t->nodes[size_t(nodeIndex)].firstChild; c >= 0;
         c = t->nodes[size_t(c)].nextSibling) {
        forEachDepthFirstImpl(host, c, fn, user);
    }
}

}  // namespace

SceneHost *SceneHost::createHost(const std::string &name) {
    SceneHost *h = SceneHost::create();
    h->meta()->entity = h;
    if (name.empty()) {
        h->meta()->name = "host" + std::to_string(++g_anonHostSeq);
    } else {
        h->meta()->name = name;
    }
    return h;
}

void SceneHost::setName(const std::string &name) { meta()->name = name; }

const std::string &SceneHost::getName() { return meta()->name; }

void SceneHost::setTree(NodeDesc root) { applyTree(this, std::move(root)); }

bool SceneHost::setTreeReconcile(NodeDesc root) { return applyTreeReconcile(this, std::move(root)); }

SceneNode *SceneHost::findById(const std::string &id) {
    if (id.empty()) return nullptr;
    auto t = tree();
    for (auto &n : t->nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

SceneNode *SceneHost::findByKey(const std::string &key) {
    if (key.empty()) return nullptr;
    auto t = tree();
    for (auto &n : t->nodes) {
        if (n.key == key) return &n;
    }
    return nullptr;
}

int SceneHost::findIndexById(const std::string &id) {
    if (id.empty()) return -1;
    auto t = tree();
    for (int i = 0; i < int(t->nodes.size()); ++i) {
        if (t->nodes[size_t(i)].id == id) return i;
    }
    return -1;
}

bool SceneHost::setParent(int childIndex, int parentIndex) {
    auto t = tree();
    if (childIndex < 0 || childIndex >= int(t->nodes.size())) return false;
    if (parentIndex < -1 || parentIndex >= int(t->nodes.size())) return false;
    if (childIndex == parentIndex) return false;
    if (parentIndex >= 0 && isAncestor(*t, childIndex, parentIndex)) return false;

    unlinkFromParent(*t, childIndex);
    if (parentIndex < 0) {
        t->nodes[size_t(childIndex)].parent = -1;
        t->nodes[size_t(childIndex)].nextSibling = -1;
        t->nodes[size_t(childIndex)].localDirty = true;
        t->transformDirty = true;
        return true;
    }
    linkAsLastChild(*t, parentIndex, childIndex);
    t->nodes[size_t(childIndex)].localDirty = true;
    t->transformDirty = true;
    return true;
}

bool SceneHost::addChild(int parentIndex, int childIndex) { return setParent(childIndex, parentIndex); }

bool SceneHost::removeChild(int parentIndex, int childIndex) {
    auto t = tree();
    if (childIndex < 0 || childIndex >= int(t->nodes.size())) return false;
    if (t->nodes[size_t(childIndex)].parent != parentIndex) return false;
    unlinkFromParent(*t, childIndex);
    t->nodes[size_t(childIndex)].localDirty = true;
    t->transformDirty = true;
    return true;
}

void SceneHost::forEachDepthFirst(int nodeIndex, void (*fn)(SceneHost *, int, void *), void *user) {
    forEachDepthFirstImpl(this, nodeIndex, fn, user);
}

bool SceneHost::linkRenderable2D(const std::string &nodeId, graphics::Renderable2D *r) {
    SceneNode *n = findById(nodeId);
    if (!n || !r) return false;
    n->linkKind = "renderable2d";
    n->linkTarget = r;
    return true;
}

bool SceneHost::linkRenderable3D(const std::string &nodeId, graphics::Renderable3D *r) {
    SceneNode *n = findById(nodeId);
    if (!n || !r) return false;
    n->linkKind = "renderable3d";
    n->linkTarget = r;
    return true;
}

bool SceneHost::unlink(const std::string &nodeId) {
    SceneNode *n = findById(nodeId);
    if (!n) return false;
    n->linkKind.clear();
    n->linkTarget = nullptr;
    return true;
}

}  // namespace eve::scene
