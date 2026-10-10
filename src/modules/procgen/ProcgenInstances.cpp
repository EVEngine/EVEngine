#include "procgen/Procgen.h"
#include "procgen/ProcgenLive.h"
#include "procgen/ProcgenScriptSupport.h"

#include "common/Capability.h"
#include "common/ProcgenSceneSink.h"
#include "common/ProcgenWorldQuery.h"
#include "common/SquirrelBinding.h"
#include "procgen/ProcgenScriptObjects.h"

#include "image/ImageData.h"

#include "procgen/GeneratorRegistry.h"
#include "procgen/heightmap/TerrainFile.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include "procgen/RuntimeGeneration.h"

namespace eve::procgen {

static std::string sceneInstanceId(const PointSet& points, size_t pointIndex,
                                   std::unordered_map<uint32_t, size_t>& seedOccurrences) {
    const auto& point      = points.points()[pointIndex];
    const auto  explicitId = points.attributes().getString(pointIndex, "instanceId");
    if (explicitId && !explicitId->empty()) return std::string(*explicitId);
    if (point.id != 0) return "pcg-id-" + std::to_string(point.id);
    return "pcg-" + std::to_string(point.seed) + "-" + std::to_string(seedOccurrences[point.seed]++);
}

static eve::ProcgenInstanceDesc sceneInstanceDesc(const PointSet& points, size_t pointIndex,
                                                  const std::string& assetAttribute, const std::string& defaultAsset,
                                                  std::unordered_map<uint32_t, size_t>& seedOccurrences) {
    const auto&              point = points.points()[pointIndex];
    eve::ProcgenInstanceDesc instance;
    instance.sourcePointId = point.id;
    instance.id            = sceneInstanceId(points, pointIndex, seedOccurrences);
    instance.asset         = defaultAsset;
    if (!assetAttribute.empty()) {
        const auto found = points.attributes().getString(pointIndex, assetAttribute);
        if (found) instance.asset = std::string(*found);
    }
    instance.x      = point.x;
    instance.y      = point.y;
    instance.z      = point.z;
    instance.yaw    = point.yaw;
    instance.scaleX = point.scaleX;
    instance.scaleY = point.scaleY;
    instance.scaleZ = point.scaleZ;
    instance.seed   = point.seed;
    return instance;
}

static eve::Result<std::vector<eve::ProcgenInstanceDesc>> sceneInstanceDescs(const PointSet&    points,
                                                                             const std::string& assetAttribute,
                                                                             const std::string& defaultAsset,
                                                                             bool               requireStablePointIds) {
    std::vector<eve::ProcgenInstanceDesc> instances;
    instances.reserve(points.points().size());
    std::unordered_map<uint32_t, size_t> seedOccurrences;
    std::unordered_set<std::string>      instanceIds;
    std::unordered_set<uint64_t>         pointIds;
    for (size_t pointIndex = 0; pointIndex < points.points().size(); ++pointIndex) {
        auto instance = sceneInstanceDesc(points, pointIndex, assetAttribute, defaultAsset, seedOccurrences);
        if (!instanceIds.insert(instance.id).second)
            return eve::Result<std::vector<eve::ProcgenInstanceDesc>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                       "procedural Scene snapshot contains a duplicate instance id", "instanceId"));
        if (requireStablePointIds && (instance.sourcePointId == 0 || !pointIds.insert(instance.sourcePointId).second))
            return eve::Result<std::vector<eve::ProcgenInstanceDesc>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "procedural Scene snapshot requires unique non-zero source PointIds",
                "pointId"));
        instances.push_back(std::move(instance));
    }
    return eve::Result<std::vector<eve::ProcgenInstanceDesc>>::success(std::move(instances));
}

eve::Result<void> Procgen::publishInstances(const std::string& batchId, ProcgenPointSetHandleRef points,
                                            const std::string& assetAttribute, const std::string& defaultAsset) {
    const auto view = resolvePointSet(points);
    if (batchId.empty() || !view.isBound())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "publishInstances requires a batch id and a live point-set handle", {}, {}, "procgen.squirrel"));
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "publishInstances scene sink is unavailable", {}, {}, "procgen.squirrel"));

    auto instances = sceneInstanceDescs(*view, assetAttribute, defaultAsset, false);
    if (!instances.ok()) return eve::Result<void>::failure(instances.status());
    if (!sink->applyBatch(batchId, instances.value()))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "publishInstances scene sink rejected batch", {}, {}, "procgen.squirrel"));
    return eve::Result<void>::success();
}

eve::Result<void> Procgen::removeInstances(const std::string& batchId) {
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (batchId.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "removeInstances requires a batch id", {}, {}, "procgen.squirrel"));
    if (!sink)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "removeInstances scene sink is unavailable", {}, {}, "procgen.squirrel"));
    if (!sink->removeBatch(batchId))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "removeInstances scene sink rejected batch", {}, {}, "procgen.squirrel"));
    return eve::Result<void>::success();
}

eve::Result<void> Procgen::publishCellInstances(const std::string& prefix, const ProcgenCellRequest& request,
                                                ProcgenPointSetHandleRef points, const std::string& assetAttribute,
                                                const std::string& defaultAsset) {
    if (prefix.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "publishCellInstances requires a prefix", {}, {},
                                                                 "procgen.squirrel"));
    const std::string batchId = prefix + "/L" + std::to_string(request.getLevel()) + "/" +
                                std::to_string(request.getX()) + "/" + std::to_string(request.getZ());
    return publishInstances(batchId, points, assetAttribute, defaultAsset);
}

eve::Result<uint64_t> Procgen::publishCellSnapshot(const std::string& prefix, const ProcgenCellRequest& request,
                                                   ProcgenPointSetHandleRef points, uint64_t targetRevision,
                                                   const std::string& assetAttribute, const std::string& defaultAsset) {
    const auto view = resolvePointSet(points);
    if (prefix.empty() || targetRevision == 0 || !view.isBound())
        return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
            !view.isBound() ? eve::DiagnosticCode::StaleHandle : eve::DiagnosticCode::InvalidArgument,
            "publishCellSnapshot requires a prefix, live point set, and non-zero target revision",
            "publishCellSnapshot", {}, "procgen.squirrel"));
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "publishCellSnapshot scene sink is unavailable", {}, {}, "procgen.squirrel"));
    auto instances = sceneInstanceDescs(*view, assetAttribute, defaultAsset, true);
    if (!instances.ok()) return eve::Result<uint64_t>::failure(instances.status());
    const std::string batchId = prefix + "/L" + std::to_string(request.getLevel()) + "/" +
                                std::to_string(request.getX()) + "/" + std::to_string(request.getZ());
    return sink->replaceBatch(batchId, targetRevision, instances.value());
}

eve::Result<uint64_t> Procgen::publishCellInstanceDelta(const std::string& prefix, const ProcgenCellRequest& request,
                                                        const PointDelta& delta, uint64_t targetRevision,
                                                        const std::string& assetAttribute,
                                                        const std::string& defaultAsset) {
    if (prefix.empty())
        return eve::Result<uint64_t>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "publishCellInstanceDelta requires a prefix",
                                                                     "prefix", {}, "procgen.squirrel"));
    if (targetRevision < 2)
        return eve::Result<uint64_t>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "publishCellInstanceDelta requires a target revision greater than one",
                                   "targetRevision", {}, "procgen.squirrel"));
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return eve::Result<uint64_t>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "publishCellInstanceDelta scene sink is unavailable",
                                   {}, {}, "procgen.squirrel"));

    eve::ProcgenInstanceDelta sceneDelta;
    sceneDelta.baseRevision     = targetRevision - 1;
    sceneDelta.targetRevision   = targetRevision;
    sceneDelta.removedPointIds  = delta.removed;
    sceneDelta.targetPointOrder = delta.targetOrder;
    std::unordered_map<uint32_t, size_t> seedOccurrences;
    sceneDelta.added.reserve(delta.added.points().size());
    for (size_t pointIndex = 0; pointIndex < delta.added.points().size(); ++pointIndex)
        sceneDelta.added.push_back(
            sceneInstanceDesc(delta.added, pointIndex, assetAttribute, defaultAsset, seedOccurrences));
    seedOccurrences.clear();
    sceneDelta.updated.reserve(delta.updated.points().size());
    for (size_t pointIndex = 0; pointIndex < delta.updated.points().size(); ++pointIndex)
        sceneDelta.updated.push_back(
            sceneInstanceDesc(delta.updated, pointIndex, assetAttribute, defaultAsset, seedOccurrences));

    const std::string batchId = prefix + "/L" + std::to_string(request.getLevel()) + "/" +
                                std::to_string(request.getX()) + "/" + std::to_string(request.getZ());
    return sink->applyDelta(batchId, sceneDelta);
}

eve::Result<uint64_t> Procgen::synchronizeCellInstances(const std::string& prefix, const RuntimeGeneration& runtime,
                                                        const ProcgenCellRequest& request,
                                                        const std::string&        assetAttribute,
                                                        const std::string&        defaultAsset) {
    if (prefix.empty())
        return eve::Result<uint64_t>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "synchronizeCellInstances requires a prefix",
                                                                     "prefix", {}, "procgen.squirrel"));
    const int                 level          = request.getLevel();
    const int                 x              = request.getX();
    const int                 z              = request.getZ();
    const uint64_t            targetRevision = runtime.getCellRevision(level, x, z);
    std::unique_ptr<PointSet> snapshot(runtime.getCellOutput(level, x, z));
    if (!snapshot || targetRevision == 0)
        return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "synchronizeCellInstances requires an active runtime cell", "cell", {},
            "procgen.squirrel"));

    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return eve::Result<uint64_t>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "synchronizeCellInstances scene sink is unavailable",
                                   {}, {}, "procgen.squirrel"));
    const std::string batchId =
        prefix + "/L" + std::to_string(level) + "/" + std::to_string(x) + "/" + std::to_string(z);
    const uint64_t sceneRevision = sink->batchRevision(batchId);
    if (sceneRevision == targetRevision) return eve::Result<uint64_t>::success(targetRevision);
    if (sceneRevision > targetRevision)
        return eve::Result<uint64_t>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "Scene cell revision is ahead of RuntimeGeneration",
                                   "revision", {}, "procgen.squirrel"));

    if (sceneRevision + 1 == targetRevision) {
        std::unique_ptr<PointDelta> delta(runtime.getCellDelta(level, x, z));
        if (delta)
            return publishCellInstanceDelta(prefix, request, *delta, targetRevision, assetAttribute, defaultAsset);
    }

    auto instances = sceneInstanceDescs(*snapshot, assetAttribute, defaultAsset, true);
    if (!instances.ok()) return eve::Result<uint64_t>::failure(instances.status());
    return sink->replaceBatch(batchId, targetRevision, instances.value());
}

eve::Result<uint64_t> Procgen::synchronizeCellInstancesAtomic(const std::string&                            prefix,
                                                              const std::vector<const RuntimeGeneration*>&  runtimes,
                                                              const std::vector<const ProcgenCellRequest*>& requests,
                                                              const std::string& assetAttribute,
                                                              const std::string& defaultAsset) {
    if (prefix.empty() || runtimes.empty() || runtimes.size() != requests.size())
        return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "synchronizeCellInstancesAtomic requires a prefix and equally sized non-empty runtime/request lists",
            "cells", {}, "procgen.squirrel"));
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "synchronizeCellInstancesAtomic scene sink is unavailable", {}, {},
            "procgen.squirrel"));

    std::vector<eve::ProcgenBatchSnapshot> snapshots;
    snapshots.reserve(requests.size());
    std::unordered_set<std::string> batchIds;
    for (size_t index = 0; index < requests.size(); ++index) {
        const auto* runtime = runtimes[index];
        const auto* request = requests[index];
        if (!runtime || !request)
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "synchronizeCellInstancesAtomic contains a null cell", "cells",
                {}, "procgen.squirrel"));
        const int                 level          = request->getLevel();
        const int                 x              = request->getX();
        const int                 z              = request->getZ();
        const uint64_t            targetRevision = runtime->getCellRevision(level, x, z);
        std::unique_ptr<PointSet> output(runtime->getCellOutput(level, x, z));
        if (!output || targetRevision == 0)
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "synchronizeCellInstancesAtomic contains an inactive cell", "cells", {},
                "procgen.squirrel"));
        const std::string batchId =
            prefix + "/L" + std::to_string(level) + "/" + std::to_string(x) + "/" + std::to_string(z);
        if (!batchIds.insert(batchId).second)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "synchronizeCellInstancesAtomic repeats a cell",
                                       "cells", {}, "procgen.squirrel"));
        const uint64_t sceneRevision = sink->batchRevision(batchId);
        if (sceneRevision > targetRevision)
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "Scene cell revision is ahead of RuntimeGeneration", "revision", {},
                "procgen.squirrel"));
        if (sceneRevision == targetRevision) continue;
        auto instances = sceneInstanceDescs(*output, assetAttribute, defaultAsset, true);
        if (!instances) return eve::Result<uint64_t>::failure(instances.status());
        snapshots.push_back({batchId, targetRevision, std::move(instances).takeValue()});
    }
    if (snapshots.empty()) return eve::Result<uint64_t>::success(0);
    return sink->replaceBatches(snapshots);
}

eve::Result<void> Procgen::removeCellInstances(const std::string& prefix, const ProcgenCellRequest& request) {
    if (prefix.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "removeCellInstances requires a prefix", {}, {}, "procgen.squirrel"));
    const std::string batchId = prefix + "/L" + std::to_string(request.getLevel()) + "/" +
                                std::to_string(request.getX()) + "/" + std::to_string(request.getZ());
    return removeInstances(batchId);
}

eve::Result<uint64_t> Procgen::removeCellInstancesAtomic(const std::string&                            prefix,
                                                         const std::vector<const ProcgenCellRequest*>& requests) {
    if (prefix.empty() || requests.empty())
        return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "removeCellInstancesAtomic requires a prefix and cleanup requests",
            "requests", {}, "procgen.squirrel"));
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return eve::Result<uint64_t>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "removeCellInstancesAtomic scene sink is unavailable",
                                   {}, {}, "procgen.squirrel"));
    std::vector<std::string> batchIds;
    batchIds.reserve(requests.size());
    for (const auto* request : requests) {
        if (!request)
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "removeCellInstancesAtomic contains a null request", "requests",
                {}, "procgen.squirrel"));
        batchIds.push_back(prefix + "/L" + std::to_string(request->getLevel()) + "/" + std::to_string(request->getX()) +
                           "/" + std::to_string(request->getZ()));
    }
    return sink->removeBatches(batchIds);
}

eve::Result<uint64_t> Procgen::completeCellCleanupAtomic(const std::string&                            prefix,
                                                         const std::vector<RuntimeGeneration*>&        runtimes,
                                                         const std::vector<const ProcgenCellRequest*>& requests) {
    if (prefix.empty() || requests.empty() || runtimes.size() != requests.size())
        return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "completeCellCleanupAtomic requires a prefix and equally sized runtime/request arrays", "requests", {},
            "procgen.squirrel"));
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    if (!sink)
        return eve::Result<uint64_t>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "completeCellCleanupAtomic scene sink is unavailable",
                                   {}, {}, "procgen.squirrel"));

    struct RuntimeCleanupGroup {
        RuntimeGeneration*                     runtime = nullptr;
        std::vector<const ProcgenCellRequest*> requests;
    };
    std::vector<RuntimeCleanupGroup> groups;
    std::vector<std::string>         batchIds;
    groups.reserve(runtimes.size());
    batchIds.reserve(requests.size());
    for (size_t index = 0; index < requests.size(); ++index) {
        auto* runtime = runtimes[index];
        auto* request = requests[index];
        if (!runtime || !request)
            return eve::Result<uint64_t>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "completeCellCleanupAtomic contains a null runtime or request",
                "requests", {}, "procgen.squirrel"));
        auto group = std::find_if(groups.begin(), groups.end(),
                                  [runtime](const auto& candidate) { return candidate.runtime == runtime; });
        if (group == groups.end()) {
            groups.push_back({runtime, {}});
            group = std::prev(groups.end());
        }
        group->requests.push_back(request);
        batchIds.push_back(prefix + "/L" + std::to_string(request->getLevel()) + "/" + std::to_string(request->getX()) +
                           "/" + std::to_string(request->getZ()));
    }
    for (const auto& group : groups) {
        auto validated = group.runtime->validateCleanups(group.requests);
        if (!validated.ok()) return eve::Result<uint64_t>::failure(validated.status());
    }
    return sink->removeBatchesCoordinated(batchIds, [&groups] {
        for (auto& group : groups) group.runtime->eraseValidatedCleanups(group.requests);
        return eve::Result<void>::success();
    });
}

int Procgen::getPublishedInstanceCount(const std::string& batchId) const {
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    return sink && !batchId.empty() ? sink->instanceCount(batchId) : 0;
}

int Procgen::getPublishedCreatedCount(const std::string& batchId) const {
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    return sink && !batchId.empty() ? sink->lastCreatedCount(batchId) : 0;
}

int Procgen::getPublishedReusedCount(const std::string& batchId) const {
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    return sink && !batchId.empty() ? sink->lastReusedCount(batchId) : 0;
}

int Procgen::getPublishedRemovedCount(const std::string& batchId) const {
    auto* sink = eve::cap::query<eve::IProcgenSceneSink>();
    return sink && !batchId.empty() ? sink->lastRemovedCount(batchId) : 0;
}


}  // namespace eve::procgen
