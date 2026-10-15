#include <cmath>
#include <future>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/Module.h"
#include "common/ProcgenSceneSink.h"
#include "common/ProcgenWorldQuery.h"
#include "procgen/MeshBuild.h"
#include "procgen/Procgen.h"
#include "procgen/RuntimeGeneration.h"
#include "procgen/ShapeGrammar.h"
#include "procgen/SpatialData.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// procgen_scene_sink.cpp
using namespace eve::procgen;

namespace {

class MockProcgenSceneSink final : public eve::IProcgenSceneSink {
public:
    bool applyBatch(const std::string& batchId, const std::vector<eve::ProcgenInstanceDesc>& instances) override {
        const auto                            previous = batches.find(batchId);
        std::unordered_map<std::string, bool> oldIds;
        if (previous != batches.end())
            for (const auto& instance : previous->second) oldIds[instance.id] = true;
        Stats stats;
        for (const auto& instance : instances) {
            if (oldIds.erase(instance.id) != 0)
                ++stats.reused;
            else
                ++stats.created;
        }
        stats.removed      = int(oldIds.size());
        batches[batchId]   = instances;
        revisions[batchId] = revisions[batchId] + 1;
        latest[batchId]    = stats;
        return true;
    }
    eve::Result<uint64_t> replaceBatch(const std::string& batchId, uint64_t targetRevision,
                                       const std::vector<eve::ProcgenInstanceDesc>& instances) override {
        if (targetRevision == 0 || batchRevision(batchId) >= targetRevision)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock snapshot revision is stale"));
        if (!applyBatch(batchId, instances))
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "mock snapshot commit failed"));
        ++replaceCalls;
        revisions[batchId] = targetRevision;
        return eve::Result<uint64_t>::success(targetRevision);
    }
    eve::Result<uint64_t> replaceBatches(const std::vector<eve::ProcgenBatchSnapshot>& snapshots) override {
        if (failBatchTransaction)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected batch transaction failure"));
        auto nextBatches   = batches;
        auto nextRevisions = revisions;
        for (const auto& snapshot : snapshots) {
            if (snapshot.batchId.empty() || snapshot.targetRevision == 0 ||
                nextRevisions[snapshot.batchId] >= snapshot.targetRevision)
                return eve::Result<uint64_t>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock batch transaction is stale"));
            nextBatches[snapshot.batchId]   = snapshot.instances;
            nextRevisions[snapshot.batchId] = snapshot.targetRevision;
        }
        batches.swap(nextBatches);
        revisions.swap(nextRevisions);
        return eve::Result<uint64_t>::success(static_cast<uint64_t>(snapshots.size()));
    }
    eve::Result<uint64_t> applyDelta(const std::string& batchId, const eve::ProcgenInstanceDelta& delta) override {
        const auto current = revisions.find(batchId);
        if (current == revisions.end() || current->second != delta.baseRevision)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock delta revision is stale"));
        lastDeltaBatch = batchId;
        lastDelta      = delta;
        ++deltaCalls;
        revisions[batchId] = delta.targetRevision;
        return eve::Result<uint64_t>::success(delta.targetRevision);
    }
    uint64_t batchRevision(const std::string& batchId) const override {
        const auto found = revisions.find(batchId);
        return found == revisions.end() ? 0 : found->second;
    }
    bool removeBatch(const std::string& batchId) override {
        const auto found = batches.find(batchId);
        if (found == batches.end()) return false;
        latest[batchId] = {0, 0, int(found->second.size())};
        batches.erase(found);
        revisions.erase(batchId);
        return true;
    }
    eve::Result<uint64_t> removeBatches(const std::vector<std::string>& batchIds) override {
        return removeBatchesCoordinated(batchIds, [] { return eve::Result<void>::success(); });
    }
    eve::Result<uint64_t> removeBatchesCoordinated(
        const std::vector<std::string>&           batchIds,
        const std::function<eve::Result<void>()>& commitPreparedOwner) override {
        if (failBatchTransaction)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected batch transaction failure"));
        auto nextBatches   = batches;
        auto nextRevisions = revisions;
        for (const auto& batchId : batchIds) {
            if (nextBatches.erase(batchId) != 1)
                return eve::Result<uint64_t>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "mock batch is unpublished"));
            nextRevisions.erase(batchId);
        }
        if (!commitPreparedOwner)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "mock commit is missing"));
        auto ownerCommit = commitPreparedOwner();
        if (!ownerCommit.ok()) return eve::Result<uint64_t>::failure(ownerCommit.status());
        batches.swap(nextBatches);
        revisions.swap(nextRevisions);
        return eve::Result<uint64_t>::success(static_cast<uint64_t>(batchIds.size()));
    }
    int instanceCount(const std::string& batchId) const override {
        const auto found = batches.find(batchId);
        return found == batches.end() ? 0 : int(found->second.size());
    }
    int lastCreatedCount(const std::string& batchId) const override {
        const auto found = latest.find(batchId);
        return found == latest.end() ? 0 : found->second.created;
    }
    int lastReusedCount(const std::string& batchId) const override {
        const auto found = latest.find(batchId);
        return found == latest.end() ? 0 : found->second.reused;
    }
    int lastRemovedCount(const std::string& batchId) const override {
        const auto found = latest.find(batchId);
        return found == latest.end() ? 0 : found->second.removed;
    }

    struct Stats {
        int created = 0;
        int reused  = 0;
        int removed = 0;
    };
    std::unordered_map<std::string, std::vector<eve::ProcgenInstanceDesc>> batches;
    std::unordered_map<std::string, uint64_t>                              revisions;
    std::unordered_map<std::string, Stats>                                 latest;
    std::string                                                            lastDeltaBatch;
    eve::ProcgenInstanceDelta                                              lastDelta;
    int                                                                    replaceCalls         = 0;
    int                                                                    deltaCalls           = 0;
    bool                                                                   failBatchTransaction = false;
};

}  // namespace

TEST_CASE("procgen.sceneSink.publishesStableAttributedInstances") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);

    Procgen proc;
    auto    pointsResult = proc.newPointSetHandle();
    REQUIRE(pointsResult.ok());
    const auto pointsHandle = std::move(pointsResult).takeValue();
    auto       pointsView   = proc.resolvePointSet(pointsHandle);
    REQUIRE(pointsView.isBound());
    const int tree = pointsView->add(1.f, 2.f, 3.f);
    REQUIRE(pointsView->trySetPointId(tree, 1001).ok());
    pointsView->setPointSeed(tree, 77);
    pointsView->setYaw(tree, 30.f);
    pointsView->setScale(tree, 2.f, 3.f, 4.f);
    pointsView->setStringAttribute(tree, "asset", "oak");
    const int rock = pointsView->add(8.f, 0.f, 9.f);
    REQUIRE(pointsView->trySetPointId(rock, 1002).ok());
    pointsView->setPointSeed(rock, 88);

    auto publishedResult = proc.publishInstances("forest/main", pointsHandle, "asset", "granite");
    REQUIRE(publishedResult.ok());
    CHECK_EQ(proc.getPublishedInstanceCount("forest/main"), 2);
    CHECK_EQ(proc.getPublishedCreatedCount("forest/main"), 2);
    CHECK_EQ(proc.getPublishedReusedCount("forest/main"), 0);
    CHECK_EQ(proc.getPublishedRemovedCount("forest/main"), 0);
    const auto& published = sink.batches.at("forest/main");
    CHECK_EQ(published[0].id, std::string("pcg-id-1001"));
    CHECK_EQ(published[0].sourcePointId, uint64_t(1001));
    CHECK_EQ(published[0].asset, std::string("oak"));
    CHECK_EQ(published[0].x, 1.f);
    CHECK_EQ(published[0].yaw, 30.f);
    CHECK_EQ(published[0].scaleZ, 4.f);
    CHECK_EQ(published[1].asset, std::string("granite"));
    CHECK_EQ(published[1].id, std::string("pcg-id-1002"));
    CHECK_EQ(published[1].sourcePointId, uint64_t(1002));

    auto reorderedResult = proc.newPointSetHandle();
    REQUIRE(reorderedResult.ok());
    const auto reorderedHandle = std::move(reorderedResult).takeValue();
    auto       reorderedView   = proc.resolvePointSet(reorderedHandle);
    REQUIRE(reorderedView.isBound());
    reorderedView->add(-1.f, 0.f, 0.f);
    reorderedView->setPointSeed(0, 99);
    std::move(reorderedView->appendPointFrom(*pointsView, 0)).expect("scene sink test point copy");
    std::move(reorderedView->appendPointFrom(*pointsView, 1)).expect("scene sink test point copy");
    auto reorderedPublishResult = proc.publishInstances("forest/main", reorderedHandle, "asset", "granite");
    REQUIRE(reorderedPublishResult.ok());
    CHECK_EQ(proc.getPublishedCreatedCount("forest/main"), 1);
    CHECK_EQ(proc.getPublishedReusedCount("forest/main"), 2);
    CHECK_EQ(proc.getPublishedRemovedCount("forest/main"), 0);
    const auto& reconciled = sink.batches.at("forest/main");
    CHECK_EQ(reconciled[1].id, std::string("pcg-id-1001"));
    CHECK_EQ(reconciled[2].id, std::string("pcg-id-1002"));

    reorderedView->setStringAttribute(0, "instanceId", "hero-tree");
    reorderedView->setStringAttribute(1, "instanceId", "hero-tree");
    auto duplicateResult = proc.publishInstances("forest/main", reorderedHandle, "asset", "granite");
    REQUIRE(!duplicateResult.ok());
    CHECK(duplicateResult.status().describe().find("duplicate instance id") != std::string::npos);
    CHECK_EQ(proc.getPublishedInstanceCount("forest/main"), 3);

    auto removeResult = proc.removeInstances("forest/main");
    REQUIRE(removeResult.ok());
    CHECK_EQ(proc.getPublishedInstanceCount("forest/main"), 0);
    CHECK_EQ(proc.getPublishedRemovedCount("forest/main"), 3);
    REQUIRE(proc.releasePointSet(reorderedHandle).ok());
    REQUIRE(proc.releasePointSet(pointsHandle).ok());
    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}

TEST_CASE("procgen.sceneSink.projectsRuntimePointDeltaByStableIdentity") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);

    Procgen proc;
    auto    pointsResult = proc.newPointSetHandle();
    REQUIRE(pointsResult.ok());
    const auto pointsHandle = std::move(pointsResult).takeValue();
    auto       pointsView   = proc.resolvePointSet(pointsHandle);
    REQUIRE(pointsView.isBound());
    const int tree = pointsView->add(1.f, 0.f, 2.f);
    REQUIRE(pointsView->trySetPointId(tree, 2001).ok());
    pointsView->setStringAttribute(tree, "instanceId", "tree-custom");
    pointsView->setStringAttribute(tree, "asset", "oak");
    const int rock = pointsView->add(3.f, 0.f, 4.f);
    REQUIRE(pointsView->trySetPointId(rock, 2002).ok());

    RuntimeGeneration runtime(77);
    REQUIRE(runtime.addLevel(10.f, 6.f, 2.f) >= 0);
    runtime.updateSource(1.f, 1.f, 1.f, 0.f);
    std::unique_ptr<ProcgenCellRequest> request(runtime.nextGenerate());
    REQUIRE(bool(request));
    auto initialPublish = proc.publishCellInstances("world", *request, pointsHandle, "asset", "stone");
    REQUIRE(initialPublish.ok());

    PointSet  target;
    const int targetTree = target.appendPointFrom(*pointsView, std::size_t(tree)).expect("scene delta target tree");
    target.setPosition(targetTree, 9.f, 1.f, 2.f);
    target.setStringAttribute(targetTree, "instanceId", "tree-renamed");
    ProcgenPoint flower;
    flower.id             = 2003;
    flower.seed           = 33;
    const int flowerIndex = target.appendPoint(flower);
    target.setStringAttribute(flowerIndex, "asset", "lily");
    auto delta = diffPointSets(*pointsView, target);
    REQUIRE(delta.ok());

    auto applied = proc.publishCellInstanceDelta("world", *request, delta.value(), 2, "asset", "stone");
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value(), uint64_t(2));
    CHECK_EQ(sink.lastDeltaBatch, std::string("world/L0/0/0"));
    CHECK_EQ(sink.lastDelta.baseRevision, uint64_t(1));
    CHECK_EQ(sink.lastDelta.targetRevision, uint64_t(2));
    REQUIRE_EQ(sink.lastDelta.updated.size(), std::size_t(1));
    CHECK_EQ(sink.lastDelta.updated[0].sourcePointId, uint64_t(2001));
    CHECK_EQ(sink.lastDelta.updated[0].id, std::string("tree-renamed"));
    CHECK_EQ(sink.lastDelta.updated[0].x, 9.f);
    REQUIRE_EQ(sink.lastDelta.added.size(), std::size_t(1));
    CHECK_EQ(sink.lastDelta.added[0].sourcePointId, uint64_t(2003));
    CHECK_EQ(sink.lastDelta.added[0].asset, std::string("lily"));
    REQUIRE_EQ(sink.lastDelta.removedPointIds.size(), std::size_t(1));
    CHECK_EQ(sink.lastDelta.removedPointIds[0], uint64_t(2002));
    CHECK_EQ(sink.lastDelta.targetPointOrder, delta.value().targetOrder);

    auto recoveryResult = proc.newPointSetHandle();
    REQUIRE(recoveryResult.ok());
    const auto recoveryHandle = std::move(recoveryResult).takeValue();
    auto       recoveryView   = proc.resolvePointSet(recoveryHandle);
    REQUIRE(recoveryView.isBound());
    for (std::size_t pointIndex = 0; pointIndex < target.points().size(); ++pointIndex)
        recoveryView->appendPointFrom(target, pointIndex).expect("scene recovery snapshot");
    auto recovered = proc.publishCellSnapshot("world", *request, recoveryHandle, 5, "asset", "stone");
    REQUIRE(recovered.ok());
    CHECK_EQ(recovered.value(), uint64_t(5));
    CHECK_EQ(sink.batchRevision("world/L0/0/0"), uint64_t(5));
    REQUIRE_EQ(sink.batches.at("world/L0/0/0").size(), std::size_t(2));
    CHECK_EQ(sink.batches.at("world/L0/0/0")[0].id, std::string("tree-renamed"));
    CHECK_EQ(sink.batches.at("world/L0/0/0")[1].sourcePointId, uint64_t(2003));
    auto staleRecovery = proc.publishCellSnapshot("world", *request, recoveryHandle, 4, "asset", "stone");
    CHECK(!staleRecovery.ok());
    CHECK_EQ(sink.batchRevision("world/L0/0/0"), uint64_t(5));

    auto invalidResult = proc.newPointSetHandle();
    REQUIRE(invalidResult.ok());
    const auto invalidHandle = std::move(invalidResult).takeValue();
    auto       invalidView   = proc.resolvePointSet(invalidHandle);
    REQUIRE(invalidView.isBound());
    invalidView->add(0.f, 0.f, 0.f);
    auto invalidSnapshot = proc.publishCellSnapshot("world", *request, invalidHandle, 6, "asset", "stone");
    CHECK(!invalidSnapshot.ok());
    CHECK_EQ(sink.batchRevision("world/L0/0/0"), uint64_t(5));

    REQUIRE(proc.releasePointSet(invalidHandle).ok());
    REQUIRE(proc.releasePointSet(recoveryHandle).ok());
    REQUIRE(proc.releasePointSet(pointsHandle).ok());
    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}

TEST_CASE("procgen.sceneSink.synchronizesRuntimeRevisionWithDeltaAndSnapshotRecovery") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);

    Procgen           proc;
    RuntimeGeneration runtime(91);
    REQUIRE(runtime.addLevel(10.f, 6.f, 2.f) >= 0);
    runtime.updateSource(1.f, 1.f, 1.f, 0.f);
    std::unique_ptr<ProcgenCellRequest> request(runtime.nextGenerate());
    REQUIRE(bool(request));

    PointSet  initial;
    const int tree = initial.add(1.f, 0.f, 2.f);
    REQUIRE(initial.trySetPointId(tree, 3001).ok());
    initial.setStringAttribute(tree, "asset", "oak");
    REQUIRE(runtime.completeGeneration(request.get(), &initial));

    auto first = proc.synchronizeCellInstances("stream", runtime, *request, "asset", "stone");
    REQUIRE(first.ok());
    CHECK_EQ(first.value(), uint64_t(1));
    CHECK_EQ(sink.replaceCalls, 1);
    CHECK_EQ(sink.deltaCalls, 0);
    auto unchanged = proc.synchronizeCellInstances("stream", runtime, *request, "asset", "stone");
    REQUIRE(unchanged.ok());
    CHECK_EQ(sink.replaceCalls, 1);

    PointSet revisionTwo = initial;
    revisionTwo.setPosition(0, 4.f, 0.f, 2.f);
    auto updated = runtime.applyCellUpdate(0, 0, 0, 1, revisionTwo);
    REQUIRE(updated.ok());
    auto incremental = proc.synchronizeCellInstances("stream", runtime, *request, "asset", "stone");
    REQUIRE(incremental.ok());
    CHECK_EQ(incremental.value(), uint64_t(2));
    CHECK_EQ(sink.deltaCalls, 1);
    CHECK_EQ(sink.replaceCalls, 1);

    PointSet revisionThree = revisionTwo;
    revisionThree.setPosition(0, 6.f, 0.f, 2.f);
    REQUIRE(runtime.applyCellUpdate(0, 0, 0, 2, revisionThree).ok());
    PointSet revisionFour = revisionThree;
    revisionFour.setPosition(0, 8.f, 0.f, 2.f);
    REQUIRE(runtime.applyCellUpdate(0, 0, 0, 3, revisionFour).ok());
    auto recovered = proc.synchronizeCellInstances("stream", runtime, *request, "asset", "stone");
    REQUIRE(recovered.ok());
    CHECK_EQ(recovered.value(), uint64_t(4));
    CHECK_EQ(sink.replaceCalls, 2);
    CHECK_EQ(sink.deltaCalls, 1);
    CHECK_EQ(sink.batches.at("stream/L0/0/0")[0].x, 8.f);

    sink.revisions["stream/L0/0/0"] = 5;
    auto sceneAhead                 = proc.synchronizeCellInstances("stream", runtime, *request, "asset", "stone");
    CHECK(!sceneAhead.ok());
    CHECK_EQ(sink.replaceCalls, 2);
    CHECK_EQ(sink.deltaCalls, 1);

    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}

TEST_CASE("procgen.sceneSink.synchronizesSeveralRuntimeCellsAtomically") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);

    Procgen           proc;
    RuntimeGeneration firstRuntime(301);
    RuntimeGeneration secondRuntime(302);
    REQUIRE(firstRuntime.addLevel(10.f, 6.f, 2.f) >= 0);
    REQUIRE(secondRuntime.addLevel(10.f, 6.f, 2.f) >= 0);
    firstRuntime.updateSource(1.f, 1.f, 1.f, 0.f);
    secondRuntime.updateSource(21.f, 1.f, 1.f, 0.f);
    std::unique_ptr<ProcgenCellRequest> firstRequest(firstRuntime.nextGenerate());
    std::unique_ptr<ProcgenCellRequest> secondRequest(secondRuntime.nextGenerate());
    REQUIRE(bool(firstRequest));
    REQUIRE(bool(secondRequest));

    PointSet  firstPoints;
    const int firstPoint = firstPoints.add(1.f, 0.f, 1.f);
    REQUIRE(firstPoints.trySetPointId(firstPoint, 5001).ok());
    PointSet  secondPoints;
    const int secondPoint = secondPoints.add(21.f, 0.f, 1.f);
    REQUIRE(secondPoints.trySetPointId(secondPoint, 5002).ok());
    REQUIRE(firstRuntime.completeGeneration(firstRequest.get(), &firstPoints));
    REQUIRE(secondRuntime.completeGeneration(secondRequest.get(), &secondPoints));

    std::vector<const RuntimeGeneration*>  runtimes{&firstRuntime, &secondRuntime};
    std::vector<const ProcgenCellRequest*> requests{firstRequest.get(), secondRequest.get()};
    auto committed = proc.synchronizeCellInstancesAtomic("world", runtimes, requests, "asset", "stone");
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value(), uint64_t(2));
    CHECK_EQ(sink.revisions.at("world/L0/0/0"), uint64_t(1));
    CHECK_EQ(sink.revisions.at("world/L0/2/0"), uint64_t(1));

    PointSet firstRevisionTwo = firstPoints;
    firstRevisionTwo.setPosition(0, 5.f, 0.f, 1.f);
    PointSet secondRevisionTwo = secondPoints;
    secondRevisionTwo.setPosition(0, 25.f, 0.f, 1.f);
    REQUIRE(firstRuntime.applyCellUpdate(0, 0, 0, 1, firstRevisionTwo).ok());
    REQUIRE(secondRuntime.applyCellUpdate(0, 2, 0, 1, secondRevisionTwo).ok());
    sink.failBatchTransaction = true;
    auto rejected             = proc.synchronizeCellInstancesAtomic("world", runtimes, requests, "asset", "stone");
    REQUIRE(!rejected.ok());
    CHECK_EQ(sink.revisions.at("world/L0/0/0"), uint64_t(1));
    CHECK_EQ(sink.revisions.at("world/L0/2/0"), uint64_t(1));
    CHECK_EQ(sink.batches.at("world/L0/0/0")[0].x, 1.f);
    CHECK_EQ(sink.batches.at("world/L0/2/0")[0].x, 21.f);

    sink.failBatchTransaction = false;
    auto recovered            = proc.synchronizeCellInstancesAtomic("world", runtimes, requests, "asset", "stone");
    REQUIRE(recovered.ok());
    CHECK_EQ(sink.batches.at("world/L0/0/0")[0].x, 5.f);
    CHECK_EQ(sink.batches.at("world/L0/2/0")[0].x, 25.f);

    std::vector<const ProcgenCellRequest*> duplicateRequests{firstRequest.get(), firstRequest.get()};
    auto duplicateRemoval = proc.removeCellInstancesAtomic("world", duplicateRequests);
    REQUIRE(!duplicateRemoval.ok());
    CHECK_EQ(sink.batches.size(), std::size_t(2));

    sink.failBatchTransaction = true;
    auto removalRejected      = proc.removeCellInstancesAtomic("world", requests);
    REQUIRE(!removalRejected.ok());
    CHECK_EQ(sink.batches.size(), std::size_t(2));
    sink.failBatchTransaction = false;
    auto removed              = proc.removeCellInstancesAtomic("world", requests);
    REQUIRE(removed.ok());
    CHECK_EQ(removed.value(), uint64_t(2));
    CHECK(sink.batches.empty());
    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}

TEST_CASE("procgen.sceneSink.completesSceneAndRuntimeCleanupAtomically") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);
    Procgen proc;

    RuntimeGeneration firstRuntime(31);
    RuntimeGeneration secondRuntime(32);
    REQUIRE(firstRuntime.addLevel(10.f, 6.f, 1.5f) >= 0);
    REQUIRE(secondRuntime.addLevel(10.f, 6.f, 1.5f) >= 0);
    firstRuntime.updateSource(1.f, 1.f, 1.f, 0.f);
    secondRuntime.updateSource(21.f, 1.f, 1.f, 0.f);
    auto firstGenerated  = std::unique_ptr<ProcgenCellRequest>(firstRuntime.nextGenerate());
    auto secondGenerated = std::unique_ptr<ProcgenCellRequest>(secondRuntime.nextGenerate());
    REQUIRE(bool(firstGenerated));
    REQUIRE(bool(secondGenerated));
    PointSet  firstPoints;
    PointSet  secondPoints;
    const int firstPoint  = firstPoints.add(1.f, 0.f, 1.f);
    const int secondPoint = secondPoints.add(21.f, 0.f, 1.f);
    REQUIRE(firstPoints.trySetPointId(firstPoint, 6001).ok());
    REQUIRE(secondPoints.trySetPointId(secondPoint, 6002).ok());
    REQUIRE(firstRuntime.completeGeneration(firstGenerated.get(), &firstPoints));
    REQUIRE(secondRuntime.completeGeneration(secondGenerated.get(), &secondPoints));
    std::vector<const RuntimeGeneration*>  publishRuntimes{&firstRuntime, &secondRuntime};
    std::vector<const ProcgenCellRequest*> publishRequests{firstGenerated.get(), secondGenerated.get()};
    REQUIRE(proc.synchronizeCellInstancesAtomic("cleanup", publishRuntimes, publishRequests, "asset", "stone").ok());

    firstRuntime.updateSource(1000.f, 1000.f, 1.f, 0.f);
    secondRuntime.updateSource(1000.f, 1000.f, 1.f, 0.f);
    auto firstCleanup  = std::unique_ptr<ProcgenCellRequest>(firstRuntime.nextCleanup());
    auto secondCleanup = std::unique_ptr<ProcgenCellRequest>(secondRuntime.nextCleanup());
    REQUIRE(bool(firstCleanup));
    REQUIRE(bool(secondCleanup));
    std::vector<RuntimeGeneration*>        cleanupRuntimes{&firstRuntime, &secondRuntime};
    std::vector<const ProcgenCellRequest*> cleanupRequests{firstCleanup.get(), secondCleanup.get()};

    sink.failBatchTransaction = true;
    auto sceneRejected        = proc.completeCellCleanupAtomic("cleanup", cleanupRuntimes, cleanupRequests);
    REQUIRE(!sceneRejected.ok());
    CHECK(firstRuntime.isRequestCurrent(firstCleanup.get()));
    CHECK(secondRuntime.isRequestCurrent(secondCleanup.get()));
    CHECK_EQ(sink.batches.size(), std::size_t(2));
    sink.failBatchTransaction = false;

    firstRuntime.updateSource(1.f, 1.f, 1.f, 0.f);
    firstRuntime.updateSource(1000.f, 1000.f, 1.f, 0.f);
    auto staleRejected = proc.completeCellCleanupAtomic("cleanup", cleanupRuntimes, cleanupRequests);
    REQUIRE(!staleRejected.ok());
    CHECK(secondRuntime.isRequestCurrent(secondCleanup.get()));
    CHECK_EQ(sink.batches.size(), std::size_t(2));
    firstCleanup = std::unique_ptr<ProcgenCellRequest>(firstRuntime.nextCleanup());
    REQUIRE(bool(firstCleanup));
    cleanupRequests[0] = firstCleanup.get();

    auto completed = proc.completeCellCleanupAtomic("cleanup", cleanupRuntimes, cleanupRequests);
    REQUIRE(completed.ok());
    CHECK_EQ(completed.value(), uint64_t(2));
    CHECK(!firstRuntime.isRequestCurrent(firstCleanup.get()));
    CHECK(!secondRuntime.isRequestCurrent(secondCleanup.get()));
    CHECK(sink.batches.empty());
    CHECK_EQ(firstRuntime.getResidentPointCount(), 0);
    CHECK_EQ(secondRuntime.getResidentPointCount(), 0);
    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}


TEST_CASE("procgen.sceneSink.reportsMissingProvider") {
    eve::cap::detail::clearAllRaw();
    Procgen proc;
    auto    pointsResult = proc.newPointSetHandle();
    REQUIRE(pointsResult.ok());
    const auto pointsHandle = std::move(pointsResult).takeValue();
    auto       pointsView   = proc.resolvePointSet(pointsHandle);
    REQUIRE(pointsView.isBound());
    pointsView->add(0.f, 0.f, 0.f);

    auto publishResult = proc.publishInstances("missing", pointsHandle, "asset", "tree");
    REQUIRE(!publishResult.ok());
    CHECK(publishResult.status().describe().find("unavailable") != std::string::npos);

    RuntimeGeneration runtime(12);
    REQUIRE(runtime.addLevel(10.f, 6.f, 2.f) >= 0);
    runtime.updateSource(1.f, 1.f, 1.f, 0.f);
    std::unique_ptr<ProcgenCellRequest> request(runtime.nextGenerate());
    REQUIRE(bool(request));
    PointSet  generated;
    const int point = generated.add(0.f, 0.f, 0.f);
    REQUIRE(generated.trySetPointId(point, 4001).ok());
    REQUIRE(runtime.completeGeneration(request.get(), &generated));
    auto synchronizeResult = proc.synchronizeCellInstances("missing", runtime, *request, "asset", "tree");
    REQUIRE(!synchronizeResult.ok());
    CHECK(synchronizeResult.status().describe().find("unavailable") != std::string::npos);
    std::vector<const ProcgenCellRequest*> removalRequests{request.get()};
    auto                                   removeResult = proc.removeCellInstancesAtomic("missing", removalRequests);
    REQUIRE(!removeResult.ok());
    CHECK(removeResult.status().describe().find("unavailable") != std::string::npos);
    REQUIRE(proc.releasePointSet(pointsHandle).ok());
}

// procgen_schema_script.cpp


// procgen_shape_grammar.cpp
using namespace eve::procgen;

TEST_CASE("procgen.shapeGrammar.expandsGroupsAndFillRepetition") {
    ShapeGrammar grammar;
    CHECK(grammar.addModule("A", "wall-a", 2.f, 1.f));
    CHECK(grammar.addModule("A", "wall-b", 2.f, 1.f));
    CHECK(!grammar.addModule("A", "wrong-length", 3.f, 1.f));
    CHECK(grammar.addModule("B", "post", 1.f, 1.f));
    CHECK(grammar.validate("A,[B,A]*"));

    PointSet spline;
    spline.add(0.f, 0.f, 0.f);
    spline.add(10.f, 0.f, 0.f);
    PointSet* first  = grammar.generate("A,[B,A]*", &spline, 42, false);
    PointSet* second = grammar.generate("A,[B,A]*", &spline, 42, false);
    REQUIRE(bool(first));
    REQUIRE(bool(second));
    CHECK_EQ(first->getCount(), 5);
    CHECK_EQ(first->getStringAttribute(0, "module", ""), std::string("A"));
    CHECK_EQ(first->getStringAttribute(1, "module", ""), std::string("B"));
    CHECK_EQ(first->getStringAttribute(2, "module", ""), std::string("A"));
    CHECK_EQ(first->getX(0), 1.f);
    CHECK_EQ(first->getX(1), 2.5f);
    CHECK_EQ(first->getX(2), 4.f);
    CHECK_EQ(first->getYaw(0), 0.f);
    CHECK_EQ(first->getFloatAttribute(1, "length", 0.f), 1.f);
    for (int i = 0; i < first->getCount(); ++i)
        CHECK_EQ(first->getStringAttribute(i, "asset", ""), second->getStringAttribute(i, "asset", ""));
    CHECK(grammar.debugReport().find("symbols=5") != std::string::npos);

    delete second;
    delete first;
}

TEST_CASE("procgen.shapeGrammar.supportsPlusExactAndIncompleteModes") {
    ShapeGrammar grammar;
    grammar.addModule("A", "wall", 2.f, 1.f);
    grammar.addModule("B", "post", 1.f, 1.f);
    PointSet spline;
    spline.add(0.f, 0.f, 0.f);
    spline.add(10.f, 0.f, 0.f);

    PointSet* plus = grammar.generate("[A,B]+", &spline, 1, false);
    REQUIRE(bool(plus));
    CHECK_EQ(plus->getCount(), 6);
    PointSet* exact = grammar.generate("A3,B2", &spline, 1, false);
    REQUIRE(bool(exact));
    CHECK_EQ(exact->getCount(), 5);

    PointSet shortSpline;
    shortSpline.add(0.f, 0.f, 0.f);
    shortSpline.add(5.f, 0.f, 0.f);
    CHECK(!grammar.generate("A3", &shortSpline, 1, false));
    CHECK(grammar.getError().find("does not fit") != std::string::npos);
    PointSet* partial = grammar.generate("A3", &shortSpline, 1, true);
    REQUIRE(bool(partial));
    CHECK_EQ(partial->getCount(), 2);

    delete partial;
    delete exact;
    delete plus;
}

TEST_CASE("procgen.shapeGrammar.reportsSyntaxAndUnknownModules") {
    ShapeGrammar grammar;
    grammar.addModule("A", "wall", 1.f, 1.f);
    CHECK(!grammar.validate("[A,A"));
    CHECK(grammar.getError().find("unterminated") != std::string::npos);
    CHECK(!grammar.validate("A,Missing"));
    CHECK(grammar.getError().find("unknown module") != std::string::npos);
    CHECK(!grammar.validate(""));
}

// procgen_spatial_data.cpp
using namespace eve::procgen;

namespace {

class FlatProcgenWorldQuery final : public eve::IProcgenWorldQuery {
public:
    eve::Result<eve::ProcgenSurfaceHit> projectDown(float x, float z, float maxY, float minY, std::uint64_t) override {
        if (maxY < 4.f || minY > 4.f) return eve::Result<eve::ProcgenSurfaceHit>::success({});
        return eve::Result<eve::ProcgenSurfaceHit>::success({true, 91, x, 4.f, z, 0.f, 1.f, 0.f});
    }
};

struct CapabilityReset {
    CapabilityReset() { eve::cap::detail::clearAllRaw(); }
    ~CapabilityReset() { eve::cap::detail::clearAllRaw(); }
};

}  // namespace

TEST_CASE("procgen.pointSet.exposesProductionPointSemantics") {
    PointSet  points;
    const int index = points.add(1.f, 2.f, 3.f);
    points.setRotation(index, 10.f, 20.f, 30.f);
    points.setBounds(index, 2.f, 3.f, 4.f, -2.f, -3.f, -4.f);
    points.setColor(index, 1.2f, 0.5f, -0.5f, 0.75f);
    points.setSteepness(index, 2.f);

    CHECK_EQ(points.getPitch(index), 10.f);
    CHECK_EQ(points.getYaw(index), 20.f);
    CHECK_EQ(points.getRoll(index), 30.f);
    CHECK_EQ(points.getBoundsMinX(index), -2.f);
    CHECK_EQ(points.getBoundsMinY(index), -3.f);
    CHECK_EQ(points.getBoundsMinZ(index), -4.f);
    CHECK_EQ(points.getBoundsMaxX(index), 2.f);
    CHECK_EQ(points.getBoundsMaxY(index), 3.f);
    CHECK_EQ(points.getBoundsMaxZ(index), 4.f);
    CHECK_EQ(points.getColorR(index), 1.f);
    CHECK_EQ(points.getColorG(index), 0.5f);
    CHECK_EQ(points.getColorB(index), 0.f);
    CHECK_EQ(points.getColorA(index), 0.75f);
    CHECK_EQ(points.getSteepness(index), 1.f);
}

TEST_CASE("procgen.worldQuery.coversProviderPresenceAndAbsence") {
    CapabilityReset reset;
    Procgen         proc;
    auto            inputResult = proc.newPointSetHandle();
    REQUIRE(inputResult.ok());
    const auto input = std::move(inputResult).takeValue();
    auto       view  = proc.resolvePointSet(input);
    REQUIRE(view.isBound());
    view->add(2.f, 100.f, 3.f);

    auto absent = proc.projectToWorldHandle(input, 20.f, -20.f, ~std::uint64_t{0}, false);
    CHECK(!absent.ok());
    REQUIRE(absent.error() != nullptr);
    CHECK_EQ(absent.error()->code(), eve::DiagnosticCode::Unsupported);

    FlatProcgenWorldQuery provider;
    eve::cap::provide<eve::IProcgenWorldQuery>(&provider);
    auto projectedResult = proc.projectToWorldHandle(input, 20.f, -20.f, ~std::uint64_t{0}, false);
    REQUIRE(projectedResult.ok());
    const auto projected     = std::move(projectedResult).takeValue();
    auto       projectedView = proc.resolvePointSet(projected);
    REQUIRE(projectedView.isBound());
    CHECK_EQ(projectedView->getCount(), 1);
    CHECK_EQ(projectedView->getY(0), 4.f);
    CHECK_EQ(projectedView->getNormalY(0), 1.f);
    CHECK_EQ(projectedView->getIntAttribute(0, "worldObjectId", 0), 91);
    REQUIRE(proc.releasePointSet(projected).ok());
    REQUIRE(proc.releasePointSet(input).ok());
}


TEST_CASE("procgen.spatialData.volumeComposition") {
    const SpatialData world     = SpatialData::box(0.f, 0.f, 0.f, 10.f, 4.f, 10.f);
    const SpatialData hole      = SpatialData::sphere(5.f, 2.f, 5.f, 2.f);
    const SpatialData buildable = SpatialData::subtract(world, hole);

    CHECK_EQ(world.getKind(), std::string("volume.box"));
    CHECK(world.contains(1.f, 1.f, 1.f));
    CHECK(!world.contains(11.f, 1.f, 1.f));
    CHECK(!buildable.contains(5.f, 2.f, 5.f));
    CHECK(buildable.contains(1.f, 1.f, 1.f));
    CHECK(buildable.hasBounds());
    CHECK_EQ(buildable.getMaxX(), 10.f);

    const SpatialData overlap = SpatialData::intersect(world, SpatialData::box(8.f, -2.f, 8.f, 12.f, 8.f, 12.f));
    CHECK(overlap.contains(9.f, 2.f, 9.f));
    CHECK(!overlap.contains(7.f, 2.f, 9.f));
    CHECK_EQ(overlap.getMinX(), 8.f);
    CHECK_EQ(overlap.getMaxZ(), 10.f);
}

TEST_CASE("procgen.spatialData.polygonTextureMaskAndMeshSurface") {
    PointSet polygon;
    polygon.add(0.f, 0.f, 0.f);
    polygon.add(4.f, 0.f, 0.f);
    polygon.add(4.f, 0.f, 4.f);
    polygon.add(0.f, 0.f, 4.f);
    const SpatialData polygonVolume = SpatialData::polygon(polygon, -1.f, 2.f);
    CHECK_EQ(polygonVolume.getKind(), std::string("volume.polygon"));
    CHECK(polygonVolume.contains(2.f, 0.f, 2.f));
    CHECK(!polygonVolume.contains(5.f, 0.f, 2.f));

    Heightmap mask(2, 2);
    mask.setHeight(0, 0, 0.f);
    mask.setHeight(1, 0, 1.f);
    mask.setHeight(0, 1, 1.f);
    mask.setHeight(1, 1, 1.f);
    const SpatialData textureMask = SpatialData::textureMask(mask, 10.f, 20.f, 2.f, 0.75f, 1.f, -2.f, 3.f);
    CHECK_EQ(textureMask.getKind(), std::string("volume.texture_mask"));
    CHECK(textureMask.contains(12.f, 0.f, 22.f));
    CHECK(!textureMask.contains(10.f, 0.f, 20.f));

    MeshBuild mesh;
    mesh.addVertex(0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(4.f, 1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 3.f, 4.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    const SpatialData surface = SpatialData::meshSurface(mesh, 0.1f);
    CHECK_EQ(surface.getKind(), std::string("surface.mesh"));
    CHECK(surface.contains(1.f, 1.5f, 1.f));
    PointSet source;
    source.add(1.f, 100.f, 1.f);
    const PointSet projected = surface.project(source);
    CHECK_EQ(projected.getY(0), 1.5f);
    CHECK(projected.getNormalY(0) > 0.f);
}

TEST_CASE("procgen.spatialData.splineAndDeterministicSampling") {
    PointSet control;
    control.add(0.f, 0.f, 0.f);
    control.add(10.f, 0.f, 0.f);
    const SpatialData road = SpatialData::spline(control, 1.f);
    CHECK(road.contains(5.f, 0.5f, 0.f));
    CHECK(!road.contains(5.f, 2.f, 0.f));

    const SpatialData volume = SpatialData::box(0.f, 0.f, 0.f, 4.f, 2.f, 4.f);
    const PointSet    first  = volume.sample(2.f, 42, 0.f);
    const PointSet    second = volume.sample(2.f, 42, 0.f);
    CHECK_EQ(first.getCount(), 18);
    CHECK_EQ(first.getCount(), second.getCount());
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK_EQ(first.getX(i), second.getX(i));
        CHECK_EQ(first.getY(i), second.getY(i));
        CHECK_EQ(first.getZ(i), second.getZ(i));
        CHECK_EQ(first.getPointSeed(i), second.getPointSeed(i));
    }
}

TEST_CASE("procgen.spatialData.scriptFacingApiFiltersPoints") {
    Procgen proc;
    auto    includeResult = proc.boxVolumeHandle(0.f, -1.f, 0.f, 5.f, 1.f, 5.f);
    auto    excludeResult = proc.sphereVolumeHandle(2.f, 0.f, 2.f, 1.f);
    REQUIRE(includeResult.ok());
    REQUIRE(excludeResult.ok());
    auto include      = std::move(includeResult).takeValue();
    auto exclude      = std::move(excludeResult).takeValue();
    auto domainResult = proc.differenceSpatialHandle(include, exclude);
    REQUIRE(domainResult.ok());
    auto domain = std::move(domainResult).takeValue();

    auto inputResult = proc.newPointSetHandle();
    REQUIRE(inputResult.ok());
    auto input     = std::move(inputResult).takeValue();
    auto inputView = proc.resolvePointSet(input);
    REQUIRE(inputView.isBound());
    inputView->add(1.f, 0.f, 1.f);
    inputView->add(2.f, 0.f, 2.f);
    inputView->add(9.f, 0.f, 9.f);
    auto acceptedResult = proc.filterSpatialHandle(input, domain, false);
    auto rejectedResult = proc.filterSpatialHandle(input, domain, true);
    REQUIRE(acceptedResult.ok());
    REQUIRE(rejectedResult.ok());
    auto accepted     = std::move(acceptedResult).takeValue();
    auto rejected     = std::move(rejectedResult).takeValue();
    auto acceptedView = proc.resolvePointSet(accepted);
    auto rejectedView = proc.resolvePointSet(rejected);
    REQUIRE(acceptedView.isBound());
    REQUIRE(rejectedView.isBound());
    CHECK_EQ(acceptedView->getCount(), 1);
    CHECK_EQ(rejectedView->getCount(), 2);

    REQUIRE(proc.releasePointSet(rejected).ok());
    REQUIRE(proc.releasePointSet(accepted).ok());
    REQUIRE(proc.releasePointSet(input).ok());
    REQUIRE(proc.release(domain).ok());
    REQUIRE(proc.release(exclude).ok());
    REQUIRE(proc.release(include).ok());
}

TEST_CASE("procgen.spatialData.heightfieldSamplesAndProjectsSurface") {
    Heightmap map(3, 3);
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x) map.setHeight(x, z, float(x + z));

    const SpatialData surface = SpatialData::heightfield(map, 10.f, 20.f, 2.f, 3.f);
    CHECK_EQ(surface.getKind(), std::string("surface.heightfield"));
    CHECK_EQ(surface.getMinX(), 10.f);
    CHECK_EQ(surface.getMaxZ(), 24.f);
    CHECK(surface.contains(12.f, 6.f, 22.f));

    const PointSet samples = surface.sample(2.f, 91, 0.f);
    CHECK_EQ(samples.getCount(), 9);
    CHECK_EQ(samples.getX(0), 10.f);
    CHECK_EQ(samples.getZ(0), 20.f);
    CHECK_EQ(samples.getY(4), 6.f);
    CHECK(samples.getNormalY(4) > 0.f);
    CHECK(samples.getNormalX(4) < 0.f);

    PointSet source;
    source.add(14.f, -100.f, 24.f);
    const PointSet projected = surface.project(source);
    CHECK_EQ(projected.getY(0), 12.f);
}

TEST_CASE("procgen.pointOperations.matchCorePcgNodes") {
    PointSet  first;
    const int a = first.add(1.f, 0.f, 0.f);
    first.setDensity(a, 1.f);
    first.setFloatAttribute(a, "age", 12.f);
    first.setStringAttribute(a, "biome", "forest");

    PointSet  second;
    const int b = second.add(0.f, 0.f, 2.f);
    second.setDensity(b, 0.f);
    second.setFloatAttribute(b, "age", 2.f);
    second.setStringAttribute(b, "biome", "desert");

    const PointSet merged = mergePointSets(first, second);
    CHECK_EQ(merged.getCount(), 2);
    const PointSet transformed = transformPointSet(merged, 10.f, 3.f, 20.f, 90.f, 2.f, 1.f, 2.f);
    CHECK(std::abs(transformed.getX(0) - 10.f) < 0.0001f);
    CHECK(std::abs(transformed.getZ(0) - 22.f) < 0.0001f);
    CHECK_EQ(transformed.getY(0), 3.f);
    CHECK_EQ(transformed.getYaw(0), 90.f);
    CHECK_EQ(transformed.getScaleX(0), 2.f);

    const PointSet mature = filterPointFloatAttribute(merged, "age", 10.f, 20.f, false);
    const PointSet forest = filterPointStringAttribute(merged, "biome", "forest", false);
    CHECK_EQ(mature.getCount(), 1);
    CHECK_EQ(forest.getCount(), 1);
    CHECK_EQ(forest.getStringAttribute(0, "biome", ""), std::string("forest"));

    const PointSet culledA = densityCullPoints(merged, 123, 1.f);
    const PointSet culledB = densityCullPoints(merged, 123, 1.f);
    CHECK_EQ(culledA.getCount(), 1);
    CHECK_EQ(culledA.getCount(), culledB.getCount());
    CHECK_EQ(culledA.getPointSeed(0), culledB.getPointSeed(0));
}

// procgen_spline_path.cpp
using namespace eve::procgen;

namespace {

[[maybe_unused]] SplinePoint point(float x, float y, float z) {
    SplinePoint value;
    value.x = x;
    value.y = y;
    value.z = z;
    return value;
}

}  // namespace

TEST_CASE("procgen.splinePath.samplesLinearPathByArcLength") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(3.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(3.f, 4.f, 0.f)).ok());
    auto length = path.lengthResult();
    REQUIRE(length.ok());
    CHECK(std::abs(length.value() - 7.f) < 1e-5f);
    auto sample = path.evaluateDistanceResult(3.5f);
    REQUIRE(sample.ok());
    CHECK(std::abs(sample.value().x - 3.f) < 1e-4f);
    CHECK(std::abs(sample.value().y - 0.5f) < 1e-4f);
    CHECK(std::abs(sample.value().normalizedDistance - 0.5f) < 1e-5f);
}

TEST_CASE("procgen.splinePath.supportsBezierClosedAndClosestQueries") {
    SplinePath path;
    REQUIRE(path.setKindResult("bezier").ok());
    auto a = point(0.f, 0.f, 0.f);
    a.outY = 2.f;
    auto b = point(2.f, 0.f, 0.f);
    b.inY  = 2.f;
    REQUIRE(path.addPointResult(a).ok());
    REQUIRE(path.addPointResult(b).ok());
    auto midpoint = path.evaluateResult(0.5f);
    REQUIRE(midpoint.ok());
    CHECK(std::abs(midpoint.value().x - 1.f) < 1e-5f);
    CHECK(midpoint.value().y > 1.49f);
    auto closest = path.closestPointResult(1.f, 1.4f, 0.f, 64);
    REQUIRE(closest.ok());
    CHECK(std::abs(closest.value().x - 1.f) < 0.05f);
    path.setClosed(true);
    CHECK_EQ(path.segmentCount(), 2);
}

TEST_CASE("procgen.splinePath.supportsExplicitQuadraticBezierInterpolation") {
    SplinePath path;
    REQUIRE(path.setKindResult("quadraticBezier").ok());
    SplinePoint first{0.f, 0.f, 0.f};
    first.outY = 2.f;
    SplinePoint second{2.f, 0.f, 0.f};
    second.inY = 2.f;
    REQUIRE(path.addPointResult(first).ok());
    REQUIRE(path.addPointResult(second).ok());
    const auto middle = path.evaluateResult(0.5f);
    REQUIRE(middle.ok());
    CHECK(std::abs(middle.value().x - 1.f) < 1e-5f);
    CHECK(std::abs(middle.value().y - 1.f) < 1e-5f);
    CHECK(std::abs(middle.value().tangentX - 1.f) < 1e-5f);
    CHECK(std::abs(middle.value().tangentY) < 1e-5f);
}

TEST_CASE("procgen.splinePath.rejectsInvalidStateAtomically") {
    SplinePath path;
    const auto revision = path.revision();
    CHECK(!path.setKindResult("nurbs").ok());
    CHECK_EQ(path.revision(), revision);
    CHECK(!path.evaluateResult(0.5f).ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    CHECK(!path.addPointResult(point(NAN, 0.f, 0.f)).ok());
    CHECK_EQ(path.pointCount(), 1);
}

TEST_CASE("procgen.splinePath.parallelTransportFramesStayOrthonormal") {
    SplinePath path;
    REQUIRE(path.setKindResult("catmullRom").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(1.f, 2.f, 0.5f)).ok());
    REQUIRE(path.addPointResult(point(-1.f, 4.f, 1.5f)).ok());
    REQUIRE(path.addPointResult(point(0.5f, 6.f, 0.f)).ok());
    auto frames = path.sampleFramesResult(32, true, 17.f, 32);
    REQUIRE(frames.ok());
    CHECK_EQ(frames.value().size(), std::size_t(33));
    for (const auto& frame : frames.value()) {
        const float sideLength =
            std::sqrt(frame.sideX * frame.sideX + frame.sideY * frame.sideY + frame.sideZ * frame.sideZ);
        const float upLength    = std::sqrt(frame.upX * frame.upX + frame.upY * frame.upY + frame.upZ * frame.upZ);
        const float sideTangent = frame.sideX * frame.sample.tangentX + frame.sideY * frame.sample.tangentY +
                                  frame.sideZ * frame.sample.tangentZ;
        const float upTangent =
            frame.upX * frame.sample.tangentX + frame.upY * frame.sample.tangentY + frame.upZ * frame.sample.tangentZ;
        CHECK(std::abs(sideLength - 1.f) < 1e-5f);
        CHECK(std::abs(upLength - 1.f) < 1e-5f);
        CHECK(std::abs(sideTangent) < 1e-5f);
        CHECK(std::abs(upTangent) < 1e-5f);
    }
}

TEST_CASE("procgen.splinePath.closedTransportFramesCorrectHolonomy") {
    SplinePath path;
    REQUIRE(path.setKindResult("catmullRom").ok());
    path.setClosed(true);
    REQUIRE(path.addPointResult(point(-2.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(0.f, 1.f, 2.f)).ok());
    REQUIRE(path.addPointResult(point(2.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(0.f, -1.f, -2.f)).ok());
    auto frames = path.sampleFramesResult(48, true);
    REQUIRE(frames.ok());
    const auto& first = frames.value().front();
    const auto& last  = frames.value().back();
    CHECK(std::abs(first.sample.x - last.sample.x) < 1e-6f);
    CHECK(std::abs(first.sideX - last.sideX) < 1e-6f);
    CHECK(std::abs(first.sideY - last.sideY) < 1e-6f);
    CHECK(std::abs(first.sideZ - last.sideZ) < 1e-6f);
    CHECK(std::abs(first.upX - last.upX) < 1e-6f);
    CHECK(std::abs(first.upY - last.upY) < 1e-6f);
    CHECK(std::abs(first.upZ - last.upZ) < 1e-6f);
    CHECK(std::abs(last.sample.normalizedDistance - 1.f) < 1e-6f);
}

TEST_CASE("procgen.splinePath.interpolatesPointRollAndProfileScale") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 4.f)).ok());
    REQUIRE(path.setPointProfileResult(0, 0.f, 1.f, 1.f).ok());
    REQUIRE(path.setPointProfileResult(1, 90.f, 3.f, 2.f).ok());
    auto midpoint = path.evaluateResult(0.5f);
    REQUIRE(midpoint.ok());
    CHECK(std::abs(midpoint.value().rollDegrees - 45.f) < 1e-6f);
    CHECK(std::abs(midpoint.value().scaleX - 2.f) < 1e-6f);
    CHECK(std::abs(midpoint.value().scaleY - 1.5f) < 1e-6f);
    const auto revision = path.revision();
    CHECK(!path.setPointProfileResult(1, 0.f, 0.f, 1.f).ok());
    CHECK_EQ(path.revision(), revision);
}

TEST_CASE("procgen.splinePath.pointRotationProducesCompleteLocalOrientation") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 4.f)).ok());
    REQUIRE(path.setPointRotationResult(0, 90.f, 0.f, 0.f).ok());
    REQUIRE(path.setPointRotationResult(1, 90.f, 0.f, 0.f).ok());
    auto frames = path.sampleFramesResult(4, true);
    REQUIRE(frames.ok());
    const auto& frame = frames.value()[2];
    CHECK(std::abs(frame.forwardY + 1.f) < 1e-5f);
    CHECK(std::abs(frame.forwardX * frame.sideX + frame.forwardY * frame.sideY + frame.forwardZ * frame.sideZ) < 1e-5f);
    const auto revision = path.revision();
    CHECK(!path.setPointRotationResult(0, std::numeric_limits<float>::infinity(), 0.f, 0.f).ok());
    CHECK_EQ(path.revision(), revision);
}

TEST_CASE("procgen.splinePath.travelWrapModesAreDistanceDeterministic") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(10.f, 0.f, 0.f)).ok());
    auto clamped = path.travelResult(12.f, "clamp");
    auto looped  = path.travelResult(12.f, "loop");
    auto bounced = path.travelResult(12.f, "pingPong");
    REQUIRE(clamped.ok());
    REQUIRE(looped.ok());
    REQUIRE(bounced.ok());
    CHECK(std::abs(clamped.value().x - 10.f) < 1e-5f);
    CHECK(std::abs(looped.value().x - 2.f) < 1e-5f);
    CHECK(std::abs(bounced.value().x - 8.f) < 1e-5f);
    auto negative = path.travelResult(-2.f, "loop");
    REQUIRE(negative.ok());
    CHECK(std::abs(negative.value().x - 8.f) < 1e-5f);
    CHECK(!path.travelResult(1.f, "teleport").ok());
    auto frame = path.travelFrameResult(5.f, "clamp", 24);
    REQUIRE(frame.ok());
    CHECK(std::abs(frame.value().sample.x - 5.f) < 1e-5f);
    const float sideLength =
        std::sqrt(frame.value().sideX * frame.value().sideX + frame.value().sideY * frame.value().sideY +
                  frame.value().sideZ * frame.value().sideZ);
    CHECK(std::abs(sideLength - 1.f) < 1e-5f);
    CHECK(std::abs(frame.value().sideX * frame.value().sample.tangentX +
                   frame.value().sideY * frame.value().sample.tangentY +
                   frame.value().sideZ * frame.value().sample.tangentZ) < 1e-5f);
}

TEST_CASE("procgen.splinePath.distributesOwningSamplesWithoutClosedDuplicate") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(9.f, 0.f, 0.f)).ok());
    auto open = path.distributeResult(4, true);
    REQUIRE(open.ok());
    CHECK_EQ(open.value().count(), 4);
    CHECK(std::abs(open.value().sampleResult(1).value().x - 3.f) < 1e-5f);
    CHECK(std::abs(open.value().sampleResult(3).value().x - 9.f) < 1e-5f);
    auto openFrame = open.value().frameResult(1);
    REQUIRE(openFrame.ok());
    CHECK(std::abs(openFrame.value().sample.x - 3.f) < 1e-5f);
    CHECK(std::abs(openFrame.value().sideX * openFrame.value().upX + openFrame.value().sideY * openFrame.value().upY +
                   openFrame.value().sideZ * openFrame.value().upZ) < 1e-5f);
    CHECK(!open.value().sampleResult(4).ok());
    CHECK(!open.value().frameResult(4).ok());

    path.setClosed(true);
    auto closed = path.distributeResult(4, true);
    REQUIRE(closed.ok());
    CHECK_EQ(closed.value().count(), 4);
    CHECK(closed.value().sampleResult(3).value().normalizedDistance < 1.f);
}

TEST_CASE("procgen.splinePath.shapePresetsReplaceAtomically") {
    SplinePath path;
    REQUIRE(path.applyShapePresetResult("circle", 12, 3.f, 0.f).ok());
    CHECK(path.isClosed());
    CHECK_EQ(path.pointCount(), 12);
    auto start = path.evaluateResult(0.f);
    auto end   = path.evaluateResult(1.f);
    REQUIRE(start.ok());
    REQUIRE(end.ok());
    CHECK(std::abs(start.value().x - end.value().x) < 1e-5f);
    const auto revision = path.revision();
    CHECK(!path.applyShapePresetResult("spiral", 1, 3.f, 4.f, 2.f).ok());
    CHECK_EQ(path.revision(), revision);
    CHECK_EQ(path.pointCount(), 12);
    REQUIRE(path.applyShapePresetResult("spiral", 16, 3.f, 4.f, 2.f).ok());
    CHECK(!path.isClosed());
    CHECK_EQ(path.kind(), std::string_view("catmullRom"));
    auto tip = path.evaluateResult(1.f);
    REQUIRE(tip.ok());
    CHECK(std::abs(tip.value().y - 4.f) < 1e-5f);
}

TEST_CASE("procgen.splinePath.buildsRendererNeutralOpenAndClosedPolylines") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(4.f, 0.f, 0.f)).ok());
    auto open = path.polylineResult(5, true, 24);
    REQUIRE(open.ok());
    CHECK_EQ(open.value().count(), 5);
    CHECK(!open.value().isClosed());
    CHECK(std::abs(open.value().pointResult(4).value().x - 4.f) < 1e-6f);
    CHECK(!open.value().pointResult(5).ok());

    path.setClosed(true);
    auto closed = path.polylineResult(8, true, 24);
    REQUIRE(closed.ok());
    CHECK(closed.value().isClosed());
    CHECK_EQ(closed.value().count(), 8);
    const auto first = closed.value().pointResult(0);
    const auto last  = closed.value().pointResult(7);
    REQUIRE(first.ok());
    REQUIRE(last.ok());
    CHECK(std::abs(first.value().x - last.value().x) > 1e-4f);
}

TEST_CASE("procgen.splinePath.disconnectedChunksExcludeGapFromLengthAndPolyline") {
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult(point(0.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(1.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(10.f, 0.f, 0.f)).ok());
    REQUIRE(path.addPointResult(point(11.f, 0.f, 0.f)).ok());
    REQUIRE(path.setPointChunkBreakResult(2, true).ok());
    CHECK_EQ(path.chunkCount(), 2);
    CHECK_EQ(path.segmentCount(), 2);
    auto length = path.lengthResult(16);
    REQUIRE(length.ok());
    CHECK(std::abs(length.value() - 2.f) < 1e-5f);
    auto afterBreak = path.evaluateDistanceResult(1.1f, 16);
    REQUIRE(afterBreak.ok());
    CHECK(std::abs(afterBreak.value().x - 10.1f) < 1e-4f);
    CHECK_EQ(afterBreak.value().chunkIndex, 1);
    auto polyline = path.polylineResult(8, true, 16);
    REQUIRE(polyline.ok());
    CHECK_EQ(polyline.value().chunkCount(), 2);
    CHECK_EQ(polyline.value().chunkPointCount(0), 4);
    CHECK_EQ(polyline.value().chunkPointCount(1), 4);
    CHECK(std::abs(polyline.value().chunkPointResult(0, 3).value().x - 1.f) < 1e-5f);
    CHECK(std::abs(polyline.value().chunkPointResult(1, 0).value().x - 10.f) < 1e-5f);
    REQUIRE(path.setPointChunkBreakResult(2, false).ok());
    CHECK_EQ(path.chunkCount(), 1);
}

// procgen_worker_contract.cpp
using namespace eve::procgen;

static_assert(std::is_copy_constructible_v<ProcgenGenerationJob>);
static_assert(!std::is_copy_constructible_v<RuntimeGeneration>);
static_assert(!std::is_move_constructible_v<RuntimeGeneration>);

TEST_CASE("procgen.worker.rejectsJobsAfterClearAndOwnerDestruction") {
    ProcgenGenerationJob job;
    {
        RuntimeGeneration original(42);
        REQUIRE(original.addLevel(10.f, 4.f, 2.f) == 0);
        original.updateSource(5.f, 5.f, 1.f, 0.f);
        auto issued = original.nextGenerationJob();
        REQUIRE(issued.ok());
        auto value = std::move(issued).takeValue();
        REQUIRE(value.has_value());
        job = *value;
        original.clear();
        auto stale = original.completeGenerationJob({job, PointSet{}});
        CHECK(!stale.ok());
    }
    RuntimeGeneration replacement(42);
    REQUIRE(replacement.addLevel(10.f, 4.f, 2.f) == 0);
    replacement.updateSource(5.f, 5.f, 1.f, 0.f);
    auto issued = replacement.nextGenerationJob();
    REQUIRE(issued.ok());
    REQUIRE(std::move(issued).takeValue().has_value());
    auto stale = replacement.completeGenerationJob({job, PointSet{}});
    CHECK(!stale.ok());
    CHECK(replacement.getGeneratingCount() == 1);
}

TEST_CASE("procgen.worker.rejectsForeignAndReplayedJobs") {
    RuntimeGeneration first(42), second(42);
    for (auto* runtime : {&first, &second}) {
        REQUIRE(runtime->addLevel(10.f, 4.f, 2.f) == 0);
        runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    }
    auto a = first.nextGenerationJob();
    auto b = second.nextGenerationJob();
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto job   = std::move(a).takeValue();
    auto other = std::move(b).takeValue();
    REQUIRE(job.has_value());
    REQUIRE(other.has_value());
    REQUIRE(job->getTicket() == other->getTicket());
    auto foreign = second.completeGenerationJob({*job, PointSet{}});
    CHECK(!foreign.ok());
    CHECK(second.getGeneratingCount() == 1);
    auto accepted = first.completeGenerationJob({*job, PointSet{}});
    REQUIRE(accepted.ok());
    auto replay = first.completeGenerationJob({*job, PointSet{}});
    CHECK(!replay.ok());
    auto own = second.completeGenerationJob({*other, PointSet{}});
    CHECK(own.ok());
}
