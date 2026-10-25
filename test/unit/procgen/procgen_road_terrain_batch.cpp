// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "procgen_road_terrain_batch.cpp"
  ]
}
*/
// clang-format on

#include <array>
#include <cmath>
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadTerrainWorkspace.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;
namespace {
RoadTerrainTile batchTile() {
    RoadTerrainTile t;
    t.id      = "batch";
    t.originX = t.originZ = -5.f;
    t.originY             = 100.f;
    t.sizeX = t.sizeZ = 10.f;
    t.heightScale     = 10.f;
    t.heights         = Heightmap(11, 11);
    t.holes           = Heightmap(11, 11);
    for (auto& v : t.heights.data()) v = .5f;
    t.splatLayers = {Heightmap(11, 11), Heightmap(11, 11), Heightmap(11, 11)};
    for (auto& v : t.splatLayers[0].data()) v = 1.f;
    return t;
}
MeshBuild batchMesh(float x) {
    MeshBuild m;
    m.setActiveGroup("asphalt");
    for (auto p :
         std::array<std::array<float, 2>, 4>{{{x - .75f, -1.f}, {x + .75f, -1.f}, {x + .75f, 1.f}, {x - .75f, 1.f}}})
        m.addVertex(p[0], 106.f, p[1], 0, 1, 0, 0, 0);
    m.addTriangle(0, 1, 2);
    m.addTriangle(0, 2, 3);
    return m;
}
RoadTerrainWriteOptions batchPolicy(int layer, float opacity) {
    RoadTerrainWriteOptions p;
    p.blendDistance = 0;
    p.splatLayer    = layer;
    p.splatOpacity  = opacity;
    return p;
}
}  // namespace
TEST_CASE("procgen.road.terrain.batchPoliciesShareOneHistoryAndSave") {
    auto                            first = batchMesh(-2), second = batchMesh(2);
    std::array<RoadTerrainWrite, 2> writes{
        {{std::cref(first), batchPolicy(1, .4f)}, {std::cref(second), batchPolicy(2, .8f)}}};
    RoadTerrainWorkspace w;
    REQUIRE(w.initialize({batchTile()}).ok());
    REQUIRE(w.applyBatch(writes, 100000).ok());
    auto t = w.copyTile("batch");
    REQUIRE(t.ok());
    REQUIRE(std::fabs(t.value().splatLayers[1].height(3, 5) - .4f) < 1e-6f);
    REQUIRE(std::fabs(t.value().splatLayers[2].height(7, 5) - .8f) < 1e-6f);
    REQUIRE_EQ(t.value().splatLayers[2].height(3, 5), 0.f);
    REQUIRE_EQ(t.value().splatLayers[1].height(7, 5), 0.f);
    REQUIRE(w.undo().ok());
    auto saved = w.capture();
    REQUIRE(saved.ok());
    auto json = saved.value().toJson();
    REQUIRE(json.ok());
    auto parsed = eve::Value::fromJson(json.value());
    REQUIRE(parsed.ok());
    RoadTerrainWorkspace restored;
    REQUIRE(restored.restore(parsed.value()).ok());
    auto old = restored.copyTile("batch");
    REQUIRE(old.ok());
    REQUIRE_EQ(old.value().splatLayers[1].height(3, 5), 0.f);
    REQUIRE_EQ(old.value().splatLayers[2].height(7, 5), 0.f);
    REQUIRE(!restored.undo().ok());
    REQUIRE(restored.redo().ok());
    auto after = restored.copyTile("batch");
    REQUIRE(after.ok());
    for (std::size_t l = 0; l < 3; ++l)
        REQUIRE_EQ(after.value().splatLayers[l].data(), t.value().splatLayers[l].data());
    RoadTerrainWorkspace selected;
    REQUIRE(selected.initialize({batchTile()}).ok());
    REQUIRE(selected.applyBatch(std::span<const RoadTerrainWrite>(writes.data(), 1), 100000).ok());
    auto onlyFirst = selected.copyTile("batch");
    REQUIRE(onlyFirst.ok());
    REQUIRE(std::fabs(onlyFirst.value().splatLayers[1].height(3, 5) - .4f) < 1e-6f);
    REQUIRE_EQ(onlyFirst.value().splatLayers[2].height(7, 5), 0.f);
}
TEST_CASE("procgen.road.terrain.batchLateFailureAndSharedBudgetPreserveRedo") {
    auto                            first = batchMesh(-2), second = batchMesh(2);
    std::array<RoadTerrainWrite, 2> writes{
        {{std::cref(first), batchPolicy(1, .4f)}, {std::cref(second), batchPolicy(2, .8f)}}};
    RoadTerrainWorkspace w;
    REQUIRE(w.initialize({batchTile()}).ok());
    REQUIRE(w.applyBatch(writes, 100000).ok());
    REQUIRE(w.undo().ok());
    auto before = w.capture();
    REQUIRE(before.ok());
    writes[1].options.splatLayer = 3;
    REQUIRE(!w.applyBatch(writes, 100000).ok());
    auto failed = w.capture();
    REQUIRE(failed.ok());
    REQUIRE(failed.value() == before.value());
    writes[1].options.splatLayer  = 2;
    writes[1].options.maximumWork = 1;
    REQUIRE(!w.applyBatch(writes, 100000).ok());
    auto limited = w.capture();
    REQUIRE(limited.ok());
    REQUIRE(limited.value() == before.value());
    writes[1].options.maximumWork = 100000;
    std::size_t low = 1, high = 100000;
    while (low < high) {
        const auto           middle = low + (high - low) / 2;
        RoadTerrainWorkspace probe;
        REQUIRE(probe.initialize({batchTile()}).ok());
        auto applied = probe.applyBatch(std::span<const RoadTerrainWrite>(writes.data(), 1), middle);
        if (applied.ok())
            high = middle;
        else
            low = middle + 1;
    }
    REQUIRE(!w.applyBatch(writes, low).ok());
    auto exhausted = w.capture();
    REQUIRE(exhausted.ok());
    REQUIRE(exhausted.value() == before.value());
    REQUIRE(!w.applyBatch({}, 100000).ok());
    REQUIRE(!w.applyBatch(writes, 0).ok());
    REQUIRE(w.redo().ok());
}
TEST_CASE("procgen.road.terrain.batchNativeWorldPublishesOnce") {
    TerrainWorldCreationSettings s;
    s.tileSize                 = 10;
    s.tileHeight               = 10;
    s.originY                  = 100;
    s.heightmapResolution      = 17;
    s.controlTextureResolution = s.detailResolution = 16;
    s.treeResolution = s.objectResolution = 4;
    s.splatLayers                         = 3;
    TerrainWorldWorkspace w;
    REQUIRE(w.create(s).ok());
    REQUIRE(w.setHeightWorldUnits(105, 10).ok());
    auto                            first = batchMesh(-2), second = batchMesh(2);
    std::array<RoadTerrainWrite, 2> writes{
        {{std::cref(first), batchPolicy(1, .4f)}, {std::cref(second), batchPolicy(2, .8f)}}};
    const auto history = w.getOperationCount();
    REQUIRE(w.conformRoadBatch(writes, 100000).ok());
    REQUIRE_EQ(w.getOperationCount(), history + 1);
    TerrainSplatmap splat;
    REQUIRE(w.copySplatmap(0, splat).ok());
    REQUIRE(std::fabs(splat.sample(1, 5, 8).value() - .4f) < 1e-6f);
    REQUIRE(std::fabs(splat.sample(2, 10, 8).value() - .8f) < 1e-6f);
    REQUIRE(w.undo().ok());
    REQUIRE(w.copySplatmap(0, splat).ok());
    REQUIRE_EQ(splat.sample(1, 5, 8).value(), 0.f);
    REQUIRE_EQ(splat.sample(2, 10, 8).value(), 0.f);
    const auto count             = w.getOperationCount();
    writes[1].options.splatLayer = 4;
    REQUIRE(!w.conformRoadBatch(writes, 100000).ok());
    REQUIRE_EQ(w.getOperationCount(), count);
    REQUIRE(w.redo().ok());
    REQUIRE(w.copySplatmap(0, splat).ok());
    REQUIRE(std::fabs(splat.sample(1, 5, 8).value() - .4f) < 1e-6f);
    REQUIRE(std::fabs(splat.sample(2, 10, 8).value() - .8f) < 1e-6f);
}
