// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "modules": [
    "procgen",
    "procgen_editing"
  ],
  "origins": [
    "procgen_road_terrain_author_policy.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include <cmath>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadTerrainPlan.h"
#include "procgen/road/RoadTerrainWorkspace.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;
namespace {
eve::Result<RoadNetwork> parallelRoads() {
    RoadNetwork network;
    for (int i = 0; i < 2; ++i) {
        float z = i == 0 ? -8.f : 8.f;
        auto  a = network.addNode(-10, 1, z, 1), b = network.addNode(10, 1, z, 1);
        if (!a.ok()) return eve::Result<RoadNetwork>::failure(a.status());
        if (!b.ok()) return eve::Result<RoadNetwork>::failure(b.status());
        RoadStyle style;
        style.laneWidth           = 2;
        style.sidewalkWidth       = 0;
        style.curbWidth           = 0;
        style.automaticStructures = false;
        style.sideObjectsLeft = style.sideObjectsRight = false;
        style.terrainPolicy.blendDistance              = 0;
        style.terrainPolicy.splatLayer                 = i + 1;
        style.terrainPolicy.splatOpacity               = i == 0 ? .4f : .8f;
        auto edge = network.addEdge(a.value(), b.value(), {{-10, 1, z}, {10, 1, z}}, 1, 0, style);
        if (!edge.ok()) return eve::Result<RoadNetwork>::failure(edge.status());
    }
    return eve::Result<RoadNetwork>::success(std::move(network));
}
RoadTerrainTile policyTile() {
    RoadTerrainTile tile;
    tile.id      = "policy";
    tile.originX = tile.originZ = -20;
    tile.sizeX = tile.sizeZ = 40;
    tile.heightScale        = 10;
    tile.heights            = Heightmap(41, 41);
    tile.holes              = Heightmap(41, 41);
    tile.splatLayers        = {Heightmap(41, 41), Heightmap(41, 41), Heightmap(41, 41)};
    for (auto& value : tile.splatLayers[0].data()) value = 1;
    return tile;
}
int stripPolicies(eve::Value& value) {
    int n = 0;
    if (auto* object = value.getIf<eve::Value::Object>()) {
        n += static_cast<int>(object->erase("terrainPolicy"));
        for (auto& [key, child] : *object) n += stripPolicies(child);
    } else if (auto* array = value.getIf<eve::Value::Array>())
        for (auto& child : *array) n += stripPolicies(child);
    return n;
}
}  // namespace
TEST_CASE("procgen.road.terrain.authorPolicyNetworkSaveConsumesOneAtomicPlan") {
    auto network = parallelRoads();
    REQUIRE(network.ok());
    auto encoded = captureRoadNetwork(network.value());
    REQUIRE(encoded.ok());
    auto json = encoded.value().toJson();
    REQUIRE(json.ok());
    auto decoded = eve::Value::fromJson(json.value());
    REQUIRE(decoded.ok());
    auto restored = restoreRoadNetwork(decoded.value());
    REQUIRE(restored.ok());
    REQUIRE_EQ(restored.value().edges()[1].style.terrainPolicy.splatLayer, 2);
    RoadTerrainPolicy shared;
    shared.enabled = false;
    auto plan      = RoadTerrainPlan::prepare(restored.value(), RoadBakeOptions{}, shared, 1000000);
    REQUIRE(plan.ok());
    // A prepared plan owns the original policy and geometry, independent of later author edits/destruction.
    auto style                     = restored.value().edges()[0].style;
    style.terrainPolicy.splatLayer = 2;
    REQUIRE(restored.value().setEdgeStyle(1, style).ok());
    RoadTerrainWorkspace workspace;
    REQUIRE(workspace.initialize({policyTile()}).ok());
    REQUIRE(plan.value().apply(workspace, 1000000).ok());
    auto tile = workspace.copyTile("policy");
    REQUIRE(tile.ok());
    REQUIRE(std::abs(tile.value().splatLayers[1].height(20, 12) - .4f) < 1e-6f);
    REQUIRE(std::abs(tile.value().splatLayers[2].height(20, 28) - .8f) < 1e-6f);
    REQUIRE_EQ(tile.value().splatLayers[2].height(20, 12), 0.f);
    REQUIRE_EQ(tile.value().splatLayers[1].height(20, 28), 0.f);
    REQUIRE(workspace.undo().ok());
    REQUIRE(!workspace.undo().ok());
    auto history = workspace.capture();
    REQUIRE(history.ok());
    RoadTerrainWorkspace loaded;
    REQUIRE(loaded.restore(history.value()).ok());
    REQUIRE(loaded.redo().ok());
    auto redo = loaded.copyTile("policy");
    REQUIRE(redo.ok());
    REQUIRE(redo.value().splatLayers[1].data() == tile.value().splatLayers[1].data());
    REQUIRE(redo.value().splatLayers[2].data() == tile.value().splatLayers[2].data());
    REQUIRE(!plan.value().apply(loaded, 1).ok());
    auto after = loaded.copyTile("policy");
    REQUIRE(after.ok());
    REQUIRE(after.value().heights.data() == redo.value().heights.data());
    auto tiny = RoadTerrainPlan::prepare(network.value(), RoadBakeOptions{}, shared, 1);
    REQUIRE(!tiny.ok());
    REQUIRE(stripPolicies(decoded.value()) == 2);
    auto legacy = restoreRoadNetwork(decoded.value());
    REQUIRE(legacy.ok());
    REQUIRE_EQ(legacy.value().edges()[0].style.terrainPolicy.splatLayer, -1);
    auto badStyle                     = network.value().edges()[1].style;
    badStyle.terrainPolicy.splatLayer = 99;
    REQUIRE(network.value().setEdgeStyle(2, badStyle).ok());
    auto invalidLayer = RoadTerrainPlan::prepare(network.value(), RoadBakeOptions{}, shared, 1000000);
    REQUIRE(invalidLayer.ok());
    REQUIRE(loaded.undo().ok());
    REQUIRE(!invalidLayer.value().apply(loaded, 1000000).ok());
    REQUIRE(loaded.redo().ok());
    auto preserved = loaded.copyTile("policy");
    REQUIRE(preserved.ok());
    REQUIRE(preserved.value().splatLayers[1].data() == tile.value().splatLayers[1].data());
    REQUIRE(preserved.value().splatLayers[2].data() == tile.value().splatLayers[2].data());
}
TEST_CASE("procgen.road.terrain.authorPolicyPresetInheritanceHistoryAndValidation") {
    using namespace eve::procgen_editing;
    auto source = parallelRoads();
    REQUIRE(source.ok());
    RoadPresetDraft draft;
    draft.source = std::move(source).takeValue();
    RoadStyleOverrides root;
    RoadTerrainPolicy  p;
    p.splatLayer       = 1;
    p.splatOpacity     = .25f;
    root.terrainPolicy = p;
    RoadStyleOverrides child;
    p.splatLayer        = 2;
    child.terrainPolicy = p;
    draft.types         = {{"root", "", root}, {"child", "root", child}};
    draft.bindings      = {{1, "root", {}}, {2, "child", {}}};
    auto created        = RoadPresetDocument::create(draft);
    REQUIRE(created.ok());
    auto document = std::move(created).takeValue();
    REQUIRE_EQ(document->network().edges()[0].style.terrainPolicy.splatLayer, 1);
    REQUIRE_EQ(document->network().edges()[1].style.terrainPolicy.splatLayer, 2);
    auto edit = document->beginEdit();
    for (auto& type : edit.types)
        if (type.id == "child") type.overrides.terrainPolicy.reset();
    auto prepared = document->prepare(edit);
    REQUIRE(prepared.ok());
    REQUIRE(document->commit(std::move(prepared).takeValue()).ok());
    REQUIRE_EQ(document->network().edges()[1].style.terrainPolicy.splatLayer, 1);
    REQUIRE(document->undo().ok());
    auto state = document->capture();
    REQUIRE(state.ok());
    auto json = state.value().toJson();
    REQUIRE(json.ok());
    auto value = eve::Value::fromJson(json.value());
    REQUIRE(value.ok());
    auto restore = document->prepareRestore(value.value());
    REQUIRE(restore.ok());
    REQUIRE(document->commit(std::move(restore).takeValue()).ok());
    REQUIRE_EQ(document->network().edges()[1].style.terrainPolicy.splatLayer, 2);
    REQUIRE(document->redo().ok());
    REQUIRE_EQ(document->network().edges()[1].style.terrainPolicy.splatLayer, 1);
    auto bad = document->beginEdit();
    for (auto& type : bad.types)
        if (type.id == "root") type.overrides.terrainPolicy->splatOpacity = 2;
    REQUIRE(!document->prepare(bad).ok());
    REQUIRE_EQ(document->network().edges()[0].style.terrainPolicy.splatOpacity, .25f);
}
TEST_CASE("procgen.road.terrain.authorPolicyDisabledPlanPreservesHistory") {
    auto network = parallelRoads();
    REQUIRE(network.ok());
    for (const auto& original : std::vector<RoadEdge>(network.value().edges())) {
        auto style                  = original.style;
        style.terrainPolicy.enabled = false;
        REQUIRE(network.value().setEdgeStyle(original.id, style).ok());
    }
    auto saved = captureRoadNetwork(network.value());
    REQUIRE(saved.ok());
    auto serialized = saved.value().toJson();
    REQUIRE(serialized.ok());
    auto parsed = eve::Value::fromJson(serialized.value());
    REQUIRE(parsed.ok());
    auto restored = restoreRoadNetwork(parsed.value());
    REQUIRE(restored.ok());
    REQUIRE(!restored.value().edges()[0].style.terrainPolicy.enabled);
    RoadTerrainPolicy shared;
    shared.enabled = false;
    auto plan      = RoadTerrainPlan::prepare(restored.value(), RoadBakeOptions{}, shared, 1);
    REQUIRE(plan.ok());
    RoadTerrainWorkspace w;
    REQUIRE(w.initialize({policyTile()}).ok());
    auto result = plan.value().apply(w, 100);
    REQUIRE(result.ok());
    REQUIRE(result.code() == eve::StatusCode::NoOp);
    REQUIRE_EQ(result.value(), 0);
    REQUIRE(!w.undo().ok());
    REQUIRE(!plan.value().apply(w, 0).ok());
    shared.splatOpacity = 2;
    REQUIRE(!RoadTerrainPlan::prepare(network.value(), RoadBakeOptions{}, shared, 100).ok());
}

TEST_CASE("procgen.road.terrain.authorPolicySharedJunctionUsesExplicitWorldPolicy") {
    auto network = RoadNetwork::makeCross(36, 1);
    REQUIRE(network.ok());
    for (const auto& edge : std::vector<RoadEdge>(network.value().edges())) {
        auto style                  = edge.style;
        style.terrainPolicy.enabled = false;
        REQUIRE(network.value().setEdgeStyle(edge.id, style).ok());
    }
    RoadTerrainPolicy shared;
    shared.blendDistance = 0;
    shared.splatLayer    = 1;
    shared.splatOpacity  = .6f;
    auto plan            = RoadTerrainPlan::prepare(network.value(), RoadBakeOptions{}, shared, 1000000);
    REQUIRE(plan.ok());
    TerrainWorldCreationSettings settings;
    settings.tileSize                 = 40;
    settings.tileHeight               = 10;
    settings.heightmapResolution      = 33;
    settings.controlTextureResolution = 32;
    settings.detailResolution         = 16;
    settings.treeResolution = settings.objectResolution = 8;
    settings.splatLayers                                = 2;
    TerrainWorldWorkspace world;
    REQUIRE(!plan.value().applyWorld(world, 1000000).ok());
    REQUIRE(world.create(settings).ok());
    REQUIRE(plan.value().applyWorld(world, 1000000).ok());
    TerrainSplatmap splat;
    REQUIRE(world.copySplatmap(0, splat).ok());
    auto center = splat.sample(1, 16, 16), edge = splat.sample(1, 24, 16);
    REQUIRE(center.ok());
    REQUIRE(edge.ok());
    REQUIRE(std::abs(center.value() - .6f) < 1e-6f);
    REQUIRE_EQ(edge.value(), 0.f);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    auto before = splat.sample(1, 16, 16);
    REQUIRE(before.ok());
    REQUIRE_EQ(before.value(), 0.f);
    REQUIRE(world.redo().ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    auto after = splat.sample(1, 16, 16);
    REQUIRE(after.ok());
    REQUIRE(std::abs(after.value() - .6f) < 1e-6f);
}

TEST_CASE("procgen.road.terrain.authorPolicyMatchesReferenceDisjointSelectionFixture") {
    for (int selection = 0; selection < 3; ++selection) {
        RoadNetwork network;
        for (int i = 0; i < 2; ++i) {
            const float z = i == 0 ? -12.f : 12.f;
            auto        a = network.addNode(-24, 106, z, 1), b = network.addNode(24, 106, z, 1);
            REQUIRE(a.ok());
            REQUIRE(b.ok());
            RoadStyle style;
            style.laneWidth     = 6;
            style.sidewalkWidth = style.curbWidth = 0;
            style.automaticStructures             = false;
            style.sideObjectsLeft = style.sideObjectsRight = false;
            auto& policy                                   = style.terrainPolicy;
            policy.blendDistance                           = 0;
            policy.splatLayer                              = i + 1;
            policy.splatOpacity                            = i == 0 ? .4f : .8f;
            policy.enabled                                 = selection == 0 || selection == i + 1;
            auto edge = network.addEdge(a.value(), b.value(), {{-24, 106, z}, {0, 106, z}, {24, 106, z}}, 1, 0, style);
            REQUIRE(edge.ok());
        }
        RoadTerrainTile tile;
        tile.id      = "reference-policy";
        tile.originX = tile.originZ = -32;
        tile.originY                = 100;
        tile.sizeX = tile.sizeZ = 64;
        tile.heightScale        = 10;
        tile.heights            = Heightmap(65, 65);
        tile.holes              = Heightmap(65, 65);
        for (auto& v : tile.heights.data()) v = .5f;
        tile.splatLayers = {Heightmap(64, 64), Heightmap(64, 64), Heightmap(64, 64)};
        for (auto& v : tile.splatLayers[0].data()) v = 1;
        RoadTerrainPolicy shared;
        shared.enabled = false;
        auto plan      = RoadTerrainPlan::prepare(network, RoadBakeOptions{}, shared, 1000000);
        REQUIRE(plan.ok());
        RoadTerrainWorkspace w;
        REQUIRE(w.initialize({tile}).ok());
        REQUIRE(plan.value().apply(w, 1000000).ok());
        auto built = w.copyTile(tile.id);
        REQUIRE(built.ok());
        auto expectedFirst = selection == 2 ? 0.f : .4f, expectedSecond = selection == 1 ? 0.f : .8f;
        REQUIRE(std::abs(built.value().splatLayers[1].height(32, 20) - expectedFirst) < 1e-6f);
        REQUIRE(std::abs(built.value().splatLayers[2].height(32, 44) - expectedSecond) < 1e-6f);
        REQUIRE(std::abs(built.value().splatLayers[0].height(32, 20) - (1.f - expectedFirst)) < 1e-6f);
        REQUIRE(std::abs(built.value().splatLayers[0].height(32, 44) - (1.f - expectedSecond)) < 1e-6f);
        REQUIRE_EQ(built.value().splatLayers[2].height(32, 20), 0.f);
        REQUIRE_EQ(built.value().splatLayers[1].height(32, 44), 0.f);
        int changed = 0;
        for (float value : built.value().splatLayers[0].data())
            if (value < .9999f) ++changed;
        REQUIRE_EQ(changed, selection == 0 ? 576 : 288);
        REQUIRE(w.undo().ok());
        auto restored = w.copyTile(tile.id);
        REQUIRE(restored.ok());
        REQUIRE(restored.value().splatLayers[0].data() == tile.splatLayers[0].data());
        REQUIRE(restored.value().splatLayers[1].data() == tile.splatLayers[1].data());
        REQUIRE(restored.value().splatLayers[2].data() == tile.splatLayers[2].data());
    }
}
