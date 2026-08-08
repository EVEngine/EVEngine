#include "scene/NodeDesc.h"

#include "scene/SceneHost.h"

#include <unordered_map>

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

void patchProps(SceneHost::Tree &tree, int nodeIndex, NodeDesc &&desc) {
    SceneNode &n = tree.nodes[size_t(nodeIndex)];
    n.visible = desc.visible;
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
    n.localDirty = true;

    int child = n.firstChild;
    for (auto &ch : desc.children) {
        patchProps(tree, child, std::move(ch));
        child = tree.nodes[size_t(child)].nextSibling;
    }
}

}  // namespace

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
    auto t = host->tree();

    struct SavedLink {
        std::string kind;
        void *target = nullptr;
    };
    std::unordered_map<std::string, SavedLink> saved;
    for (const auto &n : t->nodes) {
        if (!n.linkKind.empty() && n.linkTarget && !n.id.empty()) {
            saved[n.id] = SavedLink{n.linkKind, n.linkTarget};
        }
    }

    t->nodes.clear();
    t->root = -1;
    t->root = appendNode(*t, std::move(root), -1);
    for (auto &n : t->nodes) {
        auto it = saved.find(n.id);
        if (it == saved.end()) continue;
        n.linkKind = it->second.kind;
        n.linkTarget = it->second.target;
    }
    t->dirty = true;
    t->transformDirty = true;
}

bool applyTreeReconcile(SceneHost *host, NodeDesc root) {
    if (!host) return true;
    auto t = host->tree();
    if (t->root < 0 || t->nodes.empty() || !structureMatches(*t, t->root, root)) {
        applyTree(host, std::move(root));
        return true;
    }
    patchProps(*t, t->root, std::move(root));
    t->dirty = false;
    t->transformDirty = true;
    return false;
}

}  // namespace eve::scene
