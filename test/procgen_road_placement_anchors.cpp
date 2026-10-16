// Placement contracts split from the large generic road test translation unit.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "procgen/ObjectBuildLayer.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.placements.exposeReusableTransitionAndSideObjectAnchors") {
    auto network = RoadNetwork::makeStraight(24.f, 2);
    REQUIRE(network.ok());
    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includeJunctions  = false;
    options.sideObjectSpacing = 8.f;
    options.sideObjectOffset  = 0.75f;
    auto first                = bakeRoadNetwork(network.value(), options);
    auto second               = bakeRoadNetwork(network.value(), options);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE_EQ(first.value().placements.getCount(), 10);
    REQUIRE_EQ(second.value().placements.getCount(), first.value().placements.getCount());

    int                        startCount = 0, endCount = 0, leftCount = 0, rightCount = 0;
    std::vector<std::uint64_t> ids;
    for (int i = 0; i < first.value().placements.getCount(); ++i) {
        const auto role = first.value().placements.getStringAttribute(i, "road_role", "");
        if (role == "transition.start") ++startCount;
        if (role == "transition.end") ++endCount;
        if (role == "side.left") ++leftCount;
        if (role == "side.right") ++rightCount;
        REQUIRE_EQ(first.value().placements.getIntAttribute(i, "road_edge_id", 0), 1);
        REQUIRE(first.value().placements.hasVectorAttribute(i, "road_direction"));
        REQUIRE(std::isfinite(first.value().placements.getYaw(i)));
        REQUIRE_NE(first.value().placements.getPointId(i), std::uint64_t(0));
        REQUIRE_EQ(first.value().placements.getPointId(i), second.value().placements.getPointId(i));
        REQUIRE_EQ(first.value().placements.getX(i), second.value().placements.getX(i));
        REQUIRE_EQ(first.value().placements.getZ(i), second.value().placements.getZ(i));
        ids.push_back(first.value().placements.getPointId(i));
    }
    std::sort(ids.begin(), ids.end());
    REQUIRE(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    REQUIRE_EQ(startCount, 1);
    REQUIRE_EQ(endCount, 1);
    REQUIRE_EQ(leftCount, 4);
    REQUIRE_EQ(rightCount, 4);

    const auto leftAnchors = filterPointStringAttribute(first.value().placements, "road_role", "side.left", false);
    REQUIRE_EQ(leftAnchors.getCount(), 4);
    ObjectBuildLayer objects;
    REQUIRE(objects.addAsset("asset://road/guardrail-a", 3.f).ok());
    REQUIRE(objects.addAsset("asset://road/guardrail-b", 1.f).ok());
    objects.setSeed(17);
    REQUIRE(objects.setRandomRotation(0.f, 0.f, -4.f, 4.f, 0.f, 0.f).ok());
    auto instancesA = objects.build(leftAnchors);
    auto instancesB = objects.build(leftAnchors);
    REQUIRE(instancesA.ok());
    REQUIRE(instancesB.ok());
    REQUIRE_EQ(instancesA.value().getCount(), 4);
    for (int i = 0; i < instancesA.value().getCount(); ++i) {
        REQUIRE_EQ(instancesA.value().getStringAttribute(i, "road_role", ""), std::string("side.left"));
        REQUIRE(!instancesA.value().getStringAttribute(i, "asset", "").empty());
        REQUIRE_EQ(instancesA.value().getStringAttribute(i, "asset", ""),
                   instancesB.value().getStringAttribute(i, "asset", ""));
        REQUIRE_EQ(instancesA.value().getYaw(i), instancesB.value().getYaw(i));
    }

    options.maximumPlacements = 9;
    auto overBudget           = bakeRoadNetwork(network.value(), options);
    REQUIRE(!overBudget.ok());
}

TEST_CASE("procgen.road.placements.classifyReusableJunctionPrefabAnchors") {
    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includeJunctions  = false;
    options.sideObjectSpacing = 1000.f;

    auto cross = RoadNetwork::makeCross(32.f, 1);
    REQUIRE(cross.ok());
    auto crossBake = bakeRoadNetwork(cross.value(), options);
    REQUIRE(crossBake.ok());
    const auto xAnchors = filterPointStringAttribute(crossBake.value().placements, "road_role", "junction.x", false);
    REQUIRE_EQ(xAnchors.getCount(), 1);
    REQUIRE_EQ(xAnchors.getIntAttribute(0, "road_arm_count", 0), 4);
    REQUIRE_NE(xAnchors.getIntAttribute(0, "road_node_id", 0), 0);

    RoadNetwork tNetwork;
    auto        tHub   = tNetwork.addNode(0.f, 0.f, 0.f, 4.f);
    auto        tWest  = tNetwork.addNode(-16.f, 0.f, 0.f, 2.f);
    auto        tEast  = tNetwork.addNode(16.f, 0.f, 0.f, 2.f);
    auto        tNorth = tNetwork.addNode(0.f, 0.f, -16.f, 2.f);
    REQUIRE(tHub.ok());
    REQUIRE(tWest.ok());
    REQUIRE(tEast.ok());
    REQUIRE(tNorth.ok());
    REQUIRE(tNetwork.addEdge(tWest.value(), tHub.value(), {{}, {}}, 1).ok());
    REQUIRE(tNetwork.addEdge(tHub.value(), tEast.value(), {{}, {}}, 1).ok());
    REQUIRE(tNetwork.addEdge(tHub.value(), tNorth.value(), {{}, {}}, 1).ok());
    auto tBake = bakeRoadNetwork(tNetwork, options);
    REQUIRE(tBake.ok());
    const auto tAnchors = filterPointStringAttribute(tBake.value().placements, "road_role", "junction.t", false);
    REQUIRE_EQ(tAnchors.getCount(), 1);
    REQUIRE(tAnchors.hasVectorAttribute(0, "road_direction"));
    const auto tIslands =
        filterPointStringAttribute(tBake.value().placements, "road_role", "junction.channelizing.island", false);
    REQUIRE_EQ(tIslands.getCount(), 1);
    REQUIRE_EQ(tIslands.getIntAttribute(0, "road_node_id", 0), tHub.value());
    REQUIRE_EQ(tIslands.getIntAttribute(0, "road_arm_count", 0), 3);
    REQUIRE_GT(std::hypot(tIslands.getX(0), tIslands.getZ(0)), 1.f);
    REQUIRE(tIslands.hasVectorAttribute(0, "road_direction"));

    RoadNetwork yNetwork;
    auto        yHub = yNetwork.addNode(0.f, 0.f, 0.f, 4.f);
    auto        yA   = yNetwork.addNode(0.f, 0.f, -16.f, 2.f);
    auto        yB   = yNetwork.addNode(-14.f, 0.f, 8.f, 2.f);
    auto        yC   = yNetwork.addNode(14.f, 0.f, 8.f, 2.f);
    REQUIRE(yHub.ok());
    REQUIRE(yA.ok());
    REQUIRE(yB.ok());
    REQUIRE(yC.ok());
    REQUIRE(yNetwork.addEdge(yA.value(), yHub.value(), {{}, {}}, 1).ok());
    // A duplicated endpoint control sample must not corrupt arm classification.
    REQUIRE(yNetwork.addEdge(yHub.value(), yB.value(), {{}, {}, {}}, 1).ok());
    REQUIRE(yNetwork.addEdge(yHub.value(), yC.value(), {{}, {}}, 1).ok());
    auto strictBake = bakeRoadNetwork(yNetwork, options);
    REQUIRE(!strictBake.ok());
    REQUIRE_EQ(strictBake.error()->code(), eve::DiagnosticCode::InvalidArgument);
    // The parameter scanner rejects duplicate markers. The historical sampler
    // remains available for the independent duplicate-marker arm classification.
    for (const auto& edge : yNetwork.edges()) {
        if (edge.controlPoints.size() != 3u) continue;
        auto style                  = edge.style;
        style.samplingSpacingMeters = 0.f;
        REQUIRE(yNetwork.setEdgeStyle(edge.id, style).ok());
    }
    auto yBake = bakeRoadNetwork(yNetwork, options);
    REQUIRE(yBake.ok());
    const auto yAnchors = filterPointStringAttribute(yBake.value().placements, "road_role", "junction.y", false);
    REQUIRE_EQ(yAnchors.getCount(), 1);
    REQUIRE_EQ(yAnchors.getIntAttribute(0, "road_arm_count", 0), 3);
    const auto yIslands =
        filterPointStringAttribute(yBake.value().placements, "road_role", "junction.channelizing.island", false);
    REQUIRE_EQ(yIslands.getCount(), 1);
    REQUIRE_EQ(yIslands.getIntAttribute(0, "road_node_id", 0), yHub.value());
    REQUIRE_GT(yIslands.getFloatAttribute(0, "road_distance", 0.f), 1.f);

    options.maximumPlacements = 8;
    REQUIRE(!bakeRoadNetwork(cross.value(), options).ok());
}

TEST_CASE("procgen.road.placements.bakePriorityAwareJunctionControlAnchors") {
    RoadNetwork network;
    auto        hub   = network.addNode(0.f, 0.f, 0.f, 5.f);
    auto        west  = network.addNode(-20.f, 0.f, 0.f, 2.f);
    auto        east  = network.addNode(20.f, 0.f, 0.f, 2.f);
    auto        south = network.addNode(0.f, 0.f, 20.f, 2.f);
    REQUIRE(hub.ok());
    REQUIRE(west.ok());
    REQUIRE(east.ok());
    REQUIRE(south.ok());
    RoadStyle major;
    major.trafficPriority = 9;
    RoadStyle minor       = major;
    minor.trafficPriority = 2;
    auto westEdge         = network.addEdge(west.value(), hub.value(), {{}, {}}, 1, 1, major);
    auto eastEdge         = network.addEdge(east.value(), hub.value(), {{}, {}}, 1, 1, major);
    auto minorEdge        = network.addEdge(south.value(), hub.value(), {{}, {}}, 1, 1, minor);
    REQUIRE(westEdge.ok());
    REQUIRE(eastEdge.ok());
    REQUIRE(minorEdge.ok());
    REQUIRE(network.setNodeJunctionControl(hub.value(), RoadJunctionControl::Yield).ok());

    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includeJunctions  = false;
    options.sideObjectSpacing = 1000.f;
    auto first                = bakeRoadNetwork(network, options);
    auto second               = bakeRoadNetwork(network, options);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    const auto yield =
        filterPointStringAttribute(first.value().placements, "road_role", "junction.control.yield", false);
    REQUIRE_EQ(yield.getCount(), 1);
    REQUIRE_EQ(yield.getIntAttribute(0, "road_node_id", 0), hub.value());
    REQUIRE_EQ(yield.getIntAttribute(0, "road_edge_id", 0), minorEdge.value());
    REQUIRE_EQ(yield.getIntAttribute(0, "traffic_priority", -1), 2);
    REQUIRE_EQ(yield.getIntAttribute(0, "road_lane_direction", -1), static_cast<int>(RoadLaneDirection::Forward));
    REQUIRE(yield.hasVectorAttribute(0, "road_direction"));
    const auto yieldAgain =
        filterPointStringAttribute(second.value().placements, "road_role", "junction.control.yield", false);
    REQUIRE_EQ(yieldAgain.getCount(), 1);
    REQUIRE_EQ(yield.getPointId(0), yieldAgain.getPointId(0));
    REQUIRE_GT(std::hypot(yield.getX(0), yield.getZ(0)), 4.f);

    REQUIRE(network.setNodeJunctionControl(hub.value(), RoadJunctionControl::Signal).ok());
    auto signalled = bakeRoadNetwork(network, options);
    REQUIRE(signalled.ok());
    const auto signals =
        filterPointStringAttribute(signalled.value().placements, "road_role", "junction.control.signal", false);
    REQUIRE_EQ(signals.getCount(), 3);
    std::vector<std::int64_t> controlledEdges;
    for (int row = 0; row < signals.getCount(); ++row)
        controlledEdges.push_back(signals.getIntAttribute(row, "road_edge_id", 0));
    std::sort(controlledEdges.begin(), controlledEdges.end());
    REQUIRE_EQ(controlledEdges[0], std::int64_t{westEdge.value()});
    REQUIRE_EQ(controlledEdges[1], std::int64_t{eastEdge.value()});
    REQUIRE_EQ(controlledEdges[2], std::int64_t{minorEdge.value()});
}

TEST_CASE("procgen.road.placements.respectPerEdgeSideIntervals") {
    auto network = RoadNetwork::makeStraight(24.f, 1);
    REQUIRE(network.ok());
    const auto edgeId            = network.value().edges().front().id;
    auto       style             = network.value().edges().front().style;
    style.sideObjectDistribution = RoadSideDistribution::LegacyCentered;
    style.sideObjectStartOffset  = 5.f;
    style.sideObjectEndOffset    = 3.f;
    style.sideObjectsLeft        = true;
    style.sideObjectsRight       = false;
    REQUIRE(network.value().setEdgeStyle(edgeId, style).ok());

    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includeJunctions  = false;
    options.sideObjectSpacing = 8.f;
    auto baked                = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    REQUIRE_EQ(baked.value().placements.getCount(), 4);

    const auto left  = filterPointStringAttribute(baked.value().placements, "road_role", "side.left", false);
    const auto right = filterPointStringAttribute(baked.value().placements, "road_role", "side.right", false);
    REQUIRE_EQ(left.getCount(), 2);
    REQUIRE_EQ(right.getCount(), 0);
    REQUIRE_EQ(left.getFloatAttribute(0, "road_distance", -1.f), 9.f);
    REQUIRE_EQ(left.getFloatAttribute(1, "road_distance", -1.f), 17.f);

    options.maximumPlacements = 3;
    REQUIRE(!bakeRoadNetwork(network.value(), options).ok());

    style.sideObjectStartOffset = -1.f;
    const auto stableRevision   = network.value().revision();
    REQUIRE(!network.value().setEdgeStyle(edgeId, style).ok());
    REQUIRE_EQ(network.value().revision(), stableRevision);
}

TEST_CASE("procgen.road.placements.keepSideObjectsOutsideJunctionSockets") {
    auto network = RoadNetwork::makeCross(40.f, 2);
    REQUIRE(network.ok());
    std::uint32_t hubId     = 0;
    float         hubRadius = 0.f;
    for (const auto& node : network.value().nodes()) {
        if (std::fabs(node.x) > 1e-4f || std::fabs(node.z) > 1e-4f) continue;
        hubId     = node.id;
        hubRadius = node.junctionRadius;
    }
    REQUIRE_NE(hubId, std::uint32_t{0});

    RoadBakeOptions options;
    options.includeNavigation = false;
    options.sideObjectSpacing = 2.f;
    auto baked                = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());

    int sidePoints = 0;
    for (int row = 0; row < baked.value().placements.getCount(); ++row) {
        const auto role = baked.value().placements.getStringAttribute(row, "road_role", "");
        if (role != "side.left" && role != "side.right") continue;
        const auto edgeId =
            static_cast<std::uint32_t>(baked.value().placements.getIntAttribute(row, "road_edge_id", 0));
        const auto edge = network.value().edgeResult(edgeId);
        REQUIRE(edge.ok());
        const float distance = baked.value().placements.getFloatAttribute(row, "road_distance", -1.f);
        if (edge.value().from == hubId) REQUIRE_GE(distance, hubRadius);
        if (edge.value().to == hubId) REQUIRE_LE(distance, 20.f - hubRadius);
        ++sidePoints;
    }
    REQUIRE_GT(sidePoints, 0);
}

TEST_CASE("procgen.road.placements.avoidAdjacentRoadsAndEachOther") {
    RoadNetwork network;
    auto        a0 = network.addNode(-20.f, 0.f, -7.f, 2.f);
    auto        a1 = network.addNode(20.f, 0.f, -7.f, 2.f);
    auto        b0 = network.addNode(-20.f, 0.f, 7.f, 2.f);
    auto        b1 = network.addNode(20.f, 0.f, 7.f, 2.f);
    REQUIRE(a0.ok());
    REQUIRE(a1.ok());
    REQUIRE(b0.ok());
    REQUIRE(b1.ok());
    REQUIRE(network.addEdge(a0.value(), a1.value(), {{}, {}}, 1, 1).ok());
    REQUIRE(network.addEdge(b0.value(), b1.value(), {{}, {}}, 1, 1).ok());

    RoadBakeOptions options;
    options.includeNavigation   = false;
    options.includeJunctions    = false;
    options.sideObjectSpacing   = 8.f;
    options.sideObjectOffset    = 0.5f;
    options.sideObjectClearance = 2.f;
    auto baked                  = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    int sideAnchors = 0;
    for (int row = 0; row < baked.value().placements.getCount(); ++row) {
        const auto role = baked.value().placements.getStringAttribute(row, "road_role", "");
        if (role != "side.left" && role != "side.right") continue;
        ++sideAnchors;
        REQUIRE_EQ(baked.value().placements.getFloatAttribute(row, "road_clearance", -1.f), 2.f);
        for (const auto& edge : network.edges()) {
            if (baked.value().placements.getIntAttribute(row, "road_edge_id", 0) == edge.id) continue;
            SplinePath path;
            REQUIRE(path.setKindResult("catmullRom").ok());
            for (const auto& point : edge.controlPoints)
                REQUIRE(path.addPointResult(SplinePoint{point.x, point.y, point.z}).ok());
            auto closest =
                path.closestPointResult(baked.value().placements.getX(row), baked.value().placements.getY(row),
                                        baked.value().placements.getZ(row), 32);
            REQUIRE(closest.ok());
            const float dx = baked.value().placements.getX(row) - closest.value().x;
            const float dy = baked.value().placements.getY(row) - closest.value().y;
            const float dz = baked.value().placements.getZ(row) - closest.value().z;
            REQUIRE_GE(std::sqrt(dx * dx + dy * dy + dz * dz), 7.45f);
        }
    }
    REQUIRE_EQ(sideAnchors, 24);
}
