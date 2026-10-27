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
    "procgen_road_connections.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include "editing/EditingAuthority.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/road/RoadBake.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>

using namespace eve::procgen::road;
using namespace eve::procgen_editing;

namespace {
RoadNetwork connectionRoad() {
    RoadNetwork network;
    auto        a = network.addNode(-20.f, 0.f, 0.f), hub = network.addNode(0.f, 0.f, 0.f),
         b = network.addNode(20.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(hub.ok());
    REQUIRE(b.ok());
    RoadStyle style;
    style.laneWidth           = 3.f;
    style.automaticStructures = false;
    style.crossSection        = {{-3.f, 0.f, RoadMaterial::Asphalt}, {3.f, 0.f, RoadMaterial::Asphalt}};
    auto first =
        network.addEdge(a.value(), hub.value(), {{-20.f, 0.f, 0.f}, {-10.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}, 1, 1, style);
    auto second =
        network.addEdge(hub.value(), b.value(), {{0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}, {20.f, 0.f, 0.f}}, 1, 1, style);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(network.connectAllTurns(hub.value()).ok());
    return network;
}
RoadConnection squareConnection() {
    RoadConnection connection;
    connection.assetId   = "test.square-road-connection";
    connection.vertices  = {{-3.f, 0.f, -3.f, 0.f, 1.f, 0.f, 0.f, 0.f},
                            {-3.f, 0.f, 3.f, 0.f, 1.f, 0.f, 0.f, 1.f},
                            {3.f, 0.f, 3.f, 0.f, 1.f, 0.f, 1.f, 1.f},
                            {3.f, 0.f, -3.f, 0.f, 1.f, 0.f, 1.f, 0.f}};
    connection.triangles = {{0, 1, 2, RoadMaterial::Asphalt}, {0, 2, 3, RoadMaterial::Asphalt}};
    RoadConnectionPort left;
    left.id          = 1;
    left.edgeId      = 1;
    left.x           = -3.f;
    left.forwardX    = -1.f;
    left.section     = {{-3.f, 0.f, RoadMaterial::Asphalt}, {3.f, 0.f, RoadMaterial::Asphalt}};
    auto right       = left;
    right.id         = 2;
    right.edgeId     = 2;
    right.x          = 3.f;
    right.forwardX   = 1.f;
    connection.ports = {left, right};
    return connection;
}
}  // namespace

TEST_CASE("procgen.road.authoring.customConnectionSnapshotUndoAndSplitRebindStableIds") {
    auto                  network = connectionRoad();
    RoadNetworkEditTarget target("road:connections", network);
    const auto            original  = target.snapshotValue();
    auto                  operation = target.makeSetNodeConnection(2, squareConnection());
    REQUIRE(operation.ok());
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec     transaction;
    transaction.id           = eve::editing::TransactionId("road.connection");
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    auto plan = authority.preflight(transaction, std::span<const eve::editing::DomainOperation>(&operation.value(), 1));
    REQUIRE(plan.ok());
    auto receipt = authority.commit(plan.value());
    REQUIRE(receipt.ok());
    const auto            saved = target.snapshotValue();
    RoadNetwork           restored;
    RoadNetworkEditTarget restoredTarget("road:connections", restored);
    REQUIRE(restoredTarget.loadSnapshot(saved).ok());
    REQUIRE(restored.nodes()[1].connection.assetId == "test.square-road-connection");
    REQUIRE(bakeRoadNetwork(restored).ok());
    auto split = restored.splitEdge(1, 1, 1.f);
    REQUIRE(split.ok());
    auto hub = restored.nodeResult(2);
    REQUIRE(hub.ok());
    REQUIRE(hub.value().connection.ports[0].edgeId == split.value().secondEdgeId);
    REQUIRE(bakeRoadNetwork(restored).ok());
    REQUIRE(authority.compensate(receipt.value()).ok());
    REQUIRE(target.snapshotValue() == original);
    auto  malformed  = saved;
    auto& nodes      = *malformed.getIf<EditorValue::Object>()->at("nodes").getIf<EditorValue::Array>();
    auto& connection = *nodes[1].getIf<EditorValue::Object>()->at("connection").getIf<EditorValue::Object>();
    connection["triangles"] =
        EditorValue::Array{EditorValue::Array{std::int64_t{0}, std::int64_t{1}, std::int64_t{999}, std::int64_t{0}}};
    REQUIRE(!target.loadSnapshot(malformed).ok());
    REQUIRE(target.snapshotValue() == original);
}

TEST_CASE("procgen.road.authoring.closedBeamConnectionValidatesTheEntireSeamLoop") {
    auto                                network = connectionRoad();
    const std::vector<RoadProfilePoint> profile{{-3.f, 0.f, RoadMaterial::Asphalt},
                                                {3.f, 0.f, RoadMaterial::Deck},
                                                {3.f, -.5f, RoadMaterial::Deck},
                                                {-3.f, -.5f, RoadMaterial::Deck}};
    for (const auto& edge : network.edges()) {
        auto style               = edge.style;
        style.crossSection       = profile;
        style.crossSectionClosed = true;
        REQUIRE(network.setEdgeStyle(edge.id, style).ok());
    }
    auto connection    = squareConnection();
    connection.assetId = "test.closed-beam";
    connection.vertices.clear();
    connection.triangles.clear();
    for (std::size_t i = 0; i < profile.size(); ++i) {
        const auto& a      = profile[i];
        const auto& b      = profile[(i + 1) % profile.size()];
        const float length = std::hypot(b.side - a.side, b.up - a.up), ny = (b.side - a.side) / length,
                    nz  = (b.up - a.up) / length;
        const auto base = static_cast<std::uint32_t>(connection.vertices.size());
        connection.vertices.insert(connection.vertices.end(), {{-3.f, a.up, -a.side, 0.f, ny, nz, 0.f, 0.f},
                                                               {3.f, a.up, -a.side, 0.f, ny, nz, 1.f, 0.f},
                                                               {3.f, b.up, -b.side, 0.f, ny, nz, 1.f, 1.f},
                                                               {-3.f, b.up, -b.side, 0.f, ny, nz, 0.f, 1.f}});
        connection.triangles.push_back({base, base + 1, base + 2, a.material});
        connection.triangles.push_back({base, base + 2, base + 3, a.material});
    }
    for (auto& port : connection.ports) {
        port.section       = profile;
        port.sectionClosed = true;
    }
    REQUIRE(network.setNodeConnection(2, connection).ok());
    REQUIRE(bakeRoadNetwork(network).ok());
    RoadNetworkEditTarget target("road:closed-seam", network);
    const auto            saved = target.snapshotValue();
    RoadNetwork           restored;
    RoadNetworkEditTarget other("road:closed-seam", restored);
    REQUIRE(other.loadSnapshot(saved).ok());
    REQUIRE(restored.nodes()[1].connection.ports[0].sectionClosed);
    REQUIRE(bakeRoadNetwork(restored).ok());
    // Remove the last section span while leaving referenced vertices from its neighbors.
    connection.triangles.resize(connection.triangles.size() - 2);
    const auto revision = network.revision();
    REQUIRE(!network.setNodeConnection(2, connection).ok());
    REQUIRE(network.revision() == revision);
}
