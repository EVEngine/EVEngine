#include "editing/EditingAuthority.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/road/RoadTerrainWorkspace.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <set>

using namespace eve;
using namespace eve::procgen;
using namespace eve::procgen::road;
using namespace eve::procgen_editing;
TEST_CASE("procgen.road.authoring.rigidDistributionMatchesMeasuredReferenceAndBudget") {
    struct Sample {
        float                length, spacing;
        RoadSideDistribution mode;
        std::vector<float>   positions;
    };
    const std::vector<Sample> samples{
        {30, 5, RoadSideDistribution::Fixed, {0, 5, 10, 15, 20, 25}},
        {31, 5, RoadSideDistribution::Fixed, {0, 5, 10, 15, 20, 25, 30}},
        {31, 5, RoadSideDistribution::Average, {0, 5.171667f, 10.343330f, 15.515f, 20.686670f, 25.858330f, 31.03f}},
        {13, 5, RoadSideDistribution::Average, {0, 4.338333f, 8.676666f, 13.015f}},
        {12, 5, RoadSideDistribution::Average, {0, 6.005f, 12.01f}},
        {12.5f, 5, RoadSideDistribution::Average, {0, 6.255f, 12.51f}},
        {17.5f, 5, RoadSideDistribution::Average, {0, 4.38f, 8.76f, 13.14f, 17.52f}},
        {2, 5, RoadSideDistribution::Average, {0, 2.005f}},
        {31, 7, RoadSideDistribution::Average, {0, 7.755f, 15.51f, 23.265f, 31.02f}},
        {13, 2, RoadSideDistribution::Average, {0, 2.171667f, 4.343333f, 6.515f, 8.686666f, 10.858330f, 13.03f}},
        {30, 5, RoadSideDistribution::LegacyCentered, {2.5f, 7.5f, 12.5f, 17.5f, 22.5f, 27.5f}}};
    for (const auto& sample : samples) {
        RoadNetwork network;
        auto        a = network.addNode(0, 0, 0), b = network.addNode(0, 0, sample.length);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        RoadStyle style;
        style.automaticStructures    = false;
        style.sideObjectsRight       = false;
        style.sideObjectDistribution = sample.mode;
        auto edge = network.addEdge(a.value(), b.value(), {{0, 0, 0}, {0, 0, sample.length / 2}, {0, 0, sample.length}},
                                    1, 1, style);
        REQUIRE(edge.ok());
        RoadBakeOptions options;
        options.includeNavigation = options.includeMarkings = false;
        options.sideObjectSpacing                           = sample.spacing;
        options.sideObjectClearance                         = 0;
        options.maximumPlacements                           = static_cast<int>(sample.positions.size()) + 2;
        auto baked                                          = bakeRoadNetwork(network, options);
        REQUIRE(baked.ok());
        std::size_t index = 0;
        for (int i = 0; i < baked.value().placements.getCount(); ++i) {
            const auto& points = baked.value().placements;
            if (points.getStringAttribute(i, "road_role", "") != "side.left") continue;
            REQUIRE(index < sample.positions.size());
            if (sample.mode != RoadSideDistribution::LegacyCentered)
                REQUIRE(std::fabs(points.points()[i].z - sample.positions[index]) < 1e-4f);
            REQUIRE(std::fabs(points.getFloatAttribute(i, "road_distance", -1) - sample.positions[index]) < 1e-4f);
            ++index;
        }
        REQUIRE(index == sample.positions.size());
        Params recipeParams;
        recipeParams.setBool("navigation", false);
        recipeParams.setBool("markings", false);
        recipeParams.setFloat("sideObjectSpacing", sample.spacing);
        recipeParams.setFloat("sideObjectClearance", 0);
        REQUIRE(setRoadRecipeNetwork(recipeParams, network).ok());
        auto recipe = bakeRoadNetworkRecipe(recipeParams);
        REQUIRE(recipe.ok());
        REQUIRE(recipe.value().placements.getCount() == baked.value().placements.getCount());
        for (int i = 0; i < recipe.value().placements.getCount(); ++i) {
            REQUIRE(recipe.value().placements.points()[i].id == baked.value().placements.points()[i].id);
            REQUIRE(std::fabs(recipe.value().placements.points()[i].z - baked.value().placements.points()[i].z) <
                    1e-6f);
        }
        auto before = captureRoadNetwork(network);
        REQUIRE(before.ok());
        --options.maximumPlacements;
        REQUIRE(!bakeRoadNetwork(network, options).ok());
        REQUIRE(captureRoadNetwork(network).value() == before.value());
    }
}

TEST_CASE("procgen.road.authoring.rigidDistributionHonorsExclusionsAndEmptySpans") {
    RoadNetwork network;
    auto        a = network.addNode(0, 0, 0), b = network.addNode(0, 0, 31);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    RoadStyle style;
    style.automaticStructures   = false;
    style.sideObjectsRight      = false;
    style.sideObjectStartOffset = 2;
    style.sideObjectEndOffset   = 3;
    auto edge = network.addEdge(a.value(), b.value(), {{0, 0, 0}, {0, 0, 15.5f}, {0, 0, 31}}, 1, 1, style);
    REQUIRE(edge.ok());
    RoadBakeOptions options;
    options.includeNavigation = options.includeMarkings = false;
    options.sideObjectSpacing                           = 5;
    options.sideObjectClearance                         = 0;
    auto baked                                          = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    int count = 0;
    for (int i = 0; i < baked.value().placements.getCount(); ++i) {
        if (baked.value().placements.getStringAttribute(i, "road_role", "") != "side.left") continue;
        float distance = baked.value().placements.getFloatAttribute(i, "road_distance", -1);
        REQUIRE((distance >= 2 && distance <= 28));
        ++count;
    }
    REQUIRE(count == 5);
    style.sideObjectStartOffset = 20;
    style.sideObjectEndOffset   = 20;
    REQUIRE(network.setEdgeStyle(edge.value(), style).ok());
    auto empty = bakeRoadNetwork(network, options);
    REQUIRE(empty.ok());
    for (int i = 0; i < empty.value().placements.getCount(); ++i)
        REQUIRE(empty.value().placements.getStringAttribute(i, "road_role", "") != "side.left");
}

TEST_CASE("procgen.road.authoring.rigidDistributionPersistsPresetUndoAndLegacyMigration") {
    auto network = RoadNetwork::makeStraight(30, 2);
    REQUIRE(network.ok());
    auto snapshot = captureRoadNetwork(network.value());
    REQUIRE(snapshot.ok());
    auto  legacy = snapshot.value();
    auto* style  = legacy.getIf<eve::Value::Object>()
                      ->at("edges")
                      .getIf<eve::Value::Array>()
                      ->at(0)
                      .getIf<eve::Value::Object>()
                      ->at("style")
                      .getIf<eve::Value::Object>();
    style->erase("sideObjectDistribution");
    auto migrated = restoreRoadNetwork(legacy);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().edges()[0].style.sideObjectDistribution == RoadSideDistribution::LegacyCentered);
    style->emplace("sideObjectDistribution", eve::Value(std::int64_t{3}));
    REQUIRE(!restoreRoadNetwork(legacy).ok());
    RoadPresetDraft draft;
    draft.source = network.value();
    draft.types  = {{"root", "", {}}};
    for (const auto& edge : network.value().edges()) draft.bindings.push_back({edge.id, "root", {}});
    draft.types[0].overrides.sideObjectDistribution = RoadSideDistribution::Fixed;
    auto document                                   = RoadPresetDocument::create(draft);
    REQUIRE(document.ok());
    auto edit                                      = document.value()->beginEdit();
    edit.types[0].overrides.sideObjectDistribution = RoadSideDistribution::Average;
    auto prepared                                  = document.value()->prepare(std::move(edit));
    REQUIRE(prepared.ok());
    REQUIRE(document.value()->commit(std::move(prepared).takeValue()).ok());
    REQUIRE(document.value()->undo().ok());
    REQUIRE(document.value()->network().edges()[0].style.sideObjectDistribution == RoadSideDistribution::Fixed);
    auto saved = document.value()->capture();
    REQUIRE(saved.ok());
    auto restore = document.value()->prepareRestore(saved.value());
    REQUIRE(restore.ok());
    REQUIRE(document.value()->commit(std::move(restore).takeValue()).ok());
    REQUIRE(document.value()->redo().ok());
    REQUIRE(document.value()->network().edges()[0].style.sideObjectDistribution == RoadSideDistribution::Average);
}

namespace {
RoadStyle simpleStyle() {
    RoadStyle style;
    style.laneWidth           = 3.f;
    style.automaticStructures = false;
    style.crossSection        = {{-3.f, 0.f, RoadMaterial::Asphalt}, {3.f, 0.f, RoadMaterial::Asphalt}};
    return style;
}
RoadContinuousFacility markerPrefab() {
    RoadContinuousFacility f;
    f.profile                 = {{-.2f, 0.f, RoadMaterial::Curb},
                                 {.2f, 0.f, RoadMaterial::Curb},
                                 {.2f, 1.f, RoadMaterial::Curb},
                                 {-.2f, 1.f, RoadMaterial::Curb}};
    f.prefab.assetId          = "spacing:marker";
    f.prefab.length           = 2.f;
    f.prefab.automaticSpacing = true;
    f.prefab.vertices         = {{-.2f, 1.f, .2f}, {-.2f, 1.f, .4f}, {.2f, 1.f, .4f}};
    f.prefab.triangles        = {{0, 1, 2, RoadMaterial::Curb, 1}};
    return f;
}
RoadBakeOptions geometryOptions() {
    RoadBakeOptions options;
    options.includeNavigation = options.includeMarkings = options.includePlacements = false;
    return options;
}
std::set<int> markerPositions(const MeshBuild& mesh) {
    std::set<int> result;
    for (int t = 0; t < mesh.getIndexCount() / 3; ++t)
        if (mesh.getGroupName(mesh.getTriangleGroup(t)) == "curb")
            for (int k = 0; k < 3; ++k) {
                const int v = mesh.getIndex(t * 3 + k);
                result.insert(static_cast<int>(std::floor(mesh.getPositionX(v) / 2.f)));
            }
    return result;
}
void compensate(RoadNetworkEditTarget& target, const editing::DomainOperation& operation) {
    editing::LocalWorldAuthority authority(&target);
    editing::TransactionSpec     spec;
    spec.id           = editing::TransactionId("road.spacing-portals");
    spec.target       = target.targetId();
    spec.baseRevision = target.revision();
    auto plan         = authority.preflight(spec, std::span<const editing::DomainOperation>(&operation, 1));
    REQUIRE(plan.ok());
    auto receipt = authority.commit(plan.value());
    REQUIRE(receipt.ok());
    REQUIRE(authority.compensate(receipt.value()).ok());
}
RoadTerrainTile tile() {
    RoadTerrainTile result;
    result.id      = "portal:tile";
    result.originX = result.originZ = -20.f;
    result.sizeX = result.sizeZ = 40.f;
    result.heights              = Heightmap(41, 41);
    result.holes                = Heightmap(41, 41);
    for (auto& height : result.heights.data()) height = 5.f;
    result.details.push_back({"grass", 41, 41, std::vector<int>(1681u, 7)});
    result.trees = {{"entry", "oak", -11.f, 5.f, 0.f}, {"roof", "oak", 0.f, 5.f, 0.f}};
    return result;
}
}  // namespace

TEST_CASE("procgen.road.authoring.automaticPrefabSpacingRetainsPhaseOnSplitSaveAndUndo") {
    RoadNetwork network;
    auto        style = simpleStyle();
    style.facilities  = {markerPrefab()};
    auto a = network.addNode(0.f, 0.f, 0.f), b = network.addNode(20.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto edge =
        network.addEdge(a.value(), b.value(), {{0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}, {20.f, 0.f, 0.f}}, 1, 1, style);
    REQUIRE(edge.ok());
    auto before = bakeRoadNetwork(network, geometryOptions());
    REQUIRE(before.ok());
    REQUIRE(markerPositions(before.value().mesh).size() == 10u);
    RoadNetworkEditTarget target("road:automatic", network);
    const auto            saved   = target.snapshotValue();
    auto                  reverse = target.makeReverseEdge(edge.value());
    REQUIRE(reverse.ok());
    compensate(target, reverse.value());
    REQUIRE(target.snapshotValue() == saved);
    auto split = target.makeSplitEdge(edge.value(), 1, 1.f);
    REQUIRE(split.ok());
    compensate(target, split.value());
    REQUIRE(target.snapshotValue() == saved);
    REQUIRE(network.splitEdge(edge.value(), 1, 1.f).ok());
    REQUIRE(std::fabs(network.edges()[0].style.facilities[0].prefab.repeatSourceLength - 20.f) < 1e-4f);
    auto after = bakeRoadNetwork(network, geometryOptions());
    REQUIRE(after.ok());
    REQUIRE(markerPositions(after.value().mesh) == markerPositions(before.value().mesh));
    const auto            cutSaved = target.snapshotValue();
    RoadNetwork           restored;
    RoadNetworkEditTarget copy("road:automatic", restored);
    REQUIRE(copy.loadSnapshot(cutSaved).ok());
    REQUIRE(copy.snapshotValue() == cutSaved);
    auto reloaded = bakeRoadNetwork(restored, geometryOptions());
    REQUIRE(reloaded.ok());
    REQUIRE(reloaded.value().mesh.positions() == after.value().mesh.positions());
    REQUIRE(reloaded.value().mesh.normals() == after.value().mesh.normals());
    REQUIRE(restored.reverseEdge(edge.value()).ok());
    REQUIRE(bakeRoadNetwork(restored, geometryOptions()).ok());
    auto invalid                                    = style;
    invalid.facilities[0].prefab.repeatSourceLength = -1.f;
    REQUIRE(!network.setEdgeStyle(edge.value(), invalid).ok());
    REQUIRE(target.snapshotValue() == cutSaved);
    invalid                             = style;
    invalid.facilities[0].prefab.length = .001f;
    for (auto& v : invalid.facilities[0].prefab.vertices) v.z *= .0005f;
    REQUIRE(network.setEdgeStyle(edge.value(), invalid).ok());
    REQUIRE(!bakeRoadNetwork(network, geometryOptions()).ok());
}

TEST_CASE("procgen.road.authoring.smoothingGroupsCrossUvSeamsButPreserveHardGroups") {
    RoadNetwork network;
    auto        style         = simpleStyle();
    auto        f             = markerPrefab();
    f.prefab.automaticSpacing = false;
    // Two faces meet at an authored UV seam. Normals deliberately differ.
    f.prefab.vertices  = {{0.f, 1.f, 0.f, 0.f, 1.f, 0.f},  {0.f, 1.f, 2.f, 0.f, 1.f, 0.f},
                          {1.f, 1.f, 2.f, 0.f, 1.f, 0.f},  {0.f, 1.f, 0.f, -1.f, 0.f, 0.f, .7f, .7f},
                          {0.f, 0.f, 2.f, -1.f, 0.f, 0.f}, {0.f, 1.f, 2.f, -1.f, 0.f, 0.f, .9f, .9f}};
    f.prefab.triangles = {{0, 1, 2, RoadMaterial::Curb, 3}, {3, 4, 5, RoadMaterial::Curb, 3}};
    style.facilities   = {f};
    auto a = network.addNode(0.f, 0.f, 0.f), b = network.addNode(20.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto edge = network.addEdge(a.value(), b.value(), {{0.f, 0.f, 0.f}, {20.f, 0.f, 0.f}}, 1, 1, style);
    REQUIRE(edge.ok());
    auto smooth = bakeRoadNetwork(network, geometryOptions());
    REQUIRE(smooth.ok());
    int joined = 0;
    for (int i = 0; i < smooth.value().mesh.getVertexCount(); ++i) {
        const auto& m = smooth.value().mesh;
        if (std::fabs(m.getPositionX(i)) < 1e-4f && std::fabs(m.getPositionY(i) - 1.f) < 1e-4f &&
            std::fabs(m.getPositionZ(i)) < 1e-4f) {
            REQUIRE(std::fabs(m.getNormalY(i)) > .1f);
            REQUIRE(std::fabs(m.getNormalZ(i)) > .1f);
            ++joined;
        }
    }
    REQUIRE(joined >= 2);
    style.facilities[0].prefab.triangles[1].smoothingGroup = 4;
    REQUIRE(network.setEdgeStyle(edge.value(), style).ok());
    auto hard = bakeRoadNetwork(network, geometryOptions());
    REQUIRE(hard.ok());
    int separate = 0;
    for (int i = 0; i < hard.value().mesh.getVertexCount(); ++i) {
        const auto& m = hard.value().mesh;
        if (std::fabs(m.getPositionX(i)) < 1e-4f && std::fabs(m.getPositionY(i) - 1.f) < 1e-4f &&
            std::fabs(m.getPositionZ(i)) < 1e-4f) {
            REQUIRE((std::fabs(m.getNormalY(i)) < .01f || std::fabs(m.getNormalZ(i)) < .01f));
            ++separate;
        }
    }
    REQUIRE(separate >= 2);
    RoadNetworkEditTarget target("road:smooth", network);
    const auto            saved                              = target.snapshotValue();
    auto                  changed                            = style;
    changed.facilities[0].prefab.automaticSpacing            = false;
    changed.facilities[0].prefab.triangles[0].smoothingGroup = 0;
    auto styleOperation                                      = target.makeSetEdgeStyle(edge.value(), changed);
    REQUIRE(styleOperation.ok());
    compensate(target, styleOperation.value());
    REQUIRE(target.snapshotValue() == saved);
    RoadNetwork           restored;
    RoadNetworkEditTarget copy("road:smooth", restored);
    REQUIRE(copy.loadSnapshot(saved).ok());
    REQUIRE(restored.edges()[0].style.facilities[0].prefab.triangles[1].smoothingGroup == 4u);
    auto reverse = target.makeReverseEdge(edge.value());
    REQUIRE(reverse.ok());
    compensate(target, reverse.value());
    REQUIRE(target.snapshotValue() == saved);
}

TEST_CASE("procgen.road.authoring.exactPrefabSpacingClipsRemainderAndMigratesOptionalFields") {
    RoadNetwork network;
    auto        style       = simpleStyle();
    auto        f           = markerPrefab();
    f.prefab.averageSpacing = false;
    style.facilities        = {f};
    auto a = network.addNode(0.f, 0.f, 0.f), b = network.addNode(21.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto edge =
        network.addEdge(a.value(), b.value(), {{0.f, 0.f, 0.f}, {10.5f, 0.f, 0.f}, {21.f, 0.f, 0.f}}, 1, 1, style);
    REQUIRE(edge.ok());
    auto before = bakeRoadNetwork(network, geometryOptions());
    REQUIRE(before.ok());
    REQUIRE(markerPositions(before.value().mesh).size() == 11u);
    REQUIRE(network.reverseEdge(edge.value()).ok());
    auto reversed = bakeRoadNetwork(network, geometryOptions());
    REQUIRE(reversed.ok());
    REQUIRE(markerPositions(reversed.value().mesh) == markerPositions(before.value().mesh));
    REQUIRE(network.reverseEdge(edge.value()).ok());
    REQUIRE(network.splitEdge(edge.value(), 1, .5f).ok());
    auto cut = bakeRoadNetwork(network, geometryOptions());
    REQUIRE(cut.ok());
    REQUIRE(markerPositions(cut.value().mesh) == markerPositions(before.value().mesh));
    RoadNetworkEditTarget target("road:exact", network);
    const auto            saved = target.snapshotValue();
    RoadNetwork           restored;
    RoadNetworkEditTarget copy("road:exact", restored);
    REQUIRE(copy.loadSnapshot(saved).ok());
    REQUIRE(!restored.edges()[0].style.facilities[0].prefab.averageSpacing);
    auto  corrupt    = saved;
    auto& edges      = *corrupt.getIf<editing::Value::Object>()->at("edges").getIf<editing::Value::Array>();
    auto& facilities = *edges[0]
                            .getIf<editing::Value::Object>()
                            ->at("style")
                            .getIf<editing::Value::Object>()
                            ->at("facilities")
                            .getIf<editing::Value::Array>();
    auto& prefab = *facilities[0].getIf<editing::Value::Object>()->at("prefab").getIf<editing::Value::Object>();
    prefab.at("smoothingGroups") = editing::Value::Array{std::int64_t{65536}};
    REQUIRE(!copy.loadSnapshot(corrupt).ok());
    REQUIRE(copy.snapshotValue() == saved);
    prefab.erase("smoothingGroups");
    prefab.erase("automaticSpacing");
    prefab.erase("averageSpacing");
    prefab.erase("repeatSourceLength");
    prefab.erase("reverseRepeatSequence");
    REQUIRE(copy.loadSnapshot(corrupt).ok());
    REQUIRE(!restored.edges()[0].style.facilities[0].prefab.automaticSpacing);
    REQUIRE(restored.edges()[0].style.facilities[0].prefab.averageSpacing);
    REQUIRE(restored.edges()[0].style.facilities[0].prefab.triangles[0].smoothingGroup == 0u);
}

TEST_CASE("procgen.road.authoring.tunnelPortalHolesPreserveRoofAcrossSplitSaveUndoAndNativeWriteback") {
    RoadNetwork network;
    auto        style = simpleStyle();
    style.structures  = {{.2f, .8f, RoadStructureKind::Tunnel, 4.5f, .35f, 2.f, 3.f}};
    auto a = network.addNode(-20.f, 0.f, 0.f), b = network.addNode(20.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto edge =
        network.addEdge(a.value(), b.value(), {{-20.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, {20.f, 0.f, 0.f}}, 1, 1, style);
    REQUIRE(edge.ok());
    auto baked = bakeRoadNetwork(network, geometryOptions());
    REQUIRE(baked.ok());
    RoadTerrainWorkspace terrain;
    REQUIRE(terrain.initialize({tile()}).ok());
    RoadTerrainWriteOptions options;
    options.cutTunnelHoles = true;
    options.blendDistance  = 0.f;
    REQUIRE(terrain.apply(baked.value().mesh, options).ok());
    auto written = terrain.copyTile("portal:tile");
    REQUIRE(written.ok());
    auto hole = [](const RoadTerrainTile& t, int x) { return t.holes.height(x + 20, 20); };
    REQUIRE(hole(written.value(), -11) == 1.f);
    REQUIRE(hole(written.value(), 10) == 1.f);
    REQUIRE(hole(written.value(), 0) == 0.f);
    REQUIRE(hole(written.value(), -9) == 0.f);
    REQUIRE(written.value().heights.height(20, 20) == 5.f);
    REQUIRE(written.value().trees.size() == 1u);
    REQUIRE(written.value().trees[0].id == "roof");
    REQUIRE(written.value().details[0].density[20u * 41u + 20u] == 7);
    REQUIRE(terrain.undo().ok());
    auto savedTerrain = terrain.capture();
    REQUIRE(savedTerrain.ok());
    RoadTerrainWorkspace loaded;
    REQUIRE(loaded.restore(savedTerrain.value()).ok());
    REQUIRE(loaded.redo().ok());
    REQUIRE(loaded.copyTile("portal:tile").value().holes.data() == written.value().holes.data());
    RoadNetworkEditTarget target("road:portals", network);
    const auto            saved = target.snapshotValue();
    auto                  split = target.makeSplitEdge(edge.value(), 1, 1.f);
    REQUIRE(split.ok());
    compensate(target, split.value());
    REQUIRE(target.snapshotValue() == saved);
    auto reverse = target.makeReverseEdge(edge.value());
    REQUIRE(reverse.ok());
    compensate(target, reverse.value());
    REQUIRE(target.snapshotValue() == saved);
    REQUIRE(network.reverseEdge(edge.value()).ok());
    auto reversedMesh = bakeRoadNetwork(network, geometryOptions());
    REQUIRE(reversedMesh.ok());
    RoadTerrainWorkspace reversedTerrain;
    REQUIRE(reversedTerrain.initialize({tile()}).ok());
    REQUIRE(reversedTerrain.apply(reversedMesh.value().mesh, options).ok());
    REQUIRE(reversedTerrain.copyTile("portal:tile").value().holes.data() == written.value().holes.data());
    REQUIRE(network.reverseEdge(edge.value()).ok());
    REQUIRE(network.splitEdge(edge.value(), 1, 1.f).ok());
    REQUIRE(network.edges()[0].style.structures[0].portalEndLength == 0.f);
    REQUIRE(network.edges()[1].style.structures[0].portalStartLength == 0.f);
    auto cut = bakeRoadNetwork(network, geometryOptions());
    REQUIRE(cut.ok());
    RoadTerrainWorkspace cutTerrain;
    REQUIRE(cutTerrain.initialize({tile()}).ok());
    REQUIRE(cutTerrain.apply(cut.value().mesh, options).ok());
    auto cutTile = cutTerrain.copyTile("portal:tile");
    REQUIRE(cutTile.ok());
    bool sameHoles = true;
    for (int z = 0; z < 41; ++z)
        for (int x = 0; x < 41; ++x)
            if (cutTile.value().holes.height(x, z) != written.value().holes.height(x, z)) {
                std::cerr << "hole differs x=" << x - 20 << " z=" << z - 20
                          << " cut=" << cutTile.value().holes.height(x, z) << '\n';
                sameHoles = false;
            }
    REQUIRE(sameHoles);
    RoadNetwork           restored;
    RoadNetworkEditTarget copy("road:portals", restored);
    REQUIRE(copy.loadSnapshot(target.snapshotValue()).ok());
    REQUIRE(restored.edges()[1].style.structures[0].portalEndLength == 3.f);
    TerrainWorldWorkspace        world;
    TerrainWorldCreationSettings creation;
    creation.tileSize                 = 40.f;
    creation.tileHeight               = 10.f;
    creation.heightmapResolution      = 33;
    creation.controlTextureResolution = creation.detailResolution = creation.treeResolution =
        creation.objectResolution                                 = 16;
    REQUIRE(world.create(creation).ok());
    REQUIRE(world.conformRoad(baked.value().mesh, options).ok());
    auto nativeHoles = world.copyHoles(0);
    REQUIRE(nativeHoles.ok());
    REQUIRE(nativeHoles.value().height(7, 16) == 1.f);
    REQUIRE(nativeHoles.value().height(24, 16) == 1.f);
    REQUIRE(nativeHoles.value().height(16, 16) == 0.f);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyHoles(0).value().height(7, 16) == 0.f);
    const auto history    = world.getOperationCount();
    auto       exhausted  = options;
    exhausted.maximumWork = 1;
    REQUIRE(!world.conformRoad(baked.value().mesh, exhausted).ok());
    REQUIRE(world.getOperationCount() == history);
    REQUIRE(world.redo().ok());
    REQUIRE(world.copyHoles(0).value().data() == nativeHoles.value().data());
    options.tunnelHolesAtPortalsOnly = false;
    REQUIRE(loaded.apply(baked.value().mesh, options).ok());
    REQUIRE(hole(loaded.copyTile("portal:tile").value(), 0) == 1.f);
}
