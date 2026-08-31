#include "common/Capability.h"
#include "common/ProcgenSceneSink.h"
#include "procgen/Procgen.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace eve::procgen;

namespace {

class MockProcgenSceneSink final : public eve::IProcgenSceneSink {
public:
    bool applyBatch(const std::string& batchId, const std::vector<eve::ProcgenInstanceDesc>& instances) override {
        const auto previous = batches.find(batchId);
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
        stats.removed = int(oldIds.size());
        batches[batchId] = instances;
        revisions[batchId] = revisions[batchId] + 1;
        latest[batchId]  = stats;
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
    eve::Result<uint64_t> applyDelta(const std::string& batchId, const eve::ProcgenInstanceDelta& delta) override {
        const auto current = revisions.find(batchId);
        if (current == revisions.end() || current->second != delta.baseRevision)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock delta revision is stale"));
        lastDeltaBatch     = batchId;
        lastDelta          = delta;
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
    int                                                                    replaceCalls = 0;
    int                                                                    deltaCalls   = 0;
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
    REQUIRE(proc.releasePointSet(pointsHandle).ok());
}
