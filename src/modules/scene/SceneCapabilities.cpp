#include "common/Capability.h"
#include "common/ProcgenSceneSink.h"
#include "common/SceneQuery.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::scene {
namespace {

template <class T>
T *borrowSceneResult(eve::Result<T *> result) {
    if (!result.ok()) return nullptr;
    return std::move(result).takeValue();
}

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
        SceneNode *n = h ? borrowSceneResult(h->findById(id)) : nullptr;
        if (!n || !out) return false;
        *out = toInfo(h, *n);
        return true;
    }

    bool setNodeTransform(const std::string &id, float x, float y, float z) override {
        auto *h = host();
        SceneNode *n = h ? borrowSceneResult(h->findById(id)) : nullptr;
        if (!n) return false;
        n->x = x;
        n->y = y;
        n->z = z;
        h->markTransformDirty();
        return true;
    }

    bool setNodeVisible(const std::string &id, bool visible) override {
        auto *h = host();
        SceneNode *n = h ? borrowSceneResult(h->findById(id)) : nullptr;
        if (!n) return false;
        n->visible = visible;
        return true;
    }

    void syncTransforms() override { TransformSystem::updateAll(); }
};

class ProcgenSceneSinkImpl final : public eve::IProcgenSceneSink {
public:
    bool applyBatch(const std::string& batchId,
                    const std::vector<eve::ProcgenInstanceDesc>& instances) override {
        if (batchId.empty()) return false;
        SceneHost* host = hostByName(hostName(batchId));
        if (!host) host = borrowSceneResult(SceneHost::createHost(hostName(batchId)));
        if (!host) return false;

        NodeDesc root;
        root.id   = "pcg-root";
        root.key  = "pcg-root";
        root.name = batchId;
        root.tags = {"pcg", "pcg.batch"};
        root.children.reserve(instances.size());
        for (const auto& instance : instances) {
            NodeDesc child;
            child.id   = instance.id;
            child.key  = instance.id;
            child.name = instance.asset;
            child.x    = instance.x;
            child.y    = instance.y;
            child.z    = instance.z;
            child.yaw  = instance.yaw;
            child.sx   = instance.scaleX;
            child.sy   = instance.scaleY;
            child.sz   = instance.scaleZ;
            child.tags = {"pcg", "pcg.instance"};
            if (!instance.asset.empty()) child.tags.push_back("pcg.asset:" + instance.asset);
            root.children.push_back(std::move(child));
        }
        std::unordered_set<std::string> nextIds;
        std::unordered_map<uint64_t, std::string> nextPointIds;
        nextIds.reserve(instances.size());
        nextPointIds.reserve(instances.size());
        for (const auto& instance : instances) {
            if (!nextIds.insert(instance.id).second) return false;
            if (instance.sourcePointId != 0 && !nextPointIds.emplace(instance.sourcePointId, instance.id).second)
                return false;
        }
        const auto previous = ids_.find(batchId);
        const auto& previousIds = previous == ids_.end() ? emptyIds_ : previous->second;
        Stats stats;
        for (const auto& id : nextIds) {
            if (previousIds.find(id) == previousIds.end()) ++stats.created;
            else ++stats.reused;
        }
        for (const auto& id : previousIds) {
            if (nextIds.find(id) == nextIds.end()) ++stats.removed;
        }

        host->setVisible(true);
        host->setTreeReconcile(std::move(root));
        TransformSystem::updateHost(host);
        counts_[batchId] = int(instances.size());
        ids_[batchId]    = std::move(nextIds);
        instances_[batchId] = instances;
        pointIds_[batchId]   = std::move(nextPointIds);
        revisions_[batchId] = revisions_[batchId] + 1;
        stats_[batchId]  = stats;
        return true;
    }

    eve::Result<uint64_t> applyDelta(const std::string& batchId,
                                     const eve::ProcgenInstanceDelta& delta) override {
        const auto current = revisions_.find(batchId);
        if (batchId.empty() || current == revisions_.end())
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "procedural scene batch is not published", "batchId"));
        if (delta.baseRevision == 0 || current->second != delta.baseRevision ||
            delta.targetRevision != delta.baseRevision + 1)
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "procedural scene delta revision is stale", "baseRevision"));

        std::unordered_map<std::string, eve::ProcgenInstanceDesc> staged;
        for (const auto& instance : instances_.at(batchId)) staged.emplace(instance.id, instance);
        const auto& pointIds = pointIds_.at(batchId);
        std::unordered_set<std::string> removedIds;
        removedIds.reserve(delta.removedPointIds.size() + delta.removed.size());
        for (const auto pointId : delta.removedPointIds) {
            const auto found = pointIds.find(pointId);
            if (pointId == 0 || found == pointIds.end() || !removedIds.insert(found->second).second)
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "procedural scene delta removes an unknown source PointId",
                    "removedPointIds"));
        }
        for (const auto& id : delta.removed)
            if (id.empty() || !removedIds.insert(id).second)
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "procedural scene delta repeats a removed identity", "removed"));
        for (const auto& id : removedIds)
            if (staged.erase(id) != 1)
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "procedural scene delta removes an unknown identity", "removed"));
        for (const auto& instance : delta.updated) {
            if (instance.id.empty() || staged.find(instance.id) == staged.end())
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "procedural scene delta updates an unknown identity", "updated"));
            staged[instance.id] = instance;
        }
        for (const auto& instance : delta.added) {
            if (instance.id.empty() || !staged.emplace(instance.id, instance).second)
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "procedural scene delta adds a duplicate identity", "added"));
        }
        if (staged.size() != delta.targetOrder.size())
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "procedural scene delta target order is incomplete", "targetOrder"));
        std::vector<eve::ProcgenInstanceDesc> target;
        target.reserve(delta.targetOrder.size());
        for (const auto& id : delta.targetOrder) {
            auto found = staged.find(id);
            if (found == staged.end())
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "procedural scene delta target order has unknown identity", "targetOrder"));
            target.push_back(found->second);
            staged.erase(found);
        }
        if (!staged.empty() || !applyBatch(batchId, target))
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "procedural scene provider rejected delta commit", "batchId"));
        revisions_[batchId] = delta.targetRevision;
        return eve::Result<uint64_t>::success(delta.targetRevision);
    }

    uint64_t batchRevision(const std::string& batchId) const override {
        const auto found = revisions_.find(batchId);
        return found == revisions_.end() ? 0 : found->second;
    }

    bool removeBatch(const std::string& batchId) override {
        SceneHost* host = hostByName(hostName(batchId));
        if (!host) return false;
        NodeDesc root;
        root.id   = "pcg-root";
        root.key  = "pcg-root";
        root.name = batchId;
        root.tags = {"pcg", "pcg.batch"};
        host->setTree(std::move(root));
        host->setVisible(false);
        Stats stats;
        const auto ids = ids_.find(batchId);
        if (ids != ids_.end()) stats.removed = int(ids->second.size());
        stats_[batchId] = stats;
        counts_.erase(batchId);
        ids_.erase(batchId);
        instances_.erase(batchId);
        pointIds_.erase(batchId);
        revisions_.erase(batchId);
        return true;
    }

    int instanceCount(const std::string& batchId) const override {
        const auto found = counts_.find(batchId);
        return found == counts_.end() ? 0 : found->second;
    }
    int lastCreatedCount(const std::string& batchId) const override {
        const auto found = stats_.find(batchId);
        return found == stats_.end() ? 0 : found->second.created;
    }
    int lastReusedCount(const std::string& batchId) const override {
        const auto found = stats_.find(batchId);
        return found == stats_.end() ? 0 : found->second.reused;
    }
    int lastRemovedCount(const std::string& batchId) const override {
        const auto found = stats_.find(batchId);
        return found == stats_.end() ? 0 : found->second.removed;
    }

private:
    struct Stats {
        int created = 0;
        int reused  = 0;
        int removed = 0;
    };
    static std::string hostName(const std::string& batchId) { return "__pcg/" + batchId; }
    const std::unordered_set<std::string> emptyIds_;
    std::unordered_map<std::string, int> counts_;
    std::unordered_map<std::string, std::unordered_set<std::string>> ids_;
    std::unordered_map<std::string, std::vector<eve::ProcgenInstanceDesc>> instances_;
    std::unordered_map<std::string, std::unordered_map<uint64_t, std::string>> pointIds_;
    std::unordered_map<std::string, uint64_t> revisions_;
    std::unordered_map<std::string, Stats> stats_;
};

}  // namespace

void registerSceneCapabilities() {
    static SceneQueryImpl impl;
    static ProcgenSceneSinkImpl procgenSink;
    eve::cap::provide<eve::ISceneQuery>(&impl);
    eve::cap::provide<eve::IProcgenSceneSink>(&procgenSink);
}

}  // namespace eve::scene
