// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "procgen_road_profiles.cpp"
  ]
}
*/
// clang-format on

#include "procgen/road/RoadBake.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace eve;
using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
RoadNetwork profileRoad(float firstScale, float lastScale, float roll) {
    RoadNetwork network;
    auto        from = network.addNode(-10.f, 0.f, 0.f);
    auto        to   = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    RoadStyle style;
    style.widthInterpolation    = RoadWidthInterpolation::Linear;  // Exercise the persisted legacy profile contract.
    style.samplingSpacingMeters = 0.f;  // Exact vertex-set reversal is the legacy fixed-count contract.
    style.samplingPolicy        = RoadSamplingPolicy::MaximumIntervals;
    style.laneWidth             = 3.f;
    style.curbWidth             = 0.5f;
    style.sidewalkWidth         = 1.f;
    auto edge                   = network.addEdge(from.value(), to.value(),
                                                  {{-10.f, 0.f, 0.f, firstScale, roll},
                                                   {0.f, 0.f, 0.f, (firstScale + lastScale) * 0.5f, roll},
                                                   {10.f, 0.f, 0.f, lastScale, roll}},
                                                  1, 1, style);
    REQUIRE(edge.ok());
    return network;
}

RoadBakeResult bakeProfile(const RoadNetwork& network) {
    RoadBakeOptions options;
    options.includePiers        = false;
    options.includeMarkings     = false;
    options.sideObjectClearance = 0.f;
    options.pathSegmentsPerEdge = 64;
    auto baked                  = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    return std::move(baked).takeValue();
}
}  // namespace

TEST_CASE("procgen.road.pointProfilesDriveWidthNavigationAndPlacements") {
    auto  network      = profileRoad(1.f, 2.f, 0.f);
    auto  baked        = bakeProfile(network);
    float endpointHalf = 0.f, outerHalf = 0.f;
    for (int vertex = 0; vertex < baked.mesh.getVertexCount(); ++vertex) {
        if (std::fabs(baked.mesh.getPositionX(vertex) - 10.f) > 1e-5f) continue;
        endpointHalf = std::max(endpointHalf, std::fabs(baked.mesh.getPositionZ(vertex)));
    }
    // Asphalt half-width becomes 6m while 0.5m curb and 1m sidewalk stay metric.
    REQUIRE(std::fabs(endpointHalf - 7.5f) < 1e-4f);
    REQUIRE(baked.overlay.lanes.size() == 2u);
    REQUIRE(std::fabs(baked.overlay.lanes.front().xyz.back()) > 2.9f);
    REQUIRE(std::fabs(baked.overlay.lanes.front().xyz[2]) < 1.6f);
    for (int row = 0; row < baked.placements.getCount(); ++row)
        outerHalf = std::max(outerHalf, std::fabs(baked.placements.getZ(row)));
    REQUIRE(outerHalf > 6.f);
}

TEST_CASE("procgen.road.pointProfilesBankTheSurfaceAndPreservePhysicalReversal") {
    auto network = profileRoad(1.f, 1.f, 30.f);
    REQUIRE(network.setEdgeControlPoints(1, {{-10.f, 0.f, 0.f, 1.f, 30.f}, {10.f, 0.f, 0.f, 1.f, 30.f}}).ok());
    RoadBakeOptions physicalOptions;
    physicalOptions.includePiers      = false;
    physicalOptions.includeMarkings   = false;
    physicalOptions.includeNavigation = false;
    auto initialBake                  = bakeRoadNetwork(network, physicalOptions);
    REQUIRE(initialBake.ok());
    auto  before      = std::move(initialBake).takeValue();
    float maxAsphaltY = 0.f, minAsphaltY = 0.f;
    for (int triangle = 0; triangle < before.mesh.getIndexCount() / 3; ++triangle) {
        int group = before.mesh.getTriangleGroup(triangle);
        if (group < 0 || before.mesh.getGroupName(group) != "asphalt") continue;
        for (int corner = 0; corner < 3; ++corner) {
            float y     = before.mesh.getPositionY(before.mesh.getIndex(triangle * 3 + corner));
            maxAsphaltY = std::max(maxAsphaltY, y);
            minAsphaltY = std::min(minAsphaltY, y);
        }
    }
    REQUIRE(std::fabs(maxAsphaltY - 1.5f) < 1e-4f);
    REQUIRE(std::fabs(minAsphaltY + 1.5f) < 1e-4f);
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(network.edges().front().controlPoints.front().rollDegrees == -30.f);
    auto reversedBake = bakeRoadNetwork(network, physicalOptions);
    REQUIRE(reversedBake.ok());
    auto after = std::move(reversedBake).takeValue();
    REQUIRE(after.mesh.getVertexCount() == before.mesh.getVertexCount());
    for (int vertex = 0; vertex < before.mesh.getVertexCount(); ++vertex) {
        bool matched = false;
        for (int candidate = 0; candidate < after.mesh.getVertexCount(); ++candidate) {
            if (std::fabs(before.mesh.getPositionX(vertex) - after.mesh.getPositionX(candidate)) < 1e-4f &&
                std::fabs(before.mesh.getPositionY(vertex) - after.mesh.getPositionY(candidate)) < 1e-4f &&
                std::fabs(before.mesh.getPositionZ(vertex) - after.mesh.getPositionZ(candidate)) < 1e-4f) {
                matched = true;
                break;
            }
        }
        REQUIRE(matched);
    }
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(network.edges().front().controlPoints.front().rollDegrees == 30.f);
}

TEST_CASE("procgen.road.pointProfilesSurviveNodeMoveSplitAndInvalidEdit") {
    auto network = profileRoad(0.5f, 2.f, 20.f);
    REQUIRE(network.setNodePosition(1, -12.f, 1.f, 0.f).ok());
    REQUIRE(network.edges().front().controlPoints.front().widthScale == 0.5f);
    REQUIRE(network.edges().front().controlPoints.front().rollDegrees == 20.f);
    auto split = network.splitEdgeAtSplineParameter(1, 0.25f, 1.f);
    REQUIRE(split.ok());
    const auto& first = network.edges().front();
    REQUIRE(std::fabs(first.controlPoints.back().widthScale - 0.875f) < 1e-5f);
    REQUIRE(first.controlPoints.back().rollDegrees == 20.f);
    const auto revision       = network.revision();
    auto       points         = first.controlPoints;
    points.front().widthScale = 0.f;
    REQUIRE(!network.setEdgeControlPoints(first.id, points).ok());
    REQUIRE(network.revision() == revision);
    points.front().widthScale  = 1.f;
    points.front().rollDegrees = std::numeric_limits<float>::infinity();
    REQUIRE(!network.setEdgeControlPoints(first.id, points).ok());
    REQUIRE(network.revision() == revision);
}
