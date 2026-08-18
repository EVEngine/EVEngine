#include "scene/NodeDesc.h"

#include "scene/SceneHost.h"

#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace eve::scene {
namespace {

int appendNode(SceneHost::Tree &tree, NodeDesc &&desc, int parentIndex) {
    const int index = int(tree.nodes.size());
    SceneNode node;
    node.id = std::move(desc.id);
    node.key = desc.key.empty() ? node.id : std::move(desc.key);
    node.name = desc.name.empty() ? node.id : std::move(desc.name);
    node.space = std::move(desc.space);
    if (node.space.empty()) node.space = "3d";
    node.visible = desc.visible;
    node.tags = std::move(desc.tags);
    node.layer = desc.layer;
    node.bminX = desc.bminX;
    node.bminY = desc.bminY;
    node.bminZ = desc.bminZ;
    node.bmaxX = desc.bmaxX;
    node.bmaxY = desc.bmaxY;
    node.bmaxZ = desc.bmaxZ;
    node.hasBounds = desc.hasBounds;
    node.x = desc.x;
    node.y = desc.y;
    node.z = desc.z;
    node.yaw = desc.yaw;
    node.pitch = desc.pitch;
    node.roll = desc.roll;
    node.sx = desc.sx;
    node.sy = desc.sy;
    node.sz = desc.sz;
    node.localDirty = true;
    node.world = glm::mat4(1.f);
    node.firstChild = -1;
    node.nextSibling = -1;
    node.parent = parentIndex;

    tree.nodes.push_back(std::move(node));

    int prevChild = -1;
    int firstChild = -1;
    for (auto &child : desc.children) {
        int childIndex = appendNode(tree, std::move(child), index);
        if (firstChild < 0) firstChild = childIndex;
        if (prevChild >= 0) tree.nodes[size_t(prevChild)].nextSibling = childIndex;
        prevChild = childIndex;
    }
    tree.nodes[size_t(index)].firstChild = firstChild;
    return index;
}

bool structureMatches(const SceneHost::Tree &tree, int nodeIndex, const NodeDesc &desc) {
    if (nodeIndex < 0 || nodeIndex >= int(tree.nodes.size())) return false;
    const SceneNode &n = tree.nodes[size_t(nodeIndex)];
    const std::string &dk = desc.reconcileKey();
    if (!dk.empty() && !n.key.empty() && n.key != dk) return false;

    std::vector<std::string> oldKeys;
    for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
        oldKeys.push_back(tree.nodes[size_t(c)].key);
    }
    if (oldKeys.size() != desc.children.size()) return false;
    for (size_t i = 0; i < desc.children.size(); ++i) {
        const std::string &ck = desc.children[i].reconcileKey();
        if (ck.empty() || oldKeys[i].empty()) {
            if (!(ck.empty() && oldKeys[i].empty())) return false;
        } else if (ck != oldKeys[i]) {
            return false;
        }
    }

    int child = n.firstChild;
    for (const auto &ch : desc.children) {
        if (!structureMatches(tree, child, ch)) return false;
        child = tree.nodes[size_t(child)].nextSibling;
    }
    return true;
}

/**
 * Order-independent structure match: same node key, same child key multiset,
 * and every child subtree matches recursively. Enables reconcile moves.
 */
bool structureMatchesSet(const SceneHost::Tree &tree, int nodeIndex,
                         const NodeDesc &desc) {
    if (nodeIndex < 0 || nodeIndex >= int(tree.nodes.size())) return false;
    const SceneNode &n = tree.nodes[size_t(nodeIndex)];
    const std::string &dk = desc.reconcileKey();
    if (!dk.empty() && !n.key.empty() && n.key != dk) return false;

    std::vector<std::string> oldKeys;
    for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
        oldKeys.push_back(tree.nodes[size_t(c)].key);
    }
    if (oldKeys.size() != desc.children.size()) return false;

    std::unordered_multiset<std::string> want;
    for (const auto &ch : desc.children) want.insert(ch.reconcileKey());
    for (const auto &k : oldKeys) {
        auto it = want.find(k);
        if (it == want.end()) return false;
        want.erase(it);
    }

    // Pair every old child with a same-key desc child and recurse.
    std::vector<bool> used(desc.children.size(), false);
    for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
        bool found = false;
        const std::string &ck = tree.nodes[size_t(c)].key;
        for (size_t i = 0; i < desc.children.size(); ++i) {
            if (used[i]) continue;
            const std::string &dk2 = desc.children[i].reconcileKey();
            if (ck == dk2 || (ck.empty() && dk2.empty())) {
                if (structureMatchesSet(tree, c, desc.children[i])) {
                    used[i] = true;
                    found = true;
                    break;
                }
            }
        }
        if (!found) return false;
    }
    return true;
}

/** Re-link a node's children to match desc order (recursively). */
bool reorderChildrenRecursive(SceneHost::Tree &tree, int nodeIndex,
                              const NodeDesc &desc) {
    SceneNode &n = tree.nodes[size_t(nodeIndex)];
    std::unordered_map<std::string, std::vector<int>> byKey;
    for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
        byKey[tree.nodes[size_t(c)].key].push_back(c);
    }
    std::vector<int> order;
    order.reserve(desc.children.size());
    for (const auto &ch : desc.children) {
        auto it = byKey.find(ch.reconcileKey());
        if (it == byKey.end() || it->second.empty()) return false;
        order.push_back(it->second.back());
        it->second.pop_back();
    }
    if (order.empty()) {
        n.firstChild = -1;
    } else {
        n.firstChild = order[0];
        for (size_t i = 0; i < order.size(); ++i) {
            tree.nodes[size_t(order[i])].nextSibling =
                (i + 1 < order.size()) ? order[i + 1] : -1;
        }
    }
    for (size_t i = 0; i < order.size(); ++i) {
        if (!reorderChildrenRecursive(tree, order[i], desc.children[i])) return false;
    }
    return true;
}

void patchProps(SceneHost *host, int nodeIndex, NodeDesc &&desc) {
    SceneHost::Tree &tree = *host->tree();
    SceneNode &n = tree.nodes[size_t(nodeIndex)];
    n.visible = desc.visible;
    n.tags = desc.tags;
    n.layer = desc.layer;
    n.bminX = desc.bminX;
    n.bminY = desc.bminY;
    n.bminZ = desc.bminZ;
    n.bmaxX = desc.bmaxX;
    n.bmaxY = desc.bmaxY;
    n.bmaxZ = desc.bmaxZ;
    n.hasBounds = desc.hasBounds;
    n.x = desc.x;
    n.y = desc.y;
    n.z = desc.z;
    n.yaw = desc.yaw;
    n.pitch = desc.pitch;
    n.roll = desc.roll;
    n.sx = desc.sx;
    n.sy = desc.sy;
    n.sz = desc.sz;
    if (!desc.space.empty()) n.space = desc.space;
    if (!desc.id.empty()) n.id = desc.id;
    if (!desc.name.empty()) n.name = desc.name;
    host->markSubtreeDirty(nodeIndex);
    host->fireEvent("node_changed", n.id,
                    n.parent >= 0 ? tree.nodes[size_t(n.parent)].id : "");

    int child = n.firstChild;
    for (auto &ch : desc.children) {
        patchProps(host, child, std::move(ch));
        child = tree.nodes[size_t(child)].nextSibling;
    }
}

}  // namespace

void validateUniqueIds(const NodeDesc &root) {
    std::unordered_set<std::string> seen;
    std::function<void(const NodeDesc &)> walk = [&](const NodeDesc &d) {
        if (!d.id.empty() && !seen.insert(d.id).second) {
            throw std::runtime_error("scene: duplicate node id '" + d.id + "'");
        }
        for (const auto &c : d.children) walk(c);
    };
    walk(root);
}

NodeDesc node(std::string id, std::vector<NodeDesc> children, std::string name) {
    NodeDesc d;
    d.id = std::move(id);
    d.key = d.id;
    d.name = name.empty() ? d.id : std::move(name);
    d.children = std::move(children);
    return d;
}

NodeDesc group(std::vector<NodeDesc> children, std::string id) {
    NodeDesc d;
    d.id = std::move(id);
    d.key = d.id;
    d.name = d.id.empty() ? "group" : d.id;
    d.children = std::move(children);
    return d;
}

NodeDesc when(bool cond, NodeDesc child) {
    if (!cond) return group({}, "__when_empty");
    return child;
}

NodeDesc whenElse(bool cond, NodeDesc ifTrue, NodeDesc ifFalse) {
    return cond ? std::move(ifTrue) : std::move(ifFalse);
}

void applyTree(SceneHost *host, NodeDesc root) {
    if (!host) return;
    validateUniqueIds(root);
    auto t = host->tree();

    std::unordered_set<std::string> oldIds;
    for (const auto &n : t->nodes) {
        if (!n.id.empty()) oldIds.insert(n.id);
    }

    std::unordered_map<std::string, std::vector<SceneLink>> saved;
    for (const auto &n : t->nodes) {
        if (!n.links.empty() && !n.id.empty()) saved[n.id] = n.links;
    }

    // Preserve lazy SceneObject bindings by node id (script entities stay alive
    // across full rebuilds; orphaned SceneObjects are torn down by Scene::prune).
    std::unordered_map<std::string, uint32_t> savedObjects;
    for (const auto &n : t->nodes) {
        if (n.objectId != 0 && !n.id.empty()) savedObjects[n.id] = n.objectId;
    }

    t->nodes.clear();
    t->root = -1;
    t->root = appendNode(*t, std::move(root), -1);
    for (auto &n : t->nodes) {
        auto it = saved.find(n.id);
        if (it != saved.end()) {
            n.links = it->second;
        }
        auto oit = savedObjects.find(n.id);
        if (oit != savedObjects.end()) n.objectId = oit->second;
    }
    host->invalidateIndex();
    t->dirty = true;
    t->transformDirty = true;

    // Node lifecycle events: ids that disappeared were removed; new ids added.
    std::unordered_set<std::string> newIds;
    for (const auto &n : t->nodes) {
        if (!n.id.empty()) newIds.insert(n.id);
    }
    for (const auto &id : oldIds) {
        if (!newIds.count(id)) host->fireEvent("node_removed", id);
    }
    for (const auto &n : t->nodes) {
        if (!oldIds.count(n.id)) {
            host->fireEvent("node_added", n.id,
                            n.parent >= 0 ? t->nodes[size_t(n.parent)].id : "");
        }
    }
}

bool applyTreeReconcile(SceneHost *host, NodeDesc root) {
    if (!host) return true;
    auto t = host->tree();
    if (t->root < 0 || t->nodes.empty() ||
        !structureMatchesSet(*t, t->root, root)) {
        applyTree(host, std::move(root));
        return true;
    }
    // Same key set, maybe different order: relink siblings first so patching
    // follows the new order without rebuilding the arena (links/objects keep
    // their identity).
    if (!reorderChildrenRecursive(*t, t->root, root)) {
        applyTree(host, std::move(root));
        return true;
    }
    patchProps(host, t->root, std::move(root));
    t->dirty = false;
    t->transformDirty = true;
    return false;
}

}  // namespace eve::scene
