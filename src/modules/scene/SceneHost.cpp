#include "scene/SceneHost.h"

#include "scene/NodeDesc.h"

#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"

#include <vector>

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

void walkDepthFirstImpl(SceneHost *host, int nodeIndex, const SceneHost::NodeVisitFn &fn) {
    if (!host || nodeIndex < 0 || !fn) return;
    auto t = host->tree();
    if (nodeIndex >= int(t->nodes.size())) return;
    fn(host, nodeIndex, t->nodes[size_t(nodeIndex)]);
    for (int c = t->nodes[size_t(nodeIndex)].firstChild; c >= 0;
         c = t->nodes[size_t(c)].nextSibling) {
        walkDepthFirstImpl(host, c, fn);
    }
}

void walkBreadthFirstImpl(SceneHost *host, int nodeIndex, const SceneHost::NodeVisitFn &fn) {
    if (!host || nodeIndex < 0 || !fn) return;
    auto t = host->tree();
    if (nodeIndex >= int(t->nodes.size())) return;
    std::vector<int> queue;
    queue.push_back(nodeIndex);
    for (size_t i = 0; i < queue.size(); ++i) {
        const int idx = queue[i];
        fn(host, idx, t->nodes[size_t(idx)]);
        for (int c = t->nodes[size_t(idx)].firstChild; c >= 0;
             c = t->nodes[size_t(c)].nextSibling) {
            queue.push_back(c);
        }
    }
}

std::vector<std::string> splitPath(const std::string &path) {
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : path) {
        if (ch == '/') {
            if (!cur.empty()) {
                parts.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
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

SceneNode *SceneHost::findByName(const std::string &name) {
    const int idx = findIndexByName(name);
    if (idx < 0) return nullptr;
    return &tree()->nodes[size_t(idx)];
}

SceneNode *SceneHost::findByPath(const std::string &path) {
    const int idx = findIndexByPath(path);
    if (idx < 0) return nullptr;
    return &tree()->nodes[size_t(idx)];
}

int SceneHost::findIndexById(const std::string &id) {
    if (id.empty()) return -1;
    auto t = tree();
    for (int i = 0; i < int(t->nodes.size()); ++i) {
        if (t->nodes[size_t(i)].id == id) return i;
    }
    return -1;
}

int SceneHost::findIndexByKey(const std::string &key) {
    if (key.empty()) return -1;
    auto t = tree();
    for (int i = 0; i < int(t->nodes.size()); ++i) {
        if (t->nodes[size_t(i)].key == key) return i;
    }
    return -1;
}

int SceneHost::findIndexByName(const std::string &name) {
    if (name.empty()) return -1;
    int found = -1;
    walkDepthFirst([&](SceneHost *, int index, SceneNode &n) {
        if (found >= 0) return;
        if (n.name == name) found = index;
    });
    return found;
}

int SceneHost::findIndexByPath(const std::string &path) {
    auto parts = splitPath(path);
    if (parts.empty()) return -1;
    auto t = tree();
    if (t->root < 0) return -1;

    int idx = t->root;
    size_t start = 0;
    if (t->nodes[size_t(idx)].id == parts[0] || t->nodes[size_t(idx)].name == parts[0]) {
        start = 1;
    }
    for (size_t p = start; p < parts.size(); ++p) {
        const std::string &want = parts[p];
        int match = -1;
        for (int child = t->nodes[size_t(idx)].firstChild; child >= 0;
             child = t->nodes[size_t(child)].nextSibling) {
            const SceneNode &n = t->nodes[size_t(child)];
            if (n.id == want || n.name == want) {
                match = child;
                break;
            }
        }
        if (match < 0) return -1;
        idx = match;
    }
    return idx;
}

bool SceneHost::hasNode(const std::string &id) { return findIndexById(id) >= 0; }

int SceneHost::getNodeCount() { return int(tree()->nodes.size()); }

int SceneHost::getRootIndex() { return tree()->root; }

SceneNode *SceneHost::getRoot() {
    const int r = tree()->root;
    if (r < 0) return nullptr;
    return &tree()->nodes[size_t(r)];
}

SceneNode *SceneHost::getNode(int index) {
    auto t = tree();
    if (index < 0 || index >= int(t->nodes.size())) return nullptr;
    return &t->nodes[size_t(index)];
}

int SceneHost::getParentIndex(int nodeIndex) {
    auto t = tree();
    if (nodeIndex < 0 || nodeIndex >= int(t->nodes.size())) return -1;
    return t->nodes[size_t(nodeIndex)].parent;
}

SceneNode *SceneHost::getParent(int nodeIndex) { return getNode(getParentIndex(nodeIndex)); }

SceneNode *SceneHost::getParentById(const std::string &id) { return getParent(findIndexById(id)); }

int SceneHost::getChildCount(int nodeIndex) {
    auto t = tree();
    if (nodeIndex < 0 || nodeIndex >= int(t->nodes.size())) return 0;
    int count = 0;
    for (int c = t->nodes[size_t(nodeIndex)].firstChild; c >= 0;
         c = t->nodes[size_t(c)].nextSibling) {
        ++count;
    }
    return count;
}

int SceneHost::getChildCountById(const std::string &id) { return getChildCount(findIndexById(id)); }

int SceneHost::getChildIndexAt(int parentIndex, int childOrdinal) {
    if (childOrdinal < 0) return -1;
    auto t = tree();
    if (parentIndex < 0 || parentIndex >= int(t->nodes.size())) return -1;
    int i = 0;
    for (int c = t->nodes[size_t(parentIndex)].firstChild; c >= 0;
         c = t->nodes[size_t(c)].nextSibling) {
        if (i == childOrdinal) return c;
        ++i;
    }
    return -1;
}

SceneNode *SceneHost::getChildAt(int parentIndex, int childOrdinal) {
    return getNode(getChildIndexAt(parentIndex, childOrdinal));
}

SceneNode *SceneHost::getChildAtById(const std::string &parentId, int childOrdinal) {
    return getChildAt(findIndexById(parentId), childOrdinal);
}

std::vector<int> SceneHost::getChildIndices(int nodeIndex) {
    std::vector<int> out;
    auto t = tree();
    if (nodeIndex < 0 || nodeIndex >= int(t->nodes.size())) return out;
    for (int c = t->nodes[size_t(nodeIndex)].firstChild; c >= 0;
         c = t->nodes[size_t(c)].nextSibling) {
        out.push_back(c);
    }
    return out;
}

std::vector<SceneNode *> SceneHost::getChildren(int nodeIndex) {
    std::vector<SceneNode *> out;
    for (int c : getChildIndices(nodeIndex)) out.push_back(&tree()->nodes[size_t(c)]);
    return out;
}

std::vector<std::string> SceneHost::getChildIds(const std::string &parentId) {
    std::vector<std::string> out;
    for (SceneNode *n : getChildren(findIndexById(parentId))) {
        if (n) out.push_back(n->id);
    }
    return out;
}

std::string SceneHost::getPath(int nodeIndex) {
    auto t = tree();
    if (nodeIndex < 0 || nodeIndex >= int(t->nodes.size())) return {};
    std::vector<std::string> parts;
    for (int i = nodeIndex; i >= 0; i = t->nodes[size_t(i)].parent) {
        parts.push_back(t->nodes[size_t(i)].id);
    }
    std::string path;
    for (int i = int(parts.size()) - 1; i >= 0; --i) {
        if (!path.empty()) path += '/';
        path += parts[size_t(i)];
    }
    return path;
}

std::string SceneHost::getPathById(const std::string &id) { return getPath(findIndexById(id)); }

bool SceneHost::isAncestorOf(int ancestorIndex, int nodeIndex) {
    auto t = tree();
    if (ancestorIndex < 0 || nodeIndex < 0) return false;
    if (ancestorIndex >= int(t->nodes.size()) || nodeIndex >= int(t->nodes.size())) return false;
    return isAncestor(*t, ancestorIndex, nodeIndex);
}

bool SceneHost::isAncestorOfById(const std::string &ancestorId, const std::string &nodeId) {
    return isAncestorOf(findIndexById(ancestorId), findIndexById(nodeId));
}

bool SceneHost::isDescendantOf(int nodeIndex, int ancestorIndex) {
    return isAncestorOf(ancestorIndex, nodeIndex);
}

bool SceneHost::isDescendantOfById(const std::string &nodeId, const std::string &ancestorId) {
    return isAncestorOfById(ancestorId, nodeId);
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

void SceneHost::walkDepthFirst(NodeVisitFn fn) { walkDepthFirstFrom(tree()->root, std::move(fn)); }

void SceneHost::walkDepthFirstFrom(int nodeIndex, NodeVisitFn fn) {
    walkDepthFirstImpl(this, nodeIndex, fn);
}

void SceneHost::walkBreadthFirst(NodeVisitFn fn) {
    walkBreadthFirstFrom(tree()->root, std::move(fn));
}

void SceneHost::walkBreadthFirstFrom(int nodeIndex, NodeVisitFn fn) {
    walkBreadthFirstImpl(this, nodeIndex, fn);
}

void SceneHost::walkChildren(int parentIndex, NodeVisitFn fn) {
    if (!fn) return;
    for (int c : getChildIndices(parentIndex)) {
        fn(this, c, tree()->nodes[size_t(c)]);
    }
}

void SceneHost::walkAncestors(int nodeIndex, NodeVisitFn fn) {
    if (!fn) return;
    auto t = tree();
    if (nodeIndex < 0 || nodeIndex >= int(t->nodes.size())) return;
    for (int p = t->nodes[size_t(nodeIndex)].parent; p >= 0; p = t->nodes[size_t(p)].parent) {
        fn(this, p, t->nodes[size_t(p)]);
    }
}

SceneNode *SceneHost::findIf(NodePredFn pred) { return findIfFrom(tree()->root, std::move(pred)); }

SceneNode *SceneHost::findIfFrom(int nodeIndex, NodePredFn pred) {
    if (!pred) return nullptr;
    SceneNode *found = nullptr;
    walkDepthFirstFrom(nodeIndex, [&](SceneHost *host, int index, SceneNode &node) {
        if (found) return;
        if (pred(host, index, node)) found = &node;
    });
    return found;
}

std::vector<SceneNode *> SceneHost::filter(NodePredFn pred) {
    return filterFrom(tree()->root, std::move(pred));
}

std::vector<SceneNode *> SceneHost::filterFrom(int nodeIndex, NodePredFn pred) {
    std::vector<SceneNode *> out;
    if (!pred) return out;
    walkDepthFirstFrom(nodeIndex, [&](SceneHost *host, int index, SceneNode &node) {
        if (pred(host, index, node)) out.push_back(&node);
    });
    return out;
}

std::vector<int> SceneHost::filterIndices(NodePredFn pred) {
    return filterIndicesFrom(tree()->root, std::move(pred));
}

std::vector<int> SceneHost::filterIndicesFrom(int nodeIndex, NodePredFn pred) {
    std::vector<int> out;
    if (!pred) return out;
    walkDepthFirstFrom(nodeIndex, [&](SceneHost *host, int index, SceneNode &node) {
        if (pred(host, index, node)) out.push_back(index);
    });
    return out;
}

std::vector<SceneNode *> SceneHost::findAllByName(const std::string &name) {
    return filter([&](SceneHost *, int, const SceneNode &n) { return n.name == name; });
}

std::vector<SceneNode *> SceneHost::findAllByKey(const std::string &key) {
    return filter([&](SceneHost *, int, const SceneNode &n) { return n.key == key; });
}

std::vector<SceneNode *> SceneHost::findAllVisible(bool visible) {
    return filter([&](SceneHost *, int, const SceneNode &n) { return n.visible == visible; });
}

std::vector<SceneNode *> SceneHost::findAllBySpace(const std::string &space) {
    return filter([&](SceneHost *, int, const SceneNode &n) { return n.space == space; });
}

std::vector<SceneNode *> SceneHost::findAllLinked() {
    return filter([&](SceneHost *, int, const SceneNode &n) {
        return n.linkTarget != nullptr && !n.linkKind.empty();
    });
}

std::vector<std::string> SceneHost::collectIds() { return collectIdsFrom(tree()->root); }

std::vector<std::string> SceneHost::collectIdsFrom(int nodeIndex) {
    std::vector<std::string> out;
    walkDepthFirstFrom(nodeIndex, [&](SceneHost *, int, SceneNode &n) { out.push_back(n.id); });
    return out;
}

std::vector<std::string> SceneHost::collectIdsWhere(NodePredFn pred) {
    std::vector<std::string> out;
    for (SceneNode *n : filter(std::move(pred))) {
        if (n) out.push_back(n->id);
    }
    return out;
}

std::vector<std::string> SceneHost::collectIdsByName(const std::string &name) {
    return collectIdsWhere([&](SceneHost *, int, const SceneNode &n) { return n.name == name; });
}

std::vector<std::string> SceneHost::collectIdsVisible(bool visible) {
    return collectIdsWhere([&](SceneHost *, int, const SceneNode &n) { return n.visible == visible; });
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
