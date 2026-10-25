#include "editing/EditingAuthority.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadTerrainWorkspace.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace eve::procgen::road;
using eve::procgen::MeshBuild;
namespace {

TEST_CASE("procgen.road.authoring.referenceWorldUpFramePreservesHorizontalSectionsAndMigrates") {
    using namespace eve::procgen;
    SplinePath transport, worldUp;
    for (const auto& p : std::vector<SplinePoint>{{-18, 0, 0}, {0, 3, 7}, {18, 6, 0}}) {
        REQUIRE(transport.addPointResult(p).ok());
        auto point         = p;
        point.worldUpFrame = true;
        REQUIRE(worldUp.addPointResult(point).ok());
    }
    auto oldFrames = transport.sampleFramesResult(96, true), frames = worldUp.sampleFramesResult(96, true);
    REQUIRE(oldFrames.ok());
    REQUIRE(frames.ok());
    float transportedTilt = 0.f;
    for (std::size_t i = 0; i < frames.value().size(); ++i) {
        const auto& frame = frames.value()[i];
        REQUIRE(std::fabs(frame.sideY) < 1e-6f);
        REQUIRE(std::fabs(frame.sideX * frame.forwardX + frame.sideZ * frame.forwardZ) < 1e-6f);
        transportedTilt = std::max(transportedTilt, std::fabs(oldFrames.value()[i].sideY));
    }
    REQUIRE(transportedTilt > .05f);
    auto network = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(network.ok());
    eve::procgen_editing::RoadNetworkEditTarget target("road:frame", network.value());
    const auto                                  saved = target.snapshotValue();
    RoadNetwork                                 restored;
    eve::procgen_editing::RoadNetworkEditTarget copy("road:frame", restored);
    REQUIRE(copy.loadSnapshot(saved).ok());
    REQUIRE(restored.edges()[0].style.frameOrientation == RoadFrameOrientation::WorldUp);
    auto  legacy = saved;
    auto& style  = *legacy.getIf<eve::editing::Value::Object>()
                       ->at("edges")
                       .getIf<eve::editing::Value::Array>()
                       ->at(0)
                       .getIf<eve::editing::Value::Object>()
                       ->at("style")
                       .getIf<eve::editing::Value::Object>();
    style.erase("frameOrientation");
    REQUIRE(copy.loadSnapshot(legacy).ok());
    REQUIRE(restored.edges()[0].style.frameOrientation == RoadFrameOrientation::ParallelTransport);
    const auto migrated = copy.snapshotValue();
    style.emplace("frameOrientation", std::int64_t{8});
    REQUIRE(!copy.loadSnapshot(legacy).ok());
    REQUIRE(copy.snapshotValue() == migrated);
    auto changed             = network.value().edges()[0].style;
    changed.frameOrientation = RoadFrameOrientation::ParallelTransport;
    auto operation           = target.makeSetEdgeStyle(network.value().edges()[0].id, changed);
    REQUIRE(operation.ok());
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec     transaction;
    transaction.id           = eve::editing::TransactionId("road.frame.edit");
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    auto plan = authority.preflight(transaction, std::span<const eve::editing::DomainOperation>(&operation.value(), 1));
    REQUIRE(plan.ok());
    auto receipt = authority.commit(plan.value());
    REQUIRE(receipt.ok());
    REQUIRE(network.value().edges()[0].style.frameOrientation == RoadFrameOrientation::ParallelTransport);
    REQUIRE(authority.compensate(receipt.value()).ok());
    REQUIRE(target.snapshotValue() == saved);
}
}  // namespace

TEST_CASE("procgen.road.authoring.referenceCubicWidthIsExplicitAndLegacySnapshotsRetainLinear") {
    using namespace eve::procgen;
    SplinePath linear, cubic;
    for (int i = 0; i < 3; ++i) {
        SplinePoint point;
        point.x      = static_cast<float>(i) * 18.f - 18.f;
        point.scaleX = i == 0 ? .5f : i == 1 ? 1.5f : 1.f;
        REQUIRE(linear.addPointResult(point).ok());
        point.scaleXInterpolation = SplineScaleInterpolation::CatmullRom;
        REQUIRE(cubic.addPointResult(point).ok());
    }
    auto l = linear.evaluateResult(.25f), c = cubic.evaluateResult(.25f);
    REQUIRE(l.ok());
    REQUIRE(c.ok());
    REQUIRE(std::fabs(l.value().scaleX - 1.f) < 1e-6f);
    REQUIRE(std::fabs(c.value().scaleX - 1.03125f) < 1e-6f);
    REQUIRE(l.value().x == c.value().x);
    SplinePoint invalid;
    invalid.scaleXInterpolation = static_cast<SplineScaleInterpolation>(2);
    REQUIRE(!cubic.addPointResult(invalid).ok());
    REQUIRE(cubic.pointCount() == 3);
    auto network = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(network.ok());
    eve::procgen_editing::RoadNetworkEditTarget target("road:reference-width", network.value());
    const auto                                  saved = target.snapshotValue();
    RoadNetwork                                 restored;
    eve::procgen_editing::RoadNetworkEditTarget copy("road:reference-width", restored);
    REQUIRE(copy.loadSnapshot(saved).ok());
    REQUIRE(restored.edges()[0].style.widthInterpolation == RoadWidthInterpolation::CatmullRom);
    auto  legacy = saved;
    auto& edges  = *legacy.getIf<eve::editing::Value::Object>()->at("edges").getIf<eve::editing::Value::Array>();
    for (auto& edge : edges)
        edge.getIf<eve::editing::Value::Object>()->at("style").getIf<eve::editing::Value::Object>()->erase(
            "widthInterpolation");
    REQUIRE(copy.loadSnapshot(legacy).ok());
    REQUIRE(restored.edges()[0].style.widthInterpolation == RoadWidthInterpolation::Linear);
    const auto migrated = copy.snapshotValue();
    edges[0].getIf<eve::editing::Value::Object>()->at("style").getIf<eve::editing::Value::Object>()->emplace(
        "widthInterpolation", std::int64_t{9});
    REQUIRE(!copy.loadSnapshot(legacy).ok());
    REQUIRE(copy.snapshotValue() == migrated);
    auto points          = network.value().edges()[0].controlPoints;
    points[0].widthScale = .5f;
    points[1].widthScale = 1.5f;
    points[2].widthScale = 1.f;
    REQUIRE(network.value().setEdgeControlPoints(network.value().edges()[0].id, points).ok());
    REQUIRE(network.value().splitEdgeAtSplineParameter(network.value().edges()[0].id, .25f, 1.f).ok());
    REQUIRE(std::fabs(network.value().edges()[0].controlPoints.back().widthScale - 1.03125f) < 1e-6f);
}

TEST_CASE("procgen.road.network.nodeMoveRejectsDegenerateIncidentEdgeAtomically") {
    RoadNetwork network;
    auto        from = network.addNode(-10.f, 0.f, 0.f);
    auto        to   = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    auto edge = network.addEdge(from.value(), to.value(), {{}, {}}, 1, 0);
    REQUIRE(edge.ok());
    REQUIRE_EQ(network.edges().front().controlPoints.size(), 2u);
    auto before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    const auto revision = network.revision();
    auto       moved    = network.setNodePosition(from.value(), 10.f, 0.f, 0.f);
    REQUIRE(!moved.ok());
    REQUIRE_EQ(network.revision(), revision);
    auto after = captureRoadNetwork(network);
    REQUIRE(after.ok());
    REQUIRE(after.value() == before.value());
    eve::procgen_editing::RoadNetworkEditTarget target("road:collapse", network);
    auto                                        planned = target.makeMoveNode(from.value(), 10.f, 0.f, 0.f);
    REQUIRE(!planned.ok());
    REQUIRE_EQ(network.revision(), revision);
    auto afterPlan = captureRoadNetwork(network);
    REQUIRE(afterPlan.ok());
    REQUIRE(afterPlan.value() == before.value());
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.network.coincidentEndpointPositionsDoNotMergeNodeIdentity") {
    RoadNetwork network;
    auto        from = network.addNode(0.f, 0.f, 0.f);
    auto        to   = network.addNode(20.f, 0.f, 0.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    auto edge = network.addEdge(from.value(), to.value(),
                                {{}, {10.f, 0.f, 0.f}, {10.f, 0.f, 10.f}, {0.f, 0.f, 10.f}, {}}, 1, 0);
    REQUIRE(edge.ok());
    auto before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    eve::procgen_editing::RoadNetworkEditTarget target("road:coincident-loop", network);
    auto                                        planned = target.makeMoveNode(to.value(), 0.f, 0.f, 0.f);
    REQUIRE(planned.ok());
    REQUIRE(target.applyDomainOperation(planned.value()).ok());
    REQUIRE_EQ(network.nodeCount(), 2);
    REQUIRE_EQ(network.edges().front().from, from.value());
    REQUIRE_EQ(network.edges().front().to, to.value());
    REQUIRE(network.validate().ok());
    auto saved = captureRoadNetwork(network);
    REQUIRE(saved.ok());
    auto restored = restoreRoadNetwork(saved.value());
    REQUIRE(restored.ok());
    REQUIRE_EQ(restored.value().nodeCount(), 2);
    REQUIRE(restored.value().validate().ok());
    eve::editing::DomainOperation undo = planned.value();
    undo.type                          = undo.inverseType;
    undo.payload                       = undo.inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    auto undone = captureRoadNetwork(network);
    REQUIRE(undone.ok());
    REQUIRE(undone.value() == before.value());
    REQUIRE(target.applyDomainOperation(planned.value()).ok());
    auto redone = captureRoadNetwork(network);
    REQUIRE(redone.ok());
    REQUIRE(redone.value() == saved.value());
}

namespace {
RoadTerrainTile elevatedRoadTile() {
    RoadTerrainTile tile;
    tile.id      = "elevated";
    tile.originX = tile.originZ = -2.f;
    tile.originY                = 100.f;
    tile.sizeX = tile.sizeZ = 4.f;
    tile.heightScale        = 10.f;
    tile.heights            = eve::procgen::Heightmap(5, 5);
    tile.holes              = eve::procgen::Heightmap(5, 5);
    for (auto& sample : tile.heights.data()) sample = .5f;
    tile.splatLayers = {eve::procgen::Heightmap(5, 5), eve::procgen::Heightmap(5, 5)};
    for (auto& sample : tile.splatLayers[0].data()) sample = 1.f;
    tile.details.push_back({"grass", 5, 5, std::vector<int>(25u, 4)});
    tile.trees.push_back({"tree", "oak", 0.f, 105.f, 0.f});
    return tile;
}
MeshBuild elevatedRoadMesh(float worldY = 106.f) {
    MeshBuild mesh;
    mesh.setActiveGroup("asphalt");
    for (const auto p : {RoadControlPoint{-1.f, 106.f, -1.f}, RoadControlPoint{1.f, 106.f, -1.f},
                         RoadControlPoint{1.f, 106.f, 1.f}, RoadControlPoint{-1.f, 106.f, 1.f}})
        mesh.addVertex(p.x, worldY, p.z, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    return mesh;
}
}  // namespace
TEST_CASE("procgen.road.terrain.elevatedOriginMapsNormalizedHeightsAndAllChannels") {
    RoadTerrainWorkspace workspace;
    REQUIRE(workspace.initialize({elevatedRoadTile()}).ok());
    RoadTerrainWriteOptions options;
    options.blendDistance = 0.f;
    options.splatLayer    = 1;
    auto mesh             = elevatedRoadMesh();
    REQUIRE(workspace.apply(mesh, options).ok());
    auto tile = workspace.copyTile("elevated");
    REQUIRE(tile.ok());
    REQUIRE_EQ(tile.value().originY, 100.f);
    REQUIRE(std::fabs(tile.value().heights.height(2, 2) - .6f) < 1e-6f);
    REQUIRE_EQ(tile.value().heights.height(0, 0), .5f);
    REQUIRE_EQ(tile.value().splatLayers[1].height(2, 2), 1.f);
    REQUIRE_EQ(tile.value().details[0].density[12], 0);
    REQUIRE(tile.value().trees.empty());
    REQUIRE(workspace.undo().ok());
    auto saved = workspace.capture();
    REQUIRE(saved.ok());
    auto json = saved.value().toJson();
    REQUIRE(json.ok());
    auto parsed = eve::Value::fromJson(json.value());
    REQUIRE(parsed.ok());
    RoadTerrainWorkspace restored;
    REQUIRE(restored.restore(parsed.value()).ok());
    REQUIRE(restored.redo().ok());
    auto redone = restored.copyTile("elevated");
    REQUIRE(redone.ok());
    REQUIRE_EQ(redone.value().originY, 100.f);
    REQUIRE(std::fabs(redone.value().heights.height(2, 2) - .6f) < 1e-6f);
    REQUIRE(redone.value().trees.empty());
    REQUIRE(restored.undo().ok());
    auto undone = restored.copyTile("elevated");
    REQUIRE(undone.ok());
    REQUIRE_EQ(undone.value().heights.height(2, 2), .5f);
    REQUIRE_EQ(undone.value().trees.size(), 1u);
    auto  invalid = saved.value();
    auto& history = std::get<eve::Value::Array>(invalid.find("history")->storage());
    std::get<eve::Value::Array>(history.front().storage()).front().set("originY", "invalid");
    auto stable = restored.capture();
    REQUIRE(stable.ok());
    REQUIRE(!restored.restore(invalid).ok());
    auto after = restored.capture();
    REQUIRE(after.ok());
    REQUIRE(after.value() == stable.value());
    auto  mismatched = saved.value();
    auto& frames     = std::get<eve::Value::Array>(mismatched.find("history")->storage());
    std::get<eve::Value::Array>(frames.front().storage()).front().set("originY", 0.f);
    REQUIRE(!restored.restore(mismatched).ok());
    auto afterMismatch = restored.capture();
    REQUIRE(afterMismatch.ok());
    REQUIRE(afterMismatch.value() == stable.value());
    auto invalidTile    = elevatedRoadTile();
    invalidTile.originY = std::numeric_limits<float>::infinity();
    REQUIRE(!restored.initialize({invalidTile}).ok());
    auto afterInitialize = restored.capture();
    REQUIRE(afterInitialize.ok());
    REQUIRE(afterInitialize.value() == stable.value());
}
TEST_CASE("procgen.road.terrain.originYLegacyMigrationAndDirectHeightmapReceipt") {
    auto                      tile = elevatedRoadTile();
    RoadTerrainConformOptions options;
    options.originX = options.originZ = -2.f;
    options.originY                   = 100.f;
    options.heightScale               = 10.f;
    options.blendDistance             = 0.f;
    auto receipt                      = conformHeightmapToRoadWithReceipt(tile.heights, elevatedRoadMesh(), options);
    REQUIRE(receipt.ok());
    REQUIRE(!receipt.value().sampleIndices.empty());
    REQUIRE(std::fabs(tile.heights.height(2, 2) - .6f) < 1e-6f);
    REQUIRE(restoreHeightmapFromRoad(tile.heights, receipt.value()).ok());
    REQUIRE_EQ(tile.heights.height(2, 2), .5f);
    const auto stableSamples = tile.heights.data();
    options.originY          = std::numeric_limits<float>::infinity();
    auto invalid             = conformHeightmapToRoadWithReceipt(tile.heights, elevatedRoadMesh(), options);
    REQUIRE(!invalid.ok());
    REQUIRE(tile.heights.data() == stableSamples);
    tile.originY = 0.f;
    RoadTerrainWorkspace workspace;
    REQUIRE(workspace.initialize({tile}).ok());
    RoadTerrainWriteOptions write;
    write.blendDistance = 0.f;
    write.splatLayer    = 1;
    REQUIRE(workspace.apply(elevatedRoadMesh(6.f), write).ok());
    auto snapshot = workspace.capture();
    REQUIRE(snapshot.ok());
    auto legacy = snapshot.value();
    for (auto& frame : std::get<eve::Value::Array>(legacy.find("history")->storage()))
        for (auto& encoded : std::get<eve::Value::Array>(frame.storage()))
            std::get<eve::Value::Object>(encoded.storage()).erase("originY");
    REQUIRE(!workspace.restore(legacy).ok());  // v2 requires an explicit mapping baseline.
    legacy.set("version", 1);
    REQUIRE(workspace.restore(legacy).ok());
    auto migrated = workspace.copyTile("elevated");
    REQUIRE(migrated.ok());
    REQUIRE_EQ(migrated.value().originY, 0.f);
    REQUIRE(workspace.undo().ok());
    auto previous = workspace.copyTile("elevated");
    REQUIRE(previous.ok());
    REQUIRE_EQ(previous.value().originY, 0.f);
    REQUIRE_EQ(previous.value().heights.height(2, 2), .5f);
    REQUIRE(workspace.redo().ok());
    auto next = workspace.copyTile("elevated");
    REQUIRE(next.ok());
    REQUIRE_EQ(next.value().originY, 0.f);
    REQUIRE(std::fabs(next.value().heights.height(2, 2) - .6f) < 1e-6f);
}
