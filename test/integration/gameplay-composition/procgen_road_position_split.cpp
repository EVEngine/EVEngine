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
    "procgen_road_position_split.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/road/RoadNetwork.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>

using namespace eve;
using namespace eve::editing;
using namespace eve::procgen::road;
using namespace eve::procgen_editing;

TEST_CASE("procgen.road.network.positionSplitRefinesCurvedSnapAndRejectsOutsideRadius") {
    RoadNetwork network;
    auto        start = network.addNode(-10.f, 0.f, -10.f);
    auto        end   = network.addNode(10.f, 0.f, -10.f);
    REQUIRE(start.ok());
    REQUIRE(end.ok());
    auto edge = network.addEdge(start.value(), end.value(),
                                {{-10.f, 0.f, -10.f}, {-3.f, 0.f, 10.f}, {3.f, 0.f, 10.f}, {10.f, 0.f, -10.f}}, 1, 0);
    REQUIRE(edge.ok());
    const auto revision = network.revision();
    REQUIRE(!network.splitEdgeAtPosition(edge.value(), {0.f, 0.05f, 12.5f}, 0.049f).ok());
    REQUIRE_EQ(network.revision(), revision);
    auto split = network.splitEdgeAtPosition(edge.value(), {0.f, 0.05f, 12.5f}, 0.051f);
    REQUIRE(split.ok());
    auto node = network.nodeResult(split.value().nodeId);
    REQUIRE(node.ok());
    REQUIRE(std::fabs(node.value().x) < 1e-5f);
    REQUIRE(std::fabs(node.value().y) < 1e-5f);
    REQUIRE(std::fabs(node.value().z - 12.5f) < 1e-5f);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.network.positionSplitSnapsIn3dAndRejectsEndpoints") {
    RoadNetwork network;
    auto        from = network.addNode(-10.f, 5.f, 0.f);
    auto        to   = network.addNode(10.f, 5.f, 0.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    auto road = network.addEdge(from.value(), to.value(), {{-10.f, 5.f, 0.f}, {10.f, 5.f, 0.f}}, 1, 0);
    REQUIRE(road.ok());

    const auto unchangedRevision = network.revision();
    REQUIRE(!network.splitEdgeAtPosition(road.value(), {0.f, 0.f, 0.f}, 2.f).ok());
    REQUIRE_EQ(network.revision(), unchangedRevision);
    REQUIRE(!network.splitEdgeAtPosition(road.value(), {-10.f, 5.f, 0.f}, 0.1f).ok());
    REQUIRE_EQ(network.revision(), unchangedRevision);

    auto split = network.splitEdgeAtPosition(road.value(), {2.f, 7.f, 1.f}, 3.f, 3.5f);
    REQUIRE(split.ok());
    auto node = network.nodeResult(split.value().nodeId);
    REQUIRE(node.ok());
    REQUIRE(std::fabs(node.value().x - 2.f) < 1e-5f);
    REQUIRE(std::fabs(node.value().y - 5.f) < 1e-5f);
    REQUIRE(std::fabs(node.value().z) < 1e-5f);
    REQUIRE_EQ(network.edgeResult(split.value().firstEdgeId).value().controlPoints.size(), std::size_t{2});
    REQUIRE_EQ(network.edgeResult(split.value().secondEdgeId).value().controlPoints.size(), std::size_t{2});
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.positionSplitUndoRedoKeepsProjectedPointAndIds") {
    RoadNetwork network;
    auto        from = network.addNode(-12.f, 4.f, 0.f);
    auto        to   = network.addNode(12.f, 4.f, 0.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    auto road = network.addEdge(from.value(), to.value(), {{-12.f, 4.f, 0.f}, {12.f, 4.f, 0.f}}, 1, 0);
    REQUIRE(road.ok());

    RoadNetworkEditTarget target("road:position-split", network);
    auto                  operation = target.makeSplitEdgeAtPosition(road.value(), {3.f, 5.f, 2.f}, 3.f, 4.f);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE_EQ(network.nodeCount(), 3);
    REQUIRE_EQ(network.edgeCount(), 2);
    const auto continuation = std::find_if(network.edges().begin(), network.edges().end(),
                                           [&](const auto& edge) { return edge.id != road.value(); });
    REQUIRE(continuation != network.edges().end());
    const auto continuationId = continuation->id;
    const auto insertedNodeId = continuation->from;
    auto       inserted       = network.nodeResult(insertedNodeId);
    REQUIRE(inserted.ok());
    REQUIRE(std::fabs(inserted.value().x - 3.f) < 1e-5f);
    REQUIRE(std::fabs(inserted.value().y - 4.f) < 1e-5f);
    REQUIRE(std::fabs(inserted.value().z) < 1e-5f);

    DomainOperation undo = operation.value();
    undo.type            = operation.value().inverseType;
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    REQUIRE_EQ(network.nodeCount(), 2);
    REQUIRE_EQ(network.edgeCount(), 1);
    REQUIRE_EQ(network.edgeResult(road.value()).value().controlPoints.size(), std::size_t{2});

    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE(network.nodeResult(insertedNodeId).ok());
    REQUIRE(network.edgeResult(continuationId).ok());
    REQUIRE_EQ(network.edgeResult(road.value()).value().to, insertedNodeId);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.connectNodeAtPositionSnapsToVisibleCurve") {
    RoadNetwork network;
    auto        curveStart = network.addNode(-10.f, 0.f, -10.f);
    auto        curveEnd   = network.addNode(10.f, 0.f, -10.f);
    auto        source     = network.addNode(0.f, 0.f, 20.f);
    REQUIRE(curveStart.ok());
    REQUIRE(curveEnd.ok());
    REQUIRE(source.ok());
    auto curve = network.addEdge(curveStart.value(), curveEnd.value(),
                                 {{-10.f, 0.f, -10.f}, {-3.f, 0.f, 10.f}, {3.f, 0.f, 10.f}, {10.f, 0.f, -10.f}}, 1, 0);
    REQUIRE(curve.ok());

    RoadNetworkEditTarget target("road:curve-branch", network);
    auto                  operation =
        target.makeConnectNodeAtPosition(source.value(), curve.value(), {0.f, 0.f, 12.5f}, 0.2f, 2.f, 1, 0);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE_EQ(network.nodeCount(), 4);
    REQUIRE_EQ(network.edgeCount(), 3);
    const auto junction = std::find_if(network.nodes().begin(), network.nodes().end(), [&](const auto& node) {
        return node.id != source.value() && std::fabs(node.x) < 1e-3f && node.z > 12.f;
    });
    REQUIRE(junction != network.nodes().end());
    const auto junctionId = junction->id;
    REQUIRE(network.validate().ok());

    DomainOperation undo = operation.value();
    undo.type            = operation.value().inverseType;
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    REQUIRE_EQ(network.nodeCount(), 3);
    REQUIRE_EQ(network.edgeCount(), 1);
    REQUIRE(!network.nodeResult(junctionId).ok());
    REQUIRE_EQ(network.edgeResult(curve.value()).value().controlPoints.size(), std::size_t{4});
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE(network.nodeResult(junctionId).ok());
    REQUIRE(network.validate().ok());
}
