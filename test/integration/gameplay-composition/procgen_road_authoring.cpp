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
    "procgen_road_authoring.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include "editing/EditingAuthority.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/road/RoadBake.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

using namespace eve;
using namespace eve::procgen;
using namespace eve::procgen::road;
using namespace eve::procgen_editing;

namespace {
RoadNetwork authoredRoad(const RoadStyle& style, float elevation = 0.f) {
    RoadNetwork network;
    auto        a = network.addNode(-20.f, elevation, 0.f), b = network.addNode(20.f, elevation, 0.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto edge = network.addEdge(a.value(), b.value(),
                                {{-20.f, elevation, 0.f}, {0.f, elevation, 0.f}, {20.f, elevation, 0.f}}, 1, 1, style);
    REQUIRE(edge.ok());
    return network;
}
RoadStyle flatStyle() {
    RoadStyle style;
    style.laneWidth = 3.f;
    style.curbWidth = style.curbHeight = style.sidewalkWidth = style.sidewalkHeight = 0.f;
    style.automaticStructures                                                       = false;
    return style;
}
RoadBakeResult bakeAuthoring(const RoadNetwork& network) {
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 64;
    options.includeMarkings     = false;
    options.includePiers        = false;
    auto baked                  = bakeRoadNetwork(network, options);
    if (!baked.ok()) std::cerr << baked.status().describe() << '\n';
    REQUIRE(baked.ok());
    return std::move(baked).takeValue();
}
int triangles(const MeshBuild& mesh, const std::string& group) {
    int result = 0;
    for (int i = 0; i < mesh.getIndexCount() / 3; ++i)
        if (mesh.getGroupName(mesh.getTriangleGroup(i)) == group) ++result;
    return result;
}
RoadContinuousFacility barrier() {
    RoadContinuousFacility result;
    result.id            = 7;
    result.start         = .2f;
    result.end           = .8f;
    result.lateralOffset = 4.f;
    result.profile       = {{-.2f, 0.f, RoadMaterial::Curb},
                            {.2f, 0.f, RoadMaterial::Curb},
                            {.2f, 1.f, RoadMaterial::Curb},
                            {-.2f, 1.f, RoadMaterial::Curb}};
    return result;
}
}  // namespace

TEST_CASE("procgen.road.authoring.bridgeTunnelIntervalsKeepTerrainEvenNearItsSurface") {
    auto style       = flatStyle();
    style.structures = {{.2f, .4f, RoadStructureKind::Bridge}, {.6f, .8f, RoadStructureKind::Tunnel}};
    auto network     = authoredRoad(style, 1.f);
    auto baked       = bakeAuthoring(network);
    REQUIRE(triangles(baked.mesh, "asphalt.bridge") > 0);
    REQUIRE(triangles(baked.mesh, "asphalt.tunnel") > 0);
    REQUIRE(triangles(baked.mesh, "deck") > 0);
    REQUIRE(triangles(baked.mesh, "tunnel") > 0);
    Heightmap                 terrain(45, 9);
    RoadTerrainConformOptions options;
    options.originX          = -22.f;
    options.originZ          = -4.f;
    options.blendDistance    = 4.f;
    options.maxVerticalDelta = 100.f;
    auto receipt             = conformHeightmapToRoadWithReceipt(terrain, baked.mesh, options);
    REQUIRE(receipt.ok());
    REQUIRE(std::fabs(terrain.height(3, 4) - 1.f) < 1e-5f);  // ground section
    REQUIRE(terrain.height(12, 4) == 0.f);                   // bridge, even within blending radius
    REQUIRE(terrain.height(32, 4) == 0.f);                   // tunnel
    REQUIRE(receipt.value().skippedBridgeSamples > 0);
    REQUIRE(receipt.value().skippedTunnelSamples > 0);
    REQUIRE(restoreHeightmapFromRoad(terrain, receipt.value()).ok());
    REQUIRE(terrain.height(3, 4) == 0.f);
    auto overlapping                = style;
    overlapping.structures[1].start = .3f;
    const auto revision             = network.revision();
    REQUIRE(!network.setEdgeStyle(1, overlapping).ok());
    REQUIRE(network.revision() == revision);
}

TEST_CASE("procgen.road.authoring.intervalsSplitAndSnapshotsUndoWithoutDuplication") {
    auto style       = flatStyle();
    style.structures = {{.1f, .7f, RoadStructureKind::Bridge}};
    style.facilities.push_back(barrier());
    auto                  network = authoredRoad(style);
    RoadNetworkEditTarget target("road:authoring", network);
    const auto            saved = target.snapshotValue();
    auto                  split = target.makeSplitEdge(1, 1, 1.f);
    REQUIRE(split.ok());
    auto candidate = target.cloneDomainState();
    REQUIRE(candidate->applyDomainOperation(split.value()).ok());
    REQUIRE(target.commitDomainState(std::move(candidate)).ok());
    REQUIRE(network.edges().size() == 2u);
    REQUIRE(std::fabs(network.edges()[0].style.structures[0].start - .2f) < 1e-4f);
    REQUIRE(network.edges()[0].style.structures[0].end == 1.f);
    REQUIRE(std::fabs(network.edges()[1].style.structures[0].end - .4f) < 1e-4f);
    REQUIRE(!network.edges()[0].style.facilities[0].capEnd);
    REQUIRE(!network.edges()[1].style.facilities[0].capStart);
    REQUIRE(target.loadSnapshot(saved).ok());
    REQUIRE(target.snapshotValue() == saved);
    auto changed         = style;
    changed.crossSection = {{-4.f, 0.f, RoadMaterial::Asphalt}, {4.f, 0.f, RoadMaterial::Asphalt}};
    auto operation       = target.makeSetEdgeStyle(1, changed);
    REQUIRE(operation.ok());
    editing::LocalWorldAuthority authority(&target);
    editing::TransactionSpec     transaction;
    transaction.id           = editing::TransactionId("road.authoring");
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    auto plan = authority.preflight(transaction, std::span<const editing::DomainOperation>(&operation.value(), 1));
    REQUIRE(plan.ok());
    auto receipt = authority.commit(plan.value());
    REQUIRE(receipt.ok());
    REQUIRE(authority.compensate(receipt.value()).ok());
    REQUIRE(target.snapshotValue() == saved);
    auto  corrupt              = saved;
    auto& edges                = *corrupt.getIf<EditorValue::Object>()->at("edges").getIf<EditorValue::Array>();
    auto& encodedStyle         = *edges[0].getIf<EditorValue::Object>()->at("style").getIf<EditorValue::Object>();
    encodedStyle["structures"] = EditorValue::Array{EditorValue::Object{
        {"start", .8}, {"end", .2}, {"kind", std::int64_t{0}}, {"tunnelHeight", 4.5}, {"wallThickness", .35}}};
    REQUIRE(!target.loadSnapshot(corrupt).ok());
    REQUIRE(target.snapshotValue() == saved);
}
