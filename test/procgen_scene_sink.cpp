#include "common/Capability.h"
#include "common/ProcgenSceneSink.h"
#include "procgen/Procgen.h"

#include <zeroerr.hpp>

#include <unordered_map>

using namespace eve::procgen;

namespace {

class MockProcgenSceneSink final : public eve::IProcgenSceneSink {
public:
    bool applyBatch(const std::string& batchId,
                    const std::vector<eve::ProcgenInstanceDesc>& instances) override {
        batches[batchId] = instances;
        return true;
    }
    bool removeBatch(const std::string& batchId) override { return batches.erase(batchId) != 0; }
    int instanceCount(const std::string& batchId) const override {
        const auto found = batches.find(batchId);
        return found == batches.end() ? 0 : int(found->second.size());
    }

    std::unordered_map<std::string, std::vector<eve::ProcgenInstanceDesc>> batches;
};

}  // namespace

TEST_CASE("procgen.sceneSink.publishesStableAttributedInstances") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);

    Procgen  proc;
    PointSet points;
    const int tree = points.add(1.f, 2.f, 3.f);
    points.setPointSeed(tree, 77);
    points.setYaw(tree, 30.f);
    points.setScale(tree, 2.f, 3.f, 4.f);
    points.setStringAttribute(tree, "asset", "oak");
    const int rock = points.add(8.f, 0.f, 9.f);
    points.setPointSeed(rock, 88);

    CHECK(proc.publishInstances("forest/main", &points, "asset", "granite"));
    CHECK_EQ(proc.getPublishedInstanceCount("forest/main"), 2);
    const auto& published = sink.batches.at("forest/main");
    CHECK_EQ(published[0].id, std::string("pcg-77-0"));
    CHECK_EQ(published[0].asset, std::string("oak"));
    CHECK_EQ(published[0].x, 1.f);
    CHECK_EQ(published[0].yaw, 30.f);
    CHECK_EQ(published[0].scaleZ, 4.f);
    CHECK_EQ(published[1].asset, std::string("granite"));

    CHECK(proc.removeInstances("forest/main"));
    CHECK_EQ(proc.getPublishedInstanceCount("forest/main"), 0);
    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}

TEST_CASE("procgen.sceneSink.reportsMissingProvider") {
    eve::cap::detail::clearAllRaw();
    Procgen  proc;
    PointSet points;
    points.add(0.f, 0.f, 0.f);
    CHECK(!proc.publishInstances("missing", &points, "asset", "tree"));
    CHECK(proc.lastError().find("unavailable") != std::string::npos);
}
