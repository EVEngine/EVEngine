#include "common/Capability.h"
#include "common/SceneQuery.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::scene {
namespace {

SceneHost *hostByName(const std::string &name) {
    if (name.empty()) return nullptr;
    if (ecs::current()->getManager<SceneHost>() == nullptr) return nullptr;
    auto view = ecs::View<SceneHost, SceneHost::Meta>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta] = *it;
        if (meta->entity && meta->name == name) return meta->entity;
    }
    return nullptr;
}

SceneHost *hostAt(int index) {
    if (index < 0 || ecs::current()->getManager<SceneHost>() == nullptr) return nullptr;
    auto view = ecs::View<SceneHost, SceneHost::Meta>();
    int i = 0;
    for (auto it = view.begin(); it != view.end(); ++it, ++i) {
        auto [meta] = *it;
        if (i == index) return meta->entity;
    }
    return nullptr;
}

SceneNodeInfo toInfo(SceneHost *host, const SceneNode &n) {
    SceneNodeInfo info;
    info.id = n.id;
    info.name = n.name;
    info.path = host->getPathById(n.id);
    info.visible = n.visible;
    info.x = n.x;
    info.y = n.y;
    info.z = n.z;
    info.yaw = n.yaw;
    info.pitch = n.pitch;
    info.roll = n.roll;
    info.sx = n.sx;
    info.sy = n.sy;
    info.sz = n.sz;
    if (auto *p = host->getParentById(n.id)) info.parent = p->id;
    for (int i = 0; i < host->getChildCountById(n.id); ++i) {
        if (auto *c = host->getChildAtById(n.id, i)) info.children.push_back(c->id);
    }
    return info;
}

class SceneQueryImpl final : public eve::ISceneQuery {
public:
    SceneHost *host() const {
        auto *s = eve::ModuleManager::getInstance<Scene>("Scene");
        return s ? s->current() : nullptr;
    }

    std::string activeHost() const override {
        auto *h = host();
        return h ? h->getName() : std::string();
    }

    int hostCount() const override {
        if (ecs::current()->getManager<SceneHost>() == nullptr) return 0;
        auto view = ecs::View<SceneHost, SceneHost::Meta>();
        int n = 0;
        for (auto it = view.begin(); it != view.end(); ++it) ++n;
        return n;
    }

    std::string hostNameAt(int index) const override {
        auto *h = hostAt(index);
        return h ? h->getName() : std::string();
    }

    int nodeCount() const override {
        auto *h = host();
        return h ? h->getNodeCount() : 0;
    }

    std::string rootId() const override {
        auto *h = host();
        auto *r = h ? h->getRoot() : nullptr;
        return r ? r->id : std::string();
    }

    std::vector<SceneNodeInfo> nodes(int limit) const override {
        return nodesOf(host() ? host()->getName() : std::string(), limit);
    }

    std::vector<SceneNodeInfo> nodesOf(const std::string &hostName, int limit) const override {
        std::vector<SceneNodeInfo> out;
        SceneHost *h = hostName.empty() ? host() : hostByName(hostName);
        if (!h) return out;
        h->walkDepthFirst([&](SceneHost *, int, SceneNode &n) {
            if (static_cast<int>(out.size()) >= limit) return;
            out.push_back(toInfo(h, n));
        });
        return out;
    }

    bool getNode(const std::string &id, SceneNodeInfo *out) const override {
        auto *h = host();
        return h ? getNodeIn(h->getName(), id, out) : false;
    }

    bool getNodeIn(const std::string &hostName, const std::string &id,
                   SceneNodeInfo *out) const override {
        SceneHost *h = hostName.empty() ? host() : hostByName(hostName);
        SceneNode *n = h ? h->findById(id) : nullptr;
        if (!n || !out) return false;
        *out = toInfo(h, *n);
        return true;
    }

    bool setNodeTransform(const std::string &id, float x, float y, float z) override {
        auto *h = host();
        SceneNode *n = h ? h->findById(id) : nullptr;
        if (!n) return false;
        n->x = x;
        n->y = y;
        n->z = z;
        h->markTransformDirty();
        return true;
    }

    bool setNodeVisible(const std::string &id, bool visible) override {
        auto *h = host();
        SceneNode *n = h ? h->findById(id) : nullptr;
        if (!n) return false;
        n->visible = visible;
        return true;
    }

    void syncTransforms() override { TransformSystem::updateAll(); }
};

}  // namespace

void registerSceneCapabilities() {
    static SceneQueryImpl impl;
    eve::cap::provide<eve::ISceneQuery>(&impl);
}

}  // namespace eve::scene
