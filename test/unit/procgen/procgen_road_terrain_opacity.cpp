#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadTerrainWorkspace.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
RoadTerrainTile initialTile() {
    RoadTerrainTile tile;
    tile.id      = "opacity";
    tile.originX = tile.originZ = -2.f;
    tile.originY                = 100.f;
    tile.sizeX = tile.sizeZ = 4.f;
    tile.heightScale        = 10.f;
    tile.heights            = Heightmap(5, 5);
    for (auto& value : tile.heights.data()) value = .5f;
    tile.holes       = Heightmap(5, 5);
    tile.splatLayers = {Heightmap(5, 5), Heightmap(5, 5), Heightmap(5, 5)};
    for (std::size_t i = 0; i < 25; ++i) {
        tile.splatLayers[0].data()[i] = .2f;
        tile.splatLayers[1].data()[i] = .3f;
        tile.splatLayers[2].data()[i] = .5f;
    }
    tile.details.push_back({"grass", 5, 5, std::vector<int>(25, 4)});
    tile.trees.push_back({"center", "oak", 0.f, 105.f, 0.f});
    return tile;
}

MeshBuild roadMesh() {
    MeshBuild mesh;
    mesh.setActiveGroup("asphalt");
    for (const auto& point : {RoadControlPoint{-1.f, 106.f, -1.f}, RoadControlPoint{1.f, 106.f, -1.f},
                              RoadControlPoint{1.f, 106.f, 1.f}, RoadControlPoint{-1.f, 106.f, 1.f}})
        mesh.addVertex(point.x, point.y, point.z, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    return mesh;
}

RoadTerrainWriteOptions paint(float opacity) {
    RoadTerrainWriteOptions options;
    options.blendDistance = 0.f;
    options.splatLayer    = 1;
    options.splatOpacity  = opacity;
    return options;
}

void near(float actual, float expected) { CHECK(std::fabs(actual - expected) < 1e-6f); }
}  // namespace

TEST_CASE("procgen.road.terrain.opacity_half_renormalizes_other_layers_at_covered_samples") {
    RoadTerrainWorkspace owner;
    REQUIRE(owner.initialize({initialTile()}).ok());
    REQUIRE(owner.apply(roadMesh(), paint(.5f)).ok());
    const auto result = owner.copyTile("opacity");
    REQUIRE(result.ok());
    const auto& tile = result.value();

    near(tile.splatLayers[0].height(2, 2), .2f / .7f * .5f);
    near(tile.splatLayers[1].height(2, 2), .5f);
    near(tile.splatLayers[2].height(2, 2), .5f / .7f * .5f);
    for (std::size_t i = 0; i < 25; ++i) {
        float total = 0.f;
        for (const auto& layer : tile.splatLayers) total += layer.data()[i];
        near(total, 1.f);
    }
    CHECK_EQ(tile.splatLayers[1].height(0, 0), .3f);
}

TEST_CASE("procgen.road.terrain.opacity_zero_preserves_splats_but_updates_other_channels") {
    RoadTerrainWorkspace owner;
    const auto           baseline = initialTile();
    REQUIRE(owner.initialize({baseline}).ok());
    REQUIRE(owner.apply(roadMesh(), paint(0.f)).ok());
    const auto result = owner.copyTile("opacity");
    REQUIRE(result.ok());
    for (std::size_t layer = 0; layer < baseline.splatLayers.size(); ++layer)
        CHECK_EQ(result.value().splatLayers[layer].data(), baseline.splatLayers[layer].data());
    near(result.value().heights.height(2, 2), .6f);
    CHECK(result.value().trees.empty());
    CHECK_EQ(result.value().details[0].density[12], 0);
}

TEST_CASE("procgen.road.terrain.opacity_never_lowers_previously_painted_weight") {
    RoadTerrainWorkspace owner;
    REQUIRE(owner.initialize({initialTile()}).ok());
    REQUIRE(owner.apply(roadMesh(), paint(.5f)).ok());
    const auto before = owner.copyTile("opacity");
    REQUIRE(before.ok());
    REQUIRE(owner.apply(roadMesh(), paint(.2f)).ok());
    const auto after = owner.copyTile("opacity");
    REQUIRE(after.ok());
    for (std::size_t layer = 0; layer < 3; ++layer)
        CHECK_EQ(after.value().splatLayers[layer].data(), before.value().splatLayers[layer].data());
    REQUIRE(owner.apply(roadMesh(), paint(1.f)).ok());
    const auto full = owner.copyTile("opacity");
    REQUIRE(full.ok());
    CHECK_EQ(full.value().splatLayers[1].height(2, 2), 1.f);
}

TEST_CASE("procgen.road.terrain.invalid_opacity_preserves_state_and_redo_branch") {
    RoadTerrainWorkspace owner;
    REQUIRE(owner.initialize({initialTile()}).ok());
    REQUIRE(owner.apply(roadMesh(), paint(.5f)).ok());
    REQUIRE(owner.undo().ok());
    const auto baseline = owner.capture();
    REQUIRE(baseline.ok());
    const auto baselineJson = baseline.value().toJson();
    REQUIRE(baselineJson.ok());

    for (float invalid :
         {-1.f, 1.01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        const auto failed = owner.apply(roadMesh(), paint(invalid));
        REQUIRE(!failed.ok());
        REQUIRE_EQ(failed.status().diagnostics().front().code(), eve::DiagnosticCode::InvalidArgument);
        const auto state = owner.capture();
        REQUIRE(state.ok());
        const auto json = state.value().toJson();
        REQUIRE(json.ok());
        CHECK_EQ(json.value(), baselineJson.value());
    }
    REQUIRE(owner.redo().ok());
    const auto redone = owner.copyTile("opacity");
    REQUIRE(redone.ok());
    near(redone.value().splatLayers[1].height(2, 2), .5f);
}

TEST_CASE("procgen.road.terrain.snapshot_restores_undo_and_redo_history") {
    RoadTerrainWorkspace owner;
    REQUIRE(owner.initialize({initialTile()}).ok());
    REQUIRE(owner.apply(roadMesh(), paint(.5f)).ok());
    REQUIRE(owner.undo().ok());
    const auto state = owner.capture();
    REQUIRE(state.ok());
    const auto json = state.value().toJson();
    REQUIRE(json.ok());
    const auto decoded = eve::Value::fromJson(json.value());
    REQUIRE(decoded.ok());
    RoadTerrainWorkspace restored;
    REQUIRE(restored.restore(decoded.value()).ok());
    const auto before = restored.copyTile("opacity");
    REQUIRE(before.ok());
    CHECK_EQ(before.value().splatLayers[1].height(2, 2), .3f);
    CHECK_EQ(before.value().trees.size(), 1U);
    REQUIRE(restored.redo().ok());
    const auto after = restored.copyTile("opacity");
    REQUIRE(after.ok());
    near(after.value().splatLayers[1].height(2, 2), .5f);
    CHECK(after.value().trees.empty());
}

TEST_CASE("procgen.road.terrain.native_world_consumer_uses_same_opacity_and_history") {
    TerrainWorldCreationSettings creation;
    creation.originY                  = 100.f;
    creation.tileSize                 = 4.f;
    creation.tileHeight               = 10.f;
    creation.heightmapResolution      = 5;
    creation.controlTextureResolution = creation.detailResolution = 4;
    creation.treeResolution = creation.objectResolution = 4;
    creation.splatLayers                                = 2;
    TerrainWorldWorkspace world;
    REQUIRE(world.create(creation).ok());
    REQUIRE(world.setHeightWorldUnits(105.f, 10.f).ok());
    REQUIRE(world.conformRoad(roadMesh(), paint(.5f)).ok());

    TerrainSplatmap splat;
    Heightmap layer(4, 4);
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(splat.copyLayer(1, layer).ok());
    near(layer.height(2, 2), .5f);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(splat.copyLayer(1, layer).ok());
    CHECK_EQ(layer.height(2, 2), 0.f);
    REQUIRE(world.redo().ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(splat.copyLayer(1, layer).ok());
    near(layer.height(2, 2), .5f);

    CHECK(!world.conformRoad(roadMesh(), paint(-1.f)).ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(splat.copyLayer(1, layer).ok());
    near(layer.height(2, 2), .5f);
}
