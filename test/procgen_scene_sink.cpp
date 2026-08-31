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
    eve::Result<uint64_t> applyDelta(const std::string&, const eve::ProcgenInstanceDelta&) override {
        return eve::Result<uint64_t>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "mock delta path is not configured"));
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
    std::unordered_map<std::string, uint64_t> revisions;
    std::unordered_map<std::string, Stats> latest;
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
    pointsView->setPointSeed(tree, 77);
    pointsView->setYaw(tree, 30.f);
    pointsView->setScale(tree, 2.f, 3.f, 4.f);
    pointsView->setStringAttribute(tree, "asset", "oak");
    const int rock = pointsView->add(8.f, 0.f, 9.f);
    pointsView->setPointSeed(rock, 88);

    auto publishedResult = proc.publishInstances("forest/main", pointsHandle, "asset", "granite");
    REQUIRE(publishedResult.ok());
    CHECK_EQ(proc.getPublishedInstanceCount("forest/main"), 2);
    CHECK_EQ(proc.getPublishedCreatedCount("forest/main"), 2);
    CHECK_EQ(proc.getPublishedReusedCount("forest/main"), 0);
    CHECK_EQ(proc.getPublishedRemovedCount("forest/main"), 0);
    const auto& published = sink.batches.at("forest/main");
    CHECK_EQ(published[0].id, std::string("pcg-77-0"));
    CHECK_EQ(published[0].asset, std::string("oak"));
    CHECK_EQ(published[0].x, 1.f);
    CHECK_EQ(published[0].yaw, 30.f);
    CHECK_EQ(published[0].scaleZ, 4.f);
    CHECK_EQ(published[1].asset, std::string("granite"));
    CHECK_EQ(published[1].id, std::string("pcg-88-0"));

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
    CHECK_EQ(reconciled[1].id, std::string("pcg-77-0"));
    CHECK_EQ(reconciled[2].id, std::string("pcg-88-0"));

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
    REQUIRE(proc.releasePointSet(pointsHandle).ok());
}
