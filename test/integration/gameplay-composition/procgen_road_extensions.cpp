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
    "procgen_road_extensions.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include <iostream>
#include "editing/EditingAuthority.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadTerrainWorkspace.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve;
using namespace eve::procgen;
using namespace eve::procgen::road;
using namespace eve::procgen_editing;
namespace {
RoadStyle extensionStyle() {
    RoadStyle style;
    style.laneWidth           = 3.f;
    style.automaticStructures = false;
    style.crossSection        = {{-3.f, 0.f, RoadMaterial::Asphalt}, {3.f, 0.f, RoadMaterial::Asphalt}};
    return style;
}
RoadContinuousFacility extensionFacility() {
    RoadContinuousFacility f;
    f.id      = 7;
    f.profile = {{-.2f, 0.f, RoadMaterial::Curb},
                 {.2f, 0.f, RoadMaterial::Curb},
                 {.2f, 1.f, RoadMaterial::Curb},
                 {-.2f, 1.f, RoadMaterial::Curb}};
    return f;
}
void compensateOperation(RoadNetworkEditTarget& target, const editing::DomainOperation& op) {
    editing::LocalWorldAuthority authority(&target);
    editing::TransactionSpec     transaction;
    transaction.id           = editing::TransactionId("road.extensions");
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    auto plan                = authority.preflight(transaction, std::span<const editing::DomainOperation>(&op, 1));
    REQUIRE(plan.ok());
    auto receipt = authority.commit(plan.value());
    REQUIRE(receipt.ok());
    REQUIRE(authority.compensate(receipt.value()).ok());
}
RoadTerrainTile extensionTile() {
    RoadTerrainTile tile;
    tile.id      = "terrain:one";
    tile.originX = tile.originZ = -10.f;
    tile.sizeX = tile.sizeZ = 20.f;
    tile.heights            = Heightmap(21, 21);
    tile.holes              = Heightmap(21, 21);
    tile.splatLayers        = {Heightmap(11, 11), Heightmap(11, 11)};
    for (auto& v : tile.splatLayers[0].data()) v = 1.f;
    tile.details.push_back({"grass", 11, 11, std::vector<int>(121u, 8)});
    tile.trees = {{"tree:ground", "oak", -5.f, 0.f, 0.f},
                  {"tree:bridge", "oak", 1.f, 0.f, 0.f},
                  {"tree:roof", "oak", 6.f, 0.f, 0.f},
                  {"tree:free", "oak", 9.f, 0.f, 0.f}};
    return tile;
}
MeshBuild extensionTerrainMesh() {
    MeshBuild mesh;
    auto      rectangle = [&](float x0, float x1, const char* material) {
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        mesh.addVertex(x0, 1.f, -2.f, 0.f, 1.f, 0.f, 0.f, 0.f);
        mesh.addVertex(x0, 1.f, 2.f, 0.f, 1.f, 0.f, 0.f, 1.f);
        mesh.addVertex(x1, 1.f, 2.f, 0.f, 1.f, 0.f, 1.f, 1.f);
        mesh.addVertex(x1, 1.f, -2.f, 0.f, 1.f, 0.f, 1.f, 0.f);
        mesh.setActiveGroup(material);
        mesh.addTriangle(base, base + 1u, base + 2u);
        mesh.addTriangle(base, base + 2u, base + 3u);
    };
    rectangle(-8.f, -2.f, "asphalt");
    rectangle(-1.f, 3.f, "asphalt.bridge");
    rectangle(5.f, 8.f, "asphalt.tunnel");
    return mesh;
}
}  // namespace
TEST_CASE("procgen.road.authoring.prefabDeformsSourceTrianglesNormalsUvsAndPersistsCuts") {
    auto style                    = extensionStyle();
    auto facility                 = extensionFacility();
    facility.prefab.assetId       = "test.prefab.rail";
    facility.prefab.vertices      = {{-.2f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f},
                                     {-.2f, 1.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f},
                                     {.2f, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f, 1.f},
                                     {.2f, 1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f}};
    facility.prefab.triangles     = {{0, 1, 2, RoadMaterial::Curb}, {0, 2, 3, RoadMaterial::Curb}};
    facility.prefab.vertices[0].z = .1234567f;
    style.facilities              = {facility};
    RoadNetwork network;
    auto        a = network.addNode(-20.f, 0.f, 0.f), b = network.addNode(20.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto edge =
        network.addEdge(a.value(), b.value(), {{-20.f, 0.f, 0.f}, {0.f, 2.f, 4.f}, {20.f, 0.f, 0.f}}, 1, 1, style);
    REQUIRE(edge.ok());
    RoadBakeOptions options;
    options.includeNavigation = options.includeMarkings = options.includePlacements = false;
    auto baked                                                                      = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    int  count  = 0;
    bool curved = false, sloped = false;
    for (int t = 0; t < baked.value().mesh.getIndexCount() / 3; ++t) {
        const auto& mesh = baked.value().mesh;
        if (mesh.getGroupName(mesh.getTriangleGroup(t)) != "curb") continue;
        for (int k = 0; k < 3; ++k) {
            const int   v      = mesh.getIndex(t * 3 + k);
            const float normal = std::sqrt(std::pow(mesh.getNormalX(v), 2.f) + std::pow(mesh.getNormalY(v), 2.f) +
                                           std::pow(mesh.getNormalZ(v), 2.f));
            REQUIRE(std::fabs(normal - 1.f) < 1e-4f);
            REQUIRE((mesh.getUvU(v) >= 0.f && mesh.getUvU(v) <= 1.f));
            REQUIRE((mesh.getUvV(v) >= 0.f && mesh.getUvV(v) <= 1.f));
            curved = curved || std::fabs(mesh.getPositionZ(v)) > 2.f;
            sloped = sloped || std::fabs(mesh.getNormalX(v)) > .01f;
            ++count;
        }
    }
    REQUIRE(count > 100);
    REQUIRE(curved);
    REQUIRE(sloped);
    RoadNetworkEditTarget target("road:prefab", network);
    const auto            saved   = target.snapshotValue();
    auto                  reverse = target.makeReverseEdge(edge.value());
    REQUIRE(reverse.ok());
    compensateOperation(target, reverse.value());
    REQUIRE(target.snapshotValue() == saved);
    auto split = target.makeSplitEdge(edge.value(), 1, 2.f);
    REQUIRE(split.ok());
    compensateOperation(target, split.value());
    REQUIRE(target.snapshotValue() == saved);
    auto cut = network.splitEdge(edge.value(), 1, 2.f);
    REQUIRE(cut.ok());
    REQUIRE(std::fabs(network.edges()[0].style.facilities[0].prefab.sourceEnd - .5f) < 1e-4f);
    REQUIRE(std::fabs(network.edges()[1].style.facilities[0].prefab.sourceStart - .5f) < 1e-4f);
    REQUIRE(bakeRoadNetwork(network, options).ok());
    REQUIRE(target.loadSnapshot(saved).ok());
    RoadNetwork           other;
    RoadNetworkEditTarget restored("road:prefab", other);
    REQUIRE(restored.loadSnapshot(saved).ok());
    REQUIRE(restored.snapshotValue() == saved);
    auto invalid                                = style;
    invalid.facilities[0].prefab.triangles[0].a = 999;
    REQUIRE(!network.setEdgeStyle(edge.value(), invalid).ok());
    REQUIRE(target.snapshotValue() == saved);
    options.maximumMeshElements = 64u;
    REQUIRE(!bakeRoadNetwork(network, options).ok());
}
TEST_CASE("procgen.road.authoring.multiArmFacilityRoutesSaveUndoAndMigrateSplitIdentities") {
    RoadNetwork network;
    auto        style = extensionStyle();
    style.facilities  = {extensionFacility()};
    auto left = network.addNode(-20.f, 0.f, 0.f), hub = network.addNode(0.f, 0.f, 0.f),
         right = network.addNode(20.f, 0.f, 0.f), north = network.addNode(0.f, 0.f, 20.f);
    REQUIRE(left.ok());
    REQUIRE(hub.ok());
    REQUIRE(right.ok());
    REQUIRE(north.ok());
    auto first  = network.addEdge(left.value(), hub.value(), {{-20.f, 0.f, 0.f}, {-10.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}, 1,
                                  1, style);
    auto second = network.addEdge(hub.value(), right.value(), {{0.f, 0.f, 0.f}, {20.f, 0.f, 0.f}}, 1, 1, style);
    auto third  = network.addEdge(hub.value(), north.value(), {{0.f, 0.f, 0.f}, {0.f, 0.f, 20.f}}, 1, 1, style);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(third.ok());
    REQUIRE(network.connectAllTurns(hub.value()).ok());
    auto blocked = network.blockLaneLink(network.laneLinks().front());
    REQUIRE(blocked.ok());
    RoadNetworkEditTarget target("road:routes", network);
    auto                  before = target.snapshotValue();
    RoadConnection        connection;
    connection.facilityRoutes = {{first.value(), 7, second.value(), 7}};
    auto operation            = target.makeSetNodeConnection(hub.value(), connection);
    REQUIRE(operation.ok());
    compensateOperation(target, operation.value());
    REQUIRE(target.snapshotValue() == before);
    REQUIRE(network.setNodeConnection(hub.value(), connection).ok());
    RoadBakeOptions options;
    options.includeNavigation = options.includeMarkings = options.includePlacements = false;
    auto baked                                                                      = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    bool center = false;
    for (int v = 0; v < baked.value().mesh.getVertexCount(); ++v)
        if (std::fabs(baked.value().mesh.getPositionX(v)) < .001f &&
            std::fabs(baked.value().mesh.getPositionZ(v)) < .3f && baked.value().mesh.getPositionY(v) > .9f)
            center = true;
    REQUIRE(center);
    auto navigationOptions              = options;
    navigationOptions.includeNavigation = true;
    auto navigationBefore               = bakeRoadNetwork(network, navigationOptions);
    REQUIRE(navigationBefore.ok());
    auto saved          = target.snapshotValue();
    auto splitOperation = target.makeSplitEdge(first.value(), 1, 2.f);
    REQUIRE(splitOperation.ok());
    compensateOperation(target, splitOperation.value());
    REQUIRE(target.snapshotValue() == saved);
    auto navigationAfter = bakeRoadNetwork(network, navigationOptions);
    REQUIRE(navigationAfter.ok());
    REQUIRE(navigationAfter.value().mesh.positions() == navigationBefore.value().mesh.positions());
    REQUIRE(navigationAfter.value().mesh.indices() == navigationBefore.value().mesh.indices());
    REQUIRE(network.reverseEdge(first.value()).ok());
    REQUIRE(bakeRoadNetwork(network, options).ok());
    REQUIRE(target.loadSnapshot(saved).ok());
    auto split = network.splitEdge(first.value(), 1, 2.f);
    REQUIRE(split.ok());
    auto node = network.nodeResult(hub.value());
    REQUIRE(node.ok());
    REQUIRE(node.value().connection.facilityRoutes[0].firstEdge == split.value().secondEdgeId);
    REQUIRE(bakeRoadNetwork(network, options).ok());
    REQUIRE(target.loadSnapshot(saved).ok());
    auto bad = connection;
    bad.facilityRoutes.push_back({first.value(), 7, third.value(), 7});
    REQUIRE(!network.setNodeConnection(hub.value(), bad).ok());
    REQUIRE(target.snapshotValue() == saved);
    auto clipped              = style;
    clipped.facilities[0].end = .8f;
    REQUIRE(!network.setEdgeStyle(first.value(), clipped).ok());
    REQUIRE(!network.removeEdge(first.value()).ok());
    REQUIRE(target.snapshotValue() == saved);
    RoadNetwork           other;
    RoadNetworkEditTarget restored("road:routes", other);
    REQUIRE(restored.loadSnapshot(saved).ok());
    REQUIRE(restored.snapshotValue() == saved);
}
TEST_CASE("procgen.road.authoring.prefabStartMiddleEndRepeatsWeldsAndReversesWithoutLosingAssets") {
    auto style                   = extensionStyle();
    auto facility                = extensionFacility();
    facility.prefab.assetId      = "middle";
    facility.prefab.vertices     = {{-.2f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f},
                                    {-.2f, 1.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f},
                                    {.2f, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f, 1.f},
                                    {.2f, 1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f}};
    facility.prefab.triangles    = {{0, 1, 2, RoadMaterial::Curb}, {0, 2, 3, RoadMaterial::Curb}};
    facility.prefab.middleCopies = 3;
    facility.prefab.startMesh    = {"start", 1.f, facility.prefab.vertices, facility.prefab.triangles};
    facility.prefab.endMesh      = {"end", 1.f, facility.prefab.vertices, facility.prefab.triangles};
    for (auto& t : facility.prefab.startMesh.triangles) t.material = RoadMaterial::Deck;
    for (auto& t : facility.prefab.endMesh.triangles) t.material = RoadMaterial::Pier;
    style.facilities = {facility};
    RoadNetwork network;
    auto        a = network.addNode(-20.f, 0.f, 0.f), b = network.addNode(20.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto edge =
        network.addEdge(a.value(), b.value(), {{-20.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, {20.f, 0.f, 0.f}}, 1, 1, style);
    REQUIRE(edge.ok());
    RoadBakeOptions options;
    options.includeNavigation = options.includeMarkings = options.includePlacements = false;
    auto ordinary                                                                   = bakeRoadNetwork(network, options);
    if (!ordinary.ok()) std::cerr << ordinary.status().describe() << '\n';
    REQUIRE(ordinary.ok());
    facility.prefab.weldCompatibleVertices = true;
    style.facilities                       = {facility};
    REQUIRE(network.setEdgeStyle(edge.value(), style).ok());
    auto welded = bakeRoadNetwork(network, options);
    REQUIRE(welded.ok());
    REQUIRE(welded.value().mesh.getVertexCount() < ordinary.value().mesh.getVertexCount());
    int         start = 0, middle = 0, end = 0;
    const auto& mesh = welded.value().mesh;
    for (int t = 0; t < mesh.getIndexCount() / 3; ++t) {
        const auto material = mesh.getGroupName(mesh.getTriangleGroup(t));
        const int  v        = mesh.getIndex(t * 3);
        if (material == "deck") {
            REQUIRE(mesh.getPositionX(v) <= -11.9f);
            ++start;
        }
        if (material == "curb") {
            REQUIRE(mesh.getPositionX(v) >= -12.1f);
            REQUIRE(mesh.getPositionX(v) <= 12.1f);
            ++middle;
        }
        if (material == "pier") {
            REQUIRE(mesh.getPositionX(v) >= 11.9f);
            ++end;
        }
    }
    REQUIRE(start > 0);
    REQUIRE(middle > start);
    REQUIRE(end > 0);
    RoadNetworkEditTarget target("road:parts", network);
    auto                  saved = target.snapshotValue();
    REQUIRE(network.reverseEdge(edge.value()).ok());
    REQUIRE(network.edges()[0].style.facilities[0].prefab.startMesh.assetId == "end");
    REQUIRE(bakeRoadNetwork(network, options).ok());
    REQUIRE(network.reverseEdge(edge.value()).ok());
    REQUIRE(target.snapshotValue() == saved);
    RoadNetwork           other;
    RoadNetworkEditTarget restored("road:parts", other);
    REQUIRE(restored.loadSnapshot(saved).ok());
    REQUIRE(restored.snapshotValue() == saved);
    REQUIRE(other.edges()[0].style.facilities[0].prefab.middleCopies == 3);
    auto split = network.splitEdge(edge.value(), 1, 2.f);
    REQUIRE(split.ok());
    auto afterSplit = bakeRoadNetwork(network, options);
    REQUIRE(afterSplit.ok());
    for (int t = 0; t < afterSplit.value().mesh.getIndexCount() / 3; ++t) {
        const auto& m     = afterSplit.value().mesh;
        const auto  group = m.getGroupName(m.getTriangleGroup(t));
        if (group == "deck") REQUIRE(m.getPositionX(m.getIndex(t * 3)) <= -11.9f);
        if (group == "pier") REQUIRE(m.getPositionX(m.getIndex(t * 3)) >= 11.9f);
    }
    REQUIRE(std::fabs(network.edges()[0].style.facilities[0].prefab.sourceEnd - .5f) < 1e-4f);
}
TEST_CASE("procgen.road.authoring.prefabContinuationRejectsSameLabelWithDifferentOwnedGeometry") {
    auto style         = extensionStyle();
    auto f             = extensionFacility();
    f.prefab.assetId   = "test.prefab.route";
    f.prefab.vertices  = {{-.2f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f},
                          {-.2f, 1.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f},
                          {.2f, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f, 1.f},
                          {.2f, 1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f}};
    f.prefab.triangles = {{0, 1, 2, RoadMaterial::Curb}, {0, 2, 3, RoadMaterial::Curb}};
    style.facilities   = {f};
    RoadNetwork network;
    auto        a = network.addNode(-20.f, 0.f, 0.f), hub = network.addNode(0.f, 0.f, 0.f),
         b = network.addNode(20.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(hub.ok());
    REQUIRE(b.ok());
    auto first  = network.addEdge(a.value(), hub.value(), {{-20.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}, 1, 1, style);
    auto second = network.addEdge(hub.value(), b.value(), {{0.f, 0.f, 0.f}, {20.f, 0.f, 0.f}}, 1, 1, style);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    RoadConnection connection;
    connection.facilityRoutes = {{first.value(), 7, second.value(), 7}};
    REQUIRE(network.setNodeConnection(hub.value(), connection).ok());
    REQUIRE(bakeRoadNetwork(network).ok());
    RoadNetworkEditTarget target("road:prefab-route", network);
    const auto            saved                = target.snapshotValue();
    auto                  changed              = style;
    changed.facilities[0].prefab.vertices[0].y = 1.5f;
    REQUIRE(!network.setEdgeStyle(second.value(), changed).ok());
    REQUIRE(target.snapshotValue() == saved);
    REQUIRE(network.reverseEdge(first.value()).ok());
    REQUIRE(bakeRoadNetwork(network).ok());
}
TEST_CASE("procgen.road.authoring.terrainChannelsPersistUndoRedoAndProtectBridgeTunnelRoofs") {
    RoadTerrainWorkspace workspace;
    REQUIRE(workspace.initialize({extensionTile()}).ok());
    auto baseline = workspace.capture();
    REQUIRE(baseline.ok());
    RoadTerrainWriteOptions options;
    options.blendDistance = 0.f;
    options.splatLayer    = 1;
    auto mesh             = extensionTerrainMesh();
    auto applied          = workspace.apply(mesh, options);
    REQUIRE(applied.ok());
    REQUIRE(applied.value() > 0);
    auto tile = workspace.copyTile("terrain:one");
    REQUIRE(tile.ok());
    REQUIRE(tile.value().heights.height(5, 10) == 1.f);
    REQUIRE(tile.value().heights.height(11, 10) == 0.f);
    REQUIRE(tile.value().heights.height(16, 10) == 0.f);
    REQUIRE(tile.value().splatLayers[1].height(3, 5) == 1.f);
    REQUIRE(tile.value().splatLayers[1].height(6, 5) == 0.f);
    REQUIRE(tile.value().details[0].density[5u * 11u + 3u] == 0);
    REQUIRE(tile.value().details[0].density[5u * 11u + 6u] == 8);
    REQUIRE(tile.value().trees.size() == 3u);
    REQUIRE(tile.value().holes.height(16, 10) == 0.f);
    options.cutTunnelHoles           = true;
    options.tunnelHolesAtPortalsOnly = false;  // Explicit legacy whole-footprint contract.
    REQUIRE(workspace.apply(mesh, options).ok());
    tile = workspace.copyTile("terrain:one");
    REQUIRE(tile.ok());
    REQUIRE(tile.value().holes.height(16, 10) == 1.f);
    REQUIRE(tile.value().trees.size() == 2u);
    REQUIRE(workspace.undo().ok());
    auto saved = workspace.capture();
    REQUIRE(saved.ok());
    auto json = saved.value().toJson();
    REQUIRE(json.ok());
    auto decoded = eve::Value::fromJson(json.value());
    REQUIRE(decoded.ok());
    RoadTerrainWorkspace restored;
    REQUIRE(restored.restore(decoded.value()).ok());
    REQUIRE(restored.redo().ok());
    tile = restored.copyTile("terrain:one");
    REQUIRE(tile.ok());
    REQUIRE(tile.value().holes.height(16, 10) == 1.f);
    REQUIRE(restored.undo().ok());
    REQUIRE(restored.undo().ok());
    tile = restored.copyTile("terrain:one");
    REQUIRE(tile.ok());
    REQUIRE(tile.value().heights.data() == extensionTile().heights.data());
    REQUIRE(tile.value().trees.size() == 4u);
    REQUIRE(tile.value().details[0].density[58] == 8);
    auto bad = saved.value();
    bad.set("version", 3);
    REQUIRE(!workspace.restore(bad).ok());
    auto current = workspace.capture();
    REQUIRE(current.ok());
    REQUIRE(current.value() == saved.value());
    REQUIRE(workspace.restore(baseline.value()).ok());
    options.maximumWork = 1;
    REQUIRE(!workspace.apply(mesh, options).ok());
    current = workspace.capture();
    REQUIRE(current.ok());
    REQUIRE(current.value() == baseline.value());
}
TEST_CASE("procgen.road.authoring.terrainChannelsRollbackAllTilesAndRetainRedoOnLateFailure") {
    auto a = extensionTile(), b = extensionTile();
    b.id = "terrain:two";
    b.splatLayers.resize(1);
    RoadTerrainWorkspace workspace;
    REQUIRE(workspace.initialize({a, b}).ok());
    auto baseline = workspace.capture();
    REQUIRE(baseline.ok());
    RoadTerrainWriteOptions options;
    options.blendDistance = 0.f;
    options.splatLayer    = 1;
    auto mesh             = extensionTerrainMesh();
    REQUIRE(!workspace.apply(mesh, options).ok());
    auto current = workspace.capture();
    REQUIRE(current.ok());
    REQUIRE(current.value() == baseline.value());
    options.splatLayer = -1;
    REQUIRE(workspace.apply(mesh, options).ok());
    REQUIRE(workspace.undo().ok());
    auto saved = workspace.capture();
    REQUIRE(saved.ok());
    options.splatLayer = 1;
    REQUIRE(!workspace.apply(mesh, options).ok());
    current = workspace.capture();
    REQUIRE(current.ok());
    REQUIRE(current.value() == saved.value());
    REQUIRE(workspace.redo().ok());
}
TEST_CASE("procgen.road.authoring.nativeTerrainWorldWritesEveryRoadChannelInOneHistoryFrame") {
    TerrainWorldWorkspace        world;
    TerrainWorldCreationSettings creation;
    creation.tileSize                 = 20.f;
    creation.tileHeight               = 1.f;
    creation.heightmapResolution      = 17;
    creation.controlTextureResolution = creation.detailResolution = creation.treeResolution =
        creation.objectResolution                                 = 16;
    creation.splatLayers                                          = 2;
    creation.defaultDetailDensity                                 = 8;
    REQUIRE(world.create(creation).ok());
    Heightmap fitness(16, 16);
    for (auto& value : fitness.data()) value = 1.f;
    TerrainStampSettings region;
    region.width = region.depth = 20.f;
    TerrainTreePlacementSettings trees;
    trees.asset         = "oak";
    trees.namespaceId   = 42;
    trees.spacing       = 2.f;
    trees.jitterPercent = trees.minimumFitness = 0.f;
    trees.scaleMode                            = TerrainTreeScaleMode::Fixed;
    REQUIRE(world.applyTrees(fitness, trees, region, TerrainTreeOperationMode::Add).ok());
    TerrainObjectPlacementSettings objects;
    objects.spacing        = 4.f;
    objects.jitterPercent  = 0.f;
    objects.minimumFitness = objects.minimumInstanceFitness = 0.f;
    objects.prototype                                       = "rocks";
    objects.namespaceId                                     = 91;
    objects.maxPoints                                       = 1000;
    TerrainObjectInstanceSettings instance;
    instance.asset = "rock";
    REQUIRE(objects.addInstance(instance).ok());
    REQUIRE(world.applyObjects(fitness, objects, region, TerrainObjectOperationMode::Add).ok());
    PointSet beforeTrees, beforeObjects;
    REQUIRE(world.copyTrees(0, beforeTrees).ok());
    REQUIRE(!beforeTrees.empty());
    REQUIRE(world.copyObjects(0, beforeObjects).ok());
    REQUIRE(!beforeObjects.empty());
    const auto              history = world.getOperationCount();
    RoadTerrainWriteOptions options;
    options.blendDistance = 0.f;
    options.splatLayer    = 1;
    auto mesh             = extensionTerrainMesh();
    REQUIRE(world.conformRoad(mesh, options).ok());
    REQUIRE(world.getOperationCount() == history + 1);
    Heightmap          height, holes;
    TerrainSplatmap    splat;
    TerrainDetailLayer detail;
    PointSet           kept, rocks;
    REQUIRE(world.copyHeightmap(0, height).ok());
    REQUIRE(height.height(4, 8) == 1.f);
    REQUIRE(height.height(9, 8) == 0.f);
    REQUIRE(height.height(13, 8) == 0.f);
    REQUIRE(world.copySplatmap(0, splat).ok());
    auto weight = splat.sample(1, 4, 8);
    REQUIRE(weight.ok());
    REQUIRE(weight.value() == 1.f);
    REQUIRE(world.copyDetail(0, detail).ok());
    auto density = detail.sample(4, 8);
    REQUIRE(density.ok());
    REQUIRE(density.value() == 0);
    REQUIRE(world.copyTrees(0, kept).ok());
    REQUIRE(kept.getCount() < beforeTrees.getCount());
    for (int row = 0; row < kept.getCount(); ++row) {
        REQUIRE(kept.getPointId(row) != 0u);
        REQUIRE(kept.getStringAttribute(row, "asset", "") == "oak");
        REQUIRE(kept.getStringAttribute(row, "spawnNamespace", "") == "42");
        const auto id       = kept.getPointId(row);
        auto       original = std::find_if(beforeTrees.points().begin(), beforeTrees.points().end(),
                                           [&](const auto& p) { return p.id == id; });
        REQUIRE(original != beforeTrees.points().end());
        REQUIRE(kept.getPointSeed(row) == original->seed);
    }
    REQUIRE(world.copyObjects(0, rocks).ok());
    REQUIRE(rocks.getCount() == beforeObjects.getCount());
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyHeightmap(0, height).ok());
    REQUIRE(height.height(4, 8) == 0.f);
    REQUIRE(world.copyTrees(0, kept).ok());
    REQUIRE(kept.getCount() == beforeTrees.getCount());
    options.maximumWork = 1;
    REQUIRE(!world.conformRoad(mesh, options).ok());
    REQUIRE(world.getOperationCount() == history + 1);
    REQUIRE(world.redo().ok());
    options.maximumWork              = 16u * 1024u * 1024u;
    options.cutTunnelHoles           = true;
    options.tunnelHolesAtPortalsOnly = false;  // Explicit legacy whole-footprint contract.
    REQUIRE(world.conformRoad(mesh, options).ok());
    auto holeCopy = world.copyHoles(0);
    REQUIRE(holeCopy.ok());
    holes = std::move(holeCopy).takeValue();
    REQUIRE(holes.height(13, 8) == 1.f);
    REQUIRE(world.undo().ok());
    holeCopy = world.copyHoles(0);
    REQUIRE(holeCopy.ok());
    holes = std::move(holeCopy).takeValue();
    REQUIRE(holes.height(13, 8) == 0.f);
    REQUIRE(world.redo().ok());
    holeCopy = world.copyHoles(0);
    REQUIRE(holeCopy.ok());
    holes = std::move(holeCopy).takeValue();
    REQUIRE(holes.height(13, 8) == 1.f);
    creation.controlTextureResolution = creation.detailResolution = 1;
    REQUIRE(world.create(creation).ok());
    REQUIRE(world.conformRoad(mesh, options).ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(splat.getWidth() == 1);
    weight = splat.sample(1, 0, 0);
    REQUIRE(weight.ok());
    REQUIRE(weight.value() == 0.f);
    REQUIRE(world.copyDetail(0, detail).ok());
    density = detail.sample(0, 0);
    REQUIRE(density.ok());
    REQUIRE(density.value() == 8);
}
