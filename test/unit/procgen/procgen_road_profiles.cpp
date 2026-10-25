#include "procgen/road/RoadBake.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

RoadNetwork profiledRoad(float firstWidth, float lastWidth, float roll) {
    RoadNetwork network;
    auto        start  = network.addNode(-10.f, 0.f, 0.f);
    auto        finish = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(start.ok());
    REQUIRE(finish.ok());
    RoadStyle style;
    style.widthInterpolation    = RoadWidthInterpolation::Linear;
    style.samplingSpacingMeters = 0.f;
    style.samplingPolicy        = RoadSamplingPolicy::MaximumIntervals;
    style.laneWidth             = 3.f;
    style.curbWidth             = .5f;
    style.sidewalkWidth         = 1.f;
    REQUIRE(network
                .addEdge(start.value(), finish.value(),
                         {{-10.f, 0.f, 0.f, firstWidth, roll},
                          {0.f, 0.f, 0.f, (firstWidth + lastWidth) * .5f, roll},
                          {10.f, 0.f, 0.f, lastWidth, roll}},
                         1, 1, style)
                .ok());
    return network;
}

RoadBakeResult bake(const RoadNetwork& network, bool navigation) {
    RoadBakeOptions options;
    options.includePiers        = false;
    options.includeMarkings     = false;
    options.includeNavigation   = navigation;
    options.sideObjectClearance = 0.f;
    options.pathSegmentsPerEdge = 64;
    auto result                 = bakeRoadNetwork(network, options);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

std::pair<float, float> asphaltHeightRange(const MeshBuild& mesh) {
    float lowest = 0.f, highest = 0.f;
    for (int triangle = 0; triangle < mesh.getIndexCount() / 3; ++triangle) {
        int group = mesh.getTriangleGroup(triangle);
        if (group < 0 || mesh.getGroupName(group) != "asphalt") continue;
        for (int corner = 0; corner < 3; ++corner) {
            float y = mesh.getPositionY(mesh.getIndex(triangle * 3 + corner));
            lowest  = std::min(lowest, y);
            highest = std::max(highest, y);
        }
    }
    return {lowest, highest};
}
}  // namespace

TEST_CASE("procgen.road.profile.spec.widthScalesAsphaltButKeepsMetricCurbAndSidewalk") {
    auto  network   = profiledRoad(1.f, 2.f, 0.f);
    auto  baked     = bake(network, true);
    float outerHalf = 0.f;
    for (int vertex = 0; vertex < baked.mesh.getVertexCount(); ++vertex) {
        if (!near(baked.mesh.getPositionX(vertex), 10.f)) continue;
        outerHalf = std::max(outerHalf, std::fabs(baked.mesh.getPositionZ(vertex)));
    }
    // At the second anchor: 2 lanes * 3m * widthScale 2 / 2 + .5m curb + 1m sidewalk.
    REQUIRE(near(outerHalf, 7.5f));
    REQUIRE(baked.overlay.lanes.size() == 2);
    REQUIRE(std::fabs(baked.overlay.lanes.front().xyz.back()) > 2.9f);
    REQUIRE(std::fabs(baked.overlay.lanes.front().xyz[2]) < 1.6f);
}

TEST_CASE("procgen.road.profile.spec.rollBanksAsphaltAndReversalPreservesWorldGeometry") {
    auto       network = profiledRoad(1.f, 1.f, 30.f);
    const auto edgeId  = network.edges().front().id;
    REQUIRE(network.setEdgeControlPoints(edgeId, {{-10.f, 0.f, 0.f, 1.f, 30.f}, {10.f, 0.f, 0.f, 1.f, 30.f}}).ok());
    auto before             = bake(network, false);
    auto [minimum, maximum] = asphaltHeightRange(before.mesh);
    REQUIRE(near(minimum, -1.5f));
    REQUIRE(near(maximum, 1.5f));

    REQUIRE(network.reverseEdge(edgeId).ok());
    REQUIRE(network.edges().front().controlPoints.front().rollDegrees == -30.f);
    auto after = bake(network, false);
    REQUIRE(after.mesh.getVertexCount() == before.mesh.getVertexCount());
    for (int i = 0; i < before.mesh.getVertexCount(); ++i) {
        bool found = false;
        for (int j = 0; j < after.mesh.getVertexCount(); ++j)
            if (near(before.mesh.getPositionX(i), after.mesh.getPositionX(j)) &&
                near(before.mesh.getPositionY(i), after.mesh.getPositionY(j)) &&
                near(before.mesh.getPositionZ(i), after.mesh.getPositionZ(j)))
                found = true;
        REQUIRE(found);
    }
    REQUIRE(network.reverseEdge(edgeId).ok());
    REQUIRE(network.edges().front().controlPoints.front().rollDegrees == 30.f);
}

TEST_CASE("procgen.road.profile.spec.splitInterpolatesProfileAndInvalidEditIsAtomic") {
    auto       network = profiledRoad(.5f, 2.f, 20.f);
    const auto edgeId  = network.edges().front().id;
    REQUIRE(network.setNodePosition(network.nodes().front().id, -12.f, 1.f, 0.f).ok());
    REQUIRE(network.edges().front().controlPoints.front().widthScale == .5f);
    auto split = network.splitEdgeAtSplineParameter(edgeId, .25f, 1.f);
    REQUIRE(split.ok());
    const auto& first = network.edges().front();
    REQUIRE(near(first.controlPoints.back().widthScale, .875f));
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
