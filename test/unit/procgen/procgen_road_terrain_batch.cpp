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

#include "common/Value.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadTerrainWorkspace.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>
#include <functional>
#include <span>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
RoadTerrainTile tile() {
    RoadTerrainTile value;
    value.id      = "batch";
    value.originX = value.originZ = -5.f;
    value.originY                 = 100.f;
    value.sizeX = value.sizeZ = 10.f;
    value.heightScale         = 10.f;
    value.heights             = Heightmap(11, 11);
    value.holes               = Heightmap(11, 11);
    for (auto& height : value.heights.data()) height = .5f;
    value.splatLayers = {Heightmap(11, 11), Heightmap(11, 11), Heightmap(11, 11)};
    for (auto& weight : value.splatLayers[0].data()) weight = 1.f;
    return value;
}

MeshBuild asphalt(float center) {
    MeshBuild mesh;
    mesh.setActiveGroup("asphalt");
    const std::array<std::array<float, 2>, 4> corners{
        {{center - .75f, -1.f}, {center + .75f, -1.f}, {center + .75f, 1.f}, {center - .75f, 1.f}}};
    for (auto point : corners) mesh.addVertex(point[0], 106.f, point[1], 0, 1, 0, 0, 0);
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    return mesh;
}

RoadTerrainWriteOptions policy(int layer, float opacity) {
    RoadTerrainWriteOptions options;
    options.blendDistance = 0.f;
    options.splatLayer    = layer;
    options.splatOpacity  = opacity;
    return options;
}

using Writes = std::array<RoadTerrainWrite, 2>;
Writes writesFor(const MeshBuild& left, const MeshBuild& right) {
    return {{{std::cref(left), policy(1, .4f)}, {std::cref(right), policy(2, .8f)}}};
}

void expectWeights(const RoadTerrainTile& value) {
    REQUIRE(std::fabs(value.splatLayers[1].height(3, 5) - .4f) < 1e-6f);
    REQUIRE(std::fabs(value.splatLayers[2].height(7, 5) - .8f) < 1e-6f);
    REQUIRE(value.splatLayers[2].height(3, 5) == 0.f);
    REQUIRE(value.splatLayers[1].height(7, 5) == 0.f);
}
}  // namespace

TEST_CASE("procgen.road.terrain.spec.workspaceBatch") {
    auto left   = asphalt(-2.f);
    auto right  = asphalt(2.f);
    auto writes = writesFor(left, right);
    SUB_CASE("each write uses its own layer and opacity without leaking to the other footprint") {
        RoadTerrainWorkspace workspace;
        auto*                workspaceView = &workspace;
        REQUIRE(workspaceView->initialize({tile()}).ok());
        REQUIRE(workspaceView->applyBatch(writes, 100000).ok());
        auto result = workspaceView->copyTile("batch");
        REQUIRE(result.ok());
        expectWeights(result.value());
        RoadTerrainWorkspace single;
        REQUIRE(single.initialize({tile()}).ok());
        REQUIRE(single.applyBatch(std::span<const RoadTerrainWrite>(writes.data(), 1), 100000).ok());
        auto one = single.copyTile("batch");
        REQUIRE(one.ok());
        REQUIRE(std::fabs(one.value().splatLayers[1].height(3, 5) - .4f) < 1e-6f);
        REQUIRE(one.value().splatLayers[2].height(7, 5) == 0.f);
    };
    SUB_CASE("one undo reverses the whole batch; persisted history can redo it") {
        RoadTerrainWorkspace workspace;
        auto*                workspaceView = &workspace;
        REQUIRE(workspaceView->initialize({tile()}).ok());
        REQUIRE(workspaceView->applyBatch(writes, 100000).ok());
        auto result = workspaceView->copyTile("batch");
        REQUIRE(result.ok());
        expectWeights(result.value());
        REQUIRE(workspaceView->undo().ok());
        auto snapshot = workspaceView->capture();
        REQUIRE(snapshot.ok());
        auto encoded = snapshot.value().toJson();
        REQUIRE(encoded.ok());
        auto decoded = eve::Value::fromJson(encoded.value());
        REQUIRE(decoded.ok());
        RoadTerrainWorkspace restored;
        REQUIRE(restored.restore(decoded.value()).ok());
        auto undone = restored.copyTile("batch");
        REQUIRE(undone.ok());
        REQUIRE(undone.value().splatLayers[1].height(3, 5) == 0.f);
        REQUIRE(undone.value().splatLayers[2].height(7, 5) == 0.f);
        REQUIRE(!restored.undo().ok());
        REQUIRE(restored.redo().ok());
        auto redone = restored.copyTile("batch");
        REQUIRE(redone.ok());
        expectWeights(redone.value());
    };
    SUB_CASE("late invalid policy and work limits leave tile and redo history unchanged") {
        RoadTerrainWorkspace workspace;
        auto*                workspaceView = &workspace;
        REQUIRE(workspaceView->initialize({tile()}).ok());
        REQUIRE(workspaceView->applyBatch(writes, 100000).ok());
        REQUIRE(workspaceView->undo().ok());
        auto before = workspaceView->capture();
        REQUIRE(before.ok());
        writes[1].options.splatLayer = 3;
        REQUIRE(!workspaceView->applyBatch(writes, 100000).ok());
        writes[1].options.splatLayer  = 2;
        writes[1].options.maximumWork = 1;
        REQUIRE(!workspaceView->applyBatch(writes, 100000).ok());
        writes[1].options.maximumWork = 100000;
        REQUIRE(!workspaceView->applyBatch(writes, 0).ok());
        REQUIRE(!workspaceView->applyBatch({}, 100000).ok());
        auto after = workspaceView->capture();
        REQUIRE(after.ok());
        REQUIRE(after.value() == before.value());
        REQUIRE(workspaceView->redo().ok());
    };
}

TEST_CASE("procgen.road.terrain.spec.nativeWorldPublication") {
    TerrainWorldCreationSettings settings;
    settings.tileSize                 = 10;
    settings.tileHeight               = 10;
    settings.originY                  = 100;
    settings.heightmapResolution      = 17;
    settings.controlTextureResolution = settings.detailResolution = 16;
    settings.treeResolution = settings.objectResolution = 4;
    settings.splatLayers                                = 3;
    TerrainWorldWorkspace world;
    REQUIRE(world.create(settings).ok());
    REQUIRE(world.setHeightWorldUnits(105, 10).ok());
    auto left   = asphalt(-2.f);
    auto right  = asphalt(2.f);
    auto writes = writesFor(left, right);
    auto before = world.getOperationCount();
    REQUIRE(world.conformRoadBatch(writes, 100000).ok());
    REQUIRE(world.getOperationCount() == before + 1);
    TerrainSplatmap splat;
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(std::fabs(splat.sample(1, 5, 8).value() - .4f) < 1e-6f);
    REQUIRE(std::fabs(splat.sample(2, 10, 8).value() - .8f) < 1e-6f);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(splat.sample(1, 5, 8).value() == 0.f);
    REQUIRE(splat.sample(2, 10, 8).value() == 0.f);
    before                       = world.getOperationCount();
    writes[1].options.splatLayer = 4;
    REQUIRE(!world.conformRoadBatch(writes, 100000).ok());
    REQUIRE(world.getOperationCount() == before);
    REQUIRE(world.redo().ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(std::fabs(splat.sample(1, 5, 8).value() - .4f) < 1e-6f);
}
