#include "zeroerr/unittest.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Capability.h"
#include "common/ProcgenProbeSink.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainProbePlacement.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"

using namespace eve::procgen;

TEST_CASE("procgen.probe.reflectionLightHeightMetadataDeterminismAndAtomicFailure") {
    Heightmap fitness(2, 2), heights(2, 2);
    std::fill(fitness.data().begin(), fitness.data().end(), 1.0F);
    std::fill(heights.data().begin(), heights.data().end(), 0.5F);
    TerrainProbePlacementSettings settings;
    settings.name = "forest-probe";
    settings.width = settings.depth = 2;
    settings.spacing = 1;
    settings.jitterPercent = 0;
    settings.minimumFitness = 0;
    settings.heightScale = 10;
    settings.seaLevelActive = true;
    settings.seaLevel = 4;
    settings.reflectionOffset = 2;
    settings.namespaceId = 901;
    PointSet first, second;
    REQUIRE(exportTerrainProbePoints(first, fitness, heights, settings).ok());
    REQUIRE(exportTerrainProbePoints(second, fitness, heights, settings).ok());
    REQUIRE(first.getCount() == 9);
    REQUIRE(second.getCount() == first.getCount());
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK(first.points()[i].id == second.points()[i].id);
        CHECK(first.points()[i].x == second.points()[i].x);
        CHECK(first.points()[i].z == second.points()[i].z);
    }
    CHECK(first.points()[0].y == 7);
    CHECK(first.getStringAttribute(0, "probeResource", "") == "forest-probe");
    CHECK(first.getIntAttribute(0, "probeType", -1) == 0);
    settings.type = TerrainProbeType::Light;
    settings.lightOffset = 2.5F;
    REQUIRE(exportTerrainProbePoints(second, fitness, heights, settings).ok());
    CHECK(second.points()[0].y == 7.5F);
    CHECK(second.getIntAttribute(0, "probeType", -1) == 1);
    const int beforeCount = second.getCount();
    const auto beforeId = second.points()[0].id;
    settings.reflectionResolution = 24;
    CHECK(!exportTerrainProbePoints(second, fitness, heights, settings).ok());
    CHECK(second.getCount() == beforeCount);
    CHECK(second.points()[0].id == beforeId);
}

TEST_CASE("procgen.probe.realVmBinding") {
    Heightmap fitness(2, 2), heights(2, 2);
    std::fill(fitness.data().begin(), fitness.data().end(), 1.0F);
    std::fill(heights.data().begin(), heights.data().end(), 1.0F);
    PointSet output;
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainProbePlacementSettings();
        s.name="vm-probe";s.width=2.0;s.depth=2.0;s.spacing=1.0;s.jitterPercent=0.0;
        s.minimumFitness=0.0;s.namespaceId=902;
        local r=eve.generateTerrainProbes(output(),fitness(),heights(),s,0);
        assert(r.ok && r.value==9);
    )"));
    CHECK(output.getCount() == 9);
}

TEST_CASE("procgen.probe.graphicsProviderPublicationAndMissingProvider") {
    struct ProbeSink final : eve::IProcgenProbeSink {
        int count = 0, reflections = 0, lights = 0;
        eve::Result<int> replaceProbeBatch(const std::string&, const std::vector<eve::ProcgenProbeDesc>& probes) override {
            count = reflections = lights = 0;
            for (const auto& probe : probes) { ++count; probe.type == 0 ? ++reflections : ++lights; }
            return eve::Result<int>::success(count);
        }
        eve::Result<int> removeProbeBatch(const std::string&) override {
            const int removed = count; count = reflections = lights = 0; return eve::Result<int>::success(removed);
        }
        eve::Result<int> tickProbeBatches(int, int, int) override { return eve::Result<int>::success(0); }
        int probeCount(const std::string&) const override { return count; }
        int reflectionProbeCount(const std::string&) const override { return reflections; }
        int lightProbeCount(const std::string&) const override { return lights; }
    } sink;
    auto* previous = eve::cap::query<eve::IProcgenProbeSink>();
    struct RestoreProvider {
        eve::IProcgenProbeSink* previous;
        eve::IProcgenProbeSink* temporary;
        ~RestoreProvider() {
            eve::cap::revoke<eve::IProcgenProbeSink>(temporary);
            if (previous) eve::cap::provide<eve::IProcgenProbeSink>(previous);
        }
    } restore{previous, &sink};
    eve::cap::provide<eve::IProcgenProbeSink>(&sink);
    Heightmap fitness(2, 2), heights(2, 2);
    std::fill(fitness.data().begin(), fitness.data().end(), 1.0F);
    std::fill(heights.data().begin(), heights.data().end(), 1.0F);
    PointSet output;
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainProbePlacementSettings();
        s.name="provider-probe";s.width=1.0;s.depth=1.0;s.spacing=1.0;s.jitterPercent=0.0;
        s.minimumFitness=0.0;s.namespaceId=904;
        assert(eve.generateTerrainProbes(output(),fitness(),heights(),s,0).ok);
        local published=eve.publishTerrainProbes("probe-test",output());
        assert(published.ok && eve.getTerrainProbeBatchCount("probe-test")==published.value);
        assert(eve.getTerrainReflectionProbeCount("probe-test")==published.value);
        assert(eve.removeTerrainProbeBatch("probe-test").ok);
    )"));
    eve::cap::revoke<eve::IProcgenProbeSink>(&sink);
    vm.run(vm.compileSource(R"(
        local missing=eve.publishTerrainProbes("probe-test",output());
        assert(!missing.ok && eve.getTerrainProbeBatchCount("probe-test")==0);
    )"));
}

TEST_CASE("procgen.probe.multiTileAddReplaceRemoveAndAtomicFailure") {
    Heightmap fitness(4, 2), leftHeight(3, 3), rightHeight(3, 3);
    std::fill(fitness.data().begin(), fitness.data().end(), 1.0F);
    std::fill(leftHeight.data().begin(), leftHeight.data().end(), 0.25F);
    std::fill(rightHeight.data().begin(), rightHeight.data().end(), 0.75F);
    PointSet left, right;
    std::vector<TerrainProbeTile> tiles{{"left", &left, &leftHeight, -2, -1, 2, 2, 2, 2, false},
                                        {"right", &right, &rightHeight, 0, -1, 2, 2, 2, 2, false}};
    TerrainProbePlacementSettings settings;
    settings.name = "world-probes";
    settings.spacing = 1;
    settings.jitterPercent = settings.minimumFitness = 0;
    settings.heightScale = 8;
    settings.seaLevelActive = true;
    settings.reflectionOffset = 1;
    settings.namespaceId = 903;
    settings.maxPoints = 100;
    TerrainStampSettings operation;
    operation.width = 4;
    operation.depth = 2;
    auto added = applyTerrainProbesMultiTile(tiles, fitness, settings, operation,
                                              TerrainProbeOperationMode::Add);
    REQUIRE(added.ok());
    CHECK(added.value().affectedTiles == 2);
    CHECK(!left.empty());
    CHECK(!right.empty());
    CHECK(left.points()[0].y == 3);
    CHECK(right.points()[0].y == 7);
    settings.type = TerrainProbeType::Light;
    REQUIRE(applyTerrainProbesMultiTile(tiles, fitness, settings, operation,
                                        TerrainProbeOperationMode::Replace).ok());
    CHECK(left.getIntAttribute(0, "probeType", -1) == 1);
    REQUIRE(applyTerrainProbesMultiTile(tiles, fitness, settings, operation,
                                        TerrainProbeOperationMode::Remove).ok());
    CHECK(left.empty());
    CHECK(right.empty());
    settings.name = "atomic";
    REQUIRE(applyTerrainProbesMultiTile(tiles, fitness, settings, operation,
                                        TerrainProbeOperationMode::Add).ok());
    const auto before = left.points()[0].id;
    settings.reflectionResolution = 24;
    CHECK(!applyTerrainProbesMultiTile(tiles, fitness, settings, operation,
                                       TerrainProbeOperationMode::Replace).ok());
    CHECK(left.points()[0].id == before);
}
