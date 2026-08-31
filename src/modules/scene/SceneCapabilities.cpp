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

    eve::Result<uint64_t> replaceBatch(const std::string& batchId, uint64_t targetRevision,
                                       const std::vector<eve::ProcgenInstanceDesc>& instances) override {
        const uint64_t currentRevision = batchRevision(batchId);
        if (batchId.empty() || targetRevision == 0)
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "procedural scene snapshot requires a batch id and non-zero target revision", "targetRevision"));
        if (currentRevision >= targetRevision)
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "procedural scene snapshot revision is stale", "targetRevision"));
        std::unordered_set<std::string> instanceIds;
        std::unordered_set<uint64_t>    pointIds;
        instanceIds.reserve(instances.size());
        pointIds.reserve(instances.size());
        for (const auto& instance : instances) {
            if (instance.id.empty() || instance.sourcePointId == 0 || !instanceIds.insert(instance.id).second ||
                !pointIds.insert(instance.sourcePointId).second)
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict,
                    "procedural scene snapshot requires unique instance ids and source PointIds", "instances"));
        }
        if (!applyBatch(batchId, instances))
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "procedural scene provider rejected snapshot commit", "batchId"));
        revisions_[batchId] = targetRevision;
        return eve::Result<uint64_t>::success(targetRevision);
    }

    eve::Result<uint64_t> replaceBatches(const std::vector<eve::ProcgenBatchSnapshot>& snapshots) override {
        if (snapshots.empty())
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                       "procedural scene transaction requires at least one snapshot", "snapshots"));

        struct PreparedBatch {
            const eve::ProcgenBatchSnapshot*          snapshot = nullptr;
            SceneHost*                                host     = nullptr;
            SceneHost::Tree                           tree;
            std::unordered_set<std::string>           ids;
            std::unordered_map<uint64_t, std::string> pointIds;
            std::vector<std::string>                  addedIds;
            std::vector<std::string>                  reusedIds;
            std::vector<std::string>                  removedIds;
            Stats                                     stats;
        };
        std::vector<PreparedBatch> prepared;
        prepared.reserve(snapshots.size());
        std::unordered_set<std::string> batchIds;
        batchIds.reserve(snapshots.size());

        for (const auto& snapshot : snapshots) {
            if (snapshot.batchId.empty() || snapshot.targetRevision == 0)
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument,
                    "procedural scene transaction requires batch ids and non-zero revisions", "snapshots"));
            if (!batchIds.insert(snapshot.batchId).second)
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "procedural scene transaction repeats a batch id", "snapshots"));
            if (batchRevision(snapshot.batchId) >= snapshot.targetRevision)
                return eve::Result<uint64_t>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                           "procedural scene transaction contains a stale revision", snapshot.batchId));

            PreparedBatch batch;
            batch.snapshot = &snapshot;
            batch.ids.reserve(snapshot.instances.size());
            batch.pointIds.reserve(snapshot.instances.size());
            for (const auto& instance : snapshot.instances) {
                if (instance.id.empty() || instance.sourcePointId == 0 || !batch.ids.insert(instance.id).second ||
                    !batch.pointIds.emplace(instance.sourcePointId, instance.id).second)
                    return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::Conflict,
                        "procedural scene transaction requires unique instance ids and source PointIds",
                        snapshot.batchId));
            }
            const auto  previous    = ids_.find(snapshot.batchId);
            const auto& previousIds = previous == ids_.end() ? emptyIds_ : previous->second;
            for (const auto& id : batch.ids) {
                if (previousIds.find(id) == previousIds.end()) {
                    ++batch.stats.created;
                    batch.addedIds.push_back(id);
                } else {
                    ++batch.stats.reused;
                    batch.reusedIds.push_back(id);
                }
            }
            for (const auto& id : previousIds) {
                if (batch.ids.find(id) != batch.ids.end()) continue;
                ++batch.stats.removed;
                batch.removedIds.push_back(id);
            }

            batch.host    = hostByName(hostName(snapshot.batchId));
            NodeDesc root = makeRoot(snapshot.batchId, snapshot.instances);
            auto     tree = SceneHost::buildDetachedTree(batch.host ? &*batch.host->tree() : nullptr, std::move(root));
            if (!tree) return eve::Result<uint64_t>::failure(tree.status());
            batch.tree = std::move(tree).takeValue();
            prepared.push_back(std::move(batch));
        }

        auto nextCounts    = counts_;
        auto nextIds       = ids_;
        auto nextInstances = instances_;
        auto nextPointIds  = pointIds_;
        auto nextRevisions = revisions_;
        auto nextStats     = stats_;
        for (const auto& batch : prepared) {
            const auto& snapshot            = *batch.snapshot;
            nextCounts[snapshot.batchId]    = int(snapshot.instances.size());
            nextIds[snapshot.batchId]       = batch.ids;
            nextInstances[snapshot.batchId] = snapshot.instances;
            nextPointIds[snapshot.batchId]  = batch.pointIds;
            nextRevisions[snapshot.batchId] = snapshot.targetRevision;
            nextStats[snapshot.batchId]     = batch.stats;
        }

        for (auto& batch : prepared) {
            if (batch.host) continue;
            batch.host = borrowSceneResult(SceneHost::createHost(hostName(batch.snapshot->batchId)));
            if (!batch.host)
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Failed, "procedural scene transaction could not allocate all target hosts",
                    batch.snapshot->batchId));
            batch.host->setVisible(false);
        }

        for (auto& batch : prepared) {
            *batch.host->tree() = std::move(batch.tree);
            batch.host->setVisible(true);
        }
        counts_.swap(nextCounts);
        ids_.swap(nextIds);
        instances_.swap(nextInstances);
        pointIds_.swap(nextPointIds);
        revisions_.swap(nextRevisions);
        stats_.swap(nextStats);
        for (const auto& batch : prepared) {
            for (const auto& id : batch.removedIds) batch.host->fireEvent("node_removed", id);
            for (const auto& id : batch.addedIds) batch.host->fireEvent("node_added", id, "pcg-root");
            for (const auto& id : batch.reusedIds) batch.host->fireEvent("node_changed", id, "pcg-root");
        }
        for (const auto& batch : prepared) TransformSystem::updateHost(batch.host);
        return eve::Result<uint64_t>::success(static_cast<uint64_t>(prepared.size()));
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
        std::unordered_map<uint64_t, std::string> stagedPointIds = pointIds_.at(batchId);
        std::unordered_set<std::string>           removedIds;
        removedIds.reserve(delta.removedPointIds.size() + delta.removed.size());
        for (const auto pointId : delta.removedPointIds) {
            const auto found = stagedPointIds.find(pointId);
            if (pointId == 0 || found == stagedPointIds.end() || !removedIds.insert(found->second).second)
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "procedural scene delta removes an unknown source PointId",
                    "removedPointIds"));
        }
        for (const auto& id : delta.removed)
            if (id.empty() || !removedIds.insert(id).second)
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "procedural scene delta repeats a removed identity", "removed"));
        for (const auto& id : removedIds) {
            const auto found = staged.find(id);
            if (found == staged.end())
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "procedural scene delta removes an unknown identity", "removed"));
            if (found->second.sourcePointId != 0) stagedPointIds.erase(found->second.sourcePointId);
            staged.erase(found);
        }
        for (const auto& instance : delta.updated) {
            if (instance.id.empty() || instance.sourcePointId == 0)
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument,
                    "procedural scene delta updates require an id and source PointId", "updated"));
            const auto identity = stagedPointIds.find(instance.sourcePointId);
            if (identity == stagedPointIds.end())
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "procedural scene delta updates an unknown identity", "updated"));
            if (identity->second != instance.id && staged.find(instance.id) != staged.end())
                return eve::Result<uint64_t>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                           "procedural scene delta update renames to a duplicate identity", "updated"));
            staged.erase(identity->second);
            staged.emplace(instance.id, instance);
            identity->second = instance.id;
        }
        for (const auto& instance : delta.added) {
            if (instance.id.empty() || instance.sourcePointId == 0 ||
                stagedPointIds.find(instance.sourcePointId) != stagedPointIds.end() ||
                !staged.emplace(instance.id, instance).second)
                return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "procedural scene delta adds a duplicate identity", "added"));
            stagedPointIds.emplace(instance.sourcePointId, instance.id);
        }
        if (!delta.targetPointOrder.empty() && !delta.targetOrder.empty())
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "procedural scene delta cannot mix PointId and compatibility target orders", "targetPointOrder"));
        std::vector<std::string> resolvedOrder;
        if (!delta.targetPointOrder.empty()) {
            resolvedOrder.reserve(delta.targetPointOrder.size());
            std::unordered_set<uint64_t> orderedPointIds;
            for (const auto pointId : delta.targetPointOrder) {
                const auto found = stagedPointIds.find(pointId);
                if (pointId == 0 || found == stagedPointIds.end() || !orderedPointIds.insert(pointId).second)
                    return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::Conflict,
                        "procedural scene delta target order has an unknown or duplicate source PointId",
                        "targetPointOrder"));
                resolvedOrder.push_back(found->second);
            }
        } else {
            resolvedOrder = delta.targetOrder;
        }
        if (staged.size() != resolvedOrder.size())
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "procedural scene delta target order is incomplete", "targetOrder"));
        std::vector<eve::ProcgenInstanceDesc> target;
        target.reserve(resolvedOrder.size());
        for (const auto& id : resolvedOrder) {
            auto found = staged.find(id);
            if (found == staged.end())
                return eve::Result<uint64_t>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                           "procedural scene delta target order has unknown identity", "targetOrder"));
            target.push_back(found->second);
            staged.erase(found);
        }
        if (!staged.empty())
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                       "procedural scene delta target order repeats an identity", "targetOrder"));
        return replaceBatch(batchId, delta.targetRevision, target);
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
    static NodeDesc makeRoot(const std::string& batchId, const std::vector<eve::ProcgenInstanceDesc>& instances) {
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
        return root;
    }
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
