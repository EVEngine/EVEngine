#include <cmath>
#include <limits>
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadTerrainWorkspace.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;
using namespace eve::procgen::road;
namespace {
RoadTerrainTile opacityTile() {
    RoadTerrainTile tile;
    tile.id      = "opacity";
    tile.originX = tile.originZ = -2.f;
    tile.originY                = 100.f;
    tile.sizeX = tile.sizeZ = 4.f;
    tile.heightScale        = 10.f;
    tile.heights            = Heightmap(5, 5);
    for (auto& v : tile.heights.data()) v = .5f;
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
MeshBuild opacityMesh() {
    MeshBuild mesh;
    mesh.setActiveGroup("asphalt");
    for (const auto& p : {RoadControlPoint{-1.f, 106.f, -1.f}, RoadControlPoint{1.f, 106.f, -1.f},
                          RoadControlPoint{1.f, 106.f, 1.f}, RoadControlPoint{-1.f, 106.f, 1.f}})
        mesh.addVertex(p.x, p.y, p.z, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    return mesh;
}
}  // namespace

TEST_CASE("procgen.road.terrain.opacityPreservesNormalizationAndHistory") {
    RoadTerrainWorkspace owner;
    REQUIRE(owner.initialize({opacityTile()}).ok());
    RoadTerrainWriteOptions options;
    options.blendDistance = 0.f;
    options.splatLayer    = 1;
    options.splatOpacity  = .5f;
    REQUIRE(owner.apply(opacityMesh(), options).ok());
    auto tile = owner.copyTile("opacity");
    REQUIRE(tile.ok());
    REQUIRE(std::fabs(tile.value().splatLayers[0].height(2, 2) - (.2f / .7f * .5f)) < 1e-6f);
    REQUIRE(std::fabs(tile.value().splatLayers[1].height(2, 2) - .5f) < 1e-6f);
    REQUIRE(std::fabs(tile.value().splatLayers[2].height(2, 2) - (.5f / .7f * .5f)) < 1e-6f);
    for (std::size_t i = 0; i < 25; ++i) {
        float sum = 0.f;
        for (const auto& layer : tile.value().splatLayers) sum += layer.data()[i];
        REQUIRE(std::fabs(sum - 1.f) < 1e-6f);
    }
    REQUIRE_EQ(tile.value().splatLayers[1].height(0, 0), .3f);
    REQUIRE(std::fabs(tile.value().heights.height(2, 2) - .6f) < 1e-6f);
    REQUIRE(tile.value().trees.empty());
    REQUIRE_EQ(tile.value().details[0].density[12], 0);
    REQUIRE(owner.undo().ok());
    auto saved = owner.capture();
    REQUIRE(saved.ok());
    auto json = saved.value().toJson();
    REQUIRE(json.ok());
    auto parsed = eve::Value::fromJson(json.value());
    REQUIRE(parsed.ok());
    RoadTerrainWorkspace loaded;
    REQUIRE(loaded.restore(parsed.value()).ok());
    auto before = loaded.copyTile("opacity");
    REQUIRE(before.ok());
    REQUIRE_EQ(before.value().splatLayers[1].height(2, 2), .3f);
    REQUIRE_EQ(before.value().trees.size(), 1u);
    REQUIRE_EQ(before.value().trees[0].y, 105.f);
    REQUIRE(loaded.redo().ok());
    auto after = loaded.copyTile("opacity");
    REQUIRE(after.ok());
    REQUIRE_EQ(after.value().splatLayers[1].data(), tile.value().splatLayers[1].data());
    options.splatOpacity = .2f;
    REQUIRE(loaded.apply(opacityMesh(), options).ok());
    auto weaker = loaded.copyTile("opacity");
    REQUIRE(weaker.ok());
    for (std::size_t l = 0; l < 3; ++l)
        REQUIRE_EQ(weaker.value().splatLayers[l].data(), after.value().splatLayers[l].data());
}

TEST_CASE("procgen.road.terrain.opacityZeroAndInvalidAreIndependentAndAtomic") {
    RoadTerrainWorkspace owner;
    REQUIRE(owner.initialize({opacityTile()}).ok());
    RoadTerrainWriteOptions options;
    options.blendDistance = 0.f;
    options.splatLayer    = 1;
    options.splatOpacity  = 0.f;
    REQUIRE(owner.apply(opacityMesh(), options).ok());
    auto tile = owner.copyTile("opacity");
    REQUIRE(tile.ok());
    for (std::size_t layer = 0; layer < 3; ++layer)
        REQUIRE_EQ(tile.value().splatLayers[layer].data(), opacityTile().splatLayers[layer].data());
    REQUIRE(tile.value().trees.empty());
    REQUIRE_EQ(tile.value().details[0].density[12], 0);
    REQUIRE(std::fabs(tile.value().heights.height(2, 2) - .6f) < 1e-6f);
    REQUIRE(owner.undo().ok());
    auto saved = owner.capture();
    REQUIRE(saved.ok());
    auto originalJson = saved.value().toJson();
    REQUIRE(originalJson.ok());
    for (float invalid :
         {-1.f, 1.01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        options.splatOpacity = invalid;
        auto applied         = owner.apply(opacityMesh(), options);
        REQUIRE(!applied.ok());
        REQUIRE_EQ(applied.status().diagnostics().front().code(), eve::DiagnosticCode::InvalidArgument);
        auto state = owner.capture();
        REQUIRE(state.ok());
        auto json = state.value().toJson();
        REQUIRE(json.ok());
        REQUIRE_EQ(json.value(), originalJson.value());
    }
    REQUIRE(owner.redo().ok());
    options.splatOpacity = 1.f;
    REQUIRE(owner.apply(opacityMesh(), options).ok());
    auto full = owner.copyTile("opacity");
    REQUIRE(full.ok());
    REQUIRE_EQ(full.value().splatLayers[1].height(2, 2), 1.f);
}

TEST_CASE("procgen.road.terrain.opacityNativeWorldConsumer") {
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
    RoadTerrainWriteOptions options;
    options.blendDistance = 0.f;
    options.splatLayer    = 1;
    options.splatOpacity  = .5f;
    REQUIRE(world.conformRoad(opacityMesh(), options).ok());
    TerrainSplatmap splat;
    REQUIRE(world.copySplatmap(0, splat).ok());
    Heightmap layer(4, 4);
    REQUIRE(splat.copyLayer(1, layer).ok());
    REQUIRE(std::fabs(layer.height(2, 2) - .5f) < 1e-6f);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(splat.copyLayer(1, layer).ok());
    REQUIRE_EQ(layer.height(2, 2), 0.f);
    REQUIRE(world.redo().ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(splat.copyLayer(1, layer).ok());
    REQUIRE(std::fabs(layer.height(2, 2) - .5f) < 1e-6f);
    options.splatOpacity = -1.f;
    REQUIRE(!world.conformRoad(opacityMesh(), options).ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(splat.copyLayer(1, layer).ok());
    REQUIRE(std::fabs(layer.height(2, 2) - .5f) < 1e-6f);
}
