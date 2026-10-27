// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "procgen_road_placement_anchors.cpp"
  ]
}
*/
// clang-format on

#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
RoadBakeOptions anchorOptions(float spacing = 8.f) {
    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includeJunctions  = false;
    options.sideObjectSpacing = spacing;
    options.sideObjectOffset  = .75f;
    return options;
}

PointSet role(const RoadBakeResult& baked, const char* name) {
    return filterPointStringAttribute(baked.placements, "road_role", name, false);
}
}  // namespace

TEST_CASE("procgen.road.placements.spec.edgeAnchors") {
    auto made = RoadNetwork::makeStraight(24.f, 2);
    REQUIRE(made.ok());
    auto* networkView = &made.value();
    auto  options     = anchorOptions();

    SUB_CASE("transitions and side anchors have stable unique IDs, roles, and positions") {
        auto first  = bakeRoadNetwork(*networkView, options);
        auto second = bakeRoadNetwork(*networkView, options);
        REQUIRE(first.ok());
        REQUIRE(second.ok());
        REQUIRE(first.value().placements.getCount() == 10);
        REQUIRE(second.value().placements.getCount() == 10);
        REQUIRE(role(first.value(), "transition.start").getCount() == 1);
        REQUIRE(role(first.value(), "transition.end").getCount() == 1);
        REQUIRE(role(first.value(), "side.left").getCount() == 4);
        REQUIRE(role(first.value(), "side.right").getCount() == 4);
        std::vector<std::uint64_t> identities;
        for (int row = 0; row < first.value().placements.getCount(); ++row) {
            const auto& points = first.value().placements;
            REQUIRE(points.getIntAttribute(row, "road_edge_id", 0) == networkView->edges().front().id);
            REQUIRE(points.hasVectorAttribute(row, "road_direction"));
            REQUIRE(std::isfinite(points.getYaw(row)));
            REQUIRE(points.getPointId(row) != 0);
            REQUIRE(points.getPointId(row) == second.value().placements.getPointId(row));
            REQUIRE(points.getX(row) == second.value().placements.getX(row));
            REQUIRE(points.getZ(row) == second.value().placements.getZ(row));
            identities.push_back(points.getPointId(row));
        }
        std::sort(identities.begin(), identities.end());
        REQUIRE(std::adjacent_find(identities.begin(), identities.end()) == identities.end());
    };
    SUB_CASE("per-edge interval and side flags select exact distances") {
        auto edgeId                  = networkView->edges().front().id;
        auto style                   = networkView->edges().front().style;
        style.sideObjectDistribution = RoadSideDistribution::LegacyCentered;
        style.sideObjectStartOffset  = 5.f;
        style.sideObjectEndOffset    = 3.f;
        style.sideObjectsLeft        = true;
        style.sideObjectsRight       = false;
        REQUIRE(networkView->setEdgeStyle(edgeId, style).ok());
        auto baked = bakeRoadNetwork(*networkView, options);
        REQUIRE(baked.ok());
        auto left = role(baked.value(), "side.left");
        REQUIRE(left.getCount() == 2);
        REQUIRE(role(baked.value(), "side.right").getCount() == 0);
        REQUIRE(left.getFloatAttribute(0, "road_distance", -1.f) == 9.f);
        REQUIRE(left.getFloatAttribute(1, "road_distance", -1.f) == 17.f);
        const auto revision         = networkView->revision();
        style.sideObjectStartOffset = -1.f;
        REQUIRE(!networkView->setEdgeStyle(edgeId, style).ok());
        REQUIRE(networkView->revision() == revision);
    };
    SUB_CASE("placement budget rejects the whole bake") {
        options.maximumPlacements = 1;
        REQUIRE(!bakeRoadNetwork(*networkView, options).ok());
    };
}

TEST_CASE("procgen.road.placements.spec.junctionAnchors") {
    auto options = anchorOptions(1000.f);
    SUB_CASE("cross and tee expose their arm counts and channelizing island") {
        auto cross = RoadNetwork::makeCross(32.f, 1);
        REQUIRE(cross.ok());
        auto xBake = bakeRoadNetwork(cross.value(), options);
        REQUIRE(xBake.ok());
        auto x = role(xBake.value(), "junction.x");
        REQUIRE(x.getCount() == 1);
        REQUIRE(x.getIntAttribute(0, "road_arm_count", 0) == 4);
        REQUIRE(x.getIntAttribute(0, "road_node_id", 0) != 0);

        RoadNetwork tee;
        auto        hub   = tee.addNode(0.f, 0.f, 0.f, 4.f);
        auto        west  = tee.addNode(-16.f, 0.f, 0.f, 2.f);
        auto        east  = tee.addNode(16.f, 0.f, 0.f, 2.f);
        auto        north = tee.addNode(0.f, 0.f, -16.f, 2.f);
        REQUIRE(hub.ok());
        REQUIRE(west.ok());
        REQUIRE(east.ok());
        REQUIRE(north.ok());
        REQUIRE(tee.addEdge(west.value(), hub.value(), {{}, {}}, 1).ok());
        REQUIRE(tee.addEdge(hub.value(), east.value(), {{}, {}}, 1).ok());
        REQUIRE(tee.addEdge(hub.value(), north.value(), {{}, {}}, 1).ok());
        auto tBake = bakeRoadNetwork(tee, options);
        REQUIRE(tBake.ok());
        auto t      = role(tBake.value(), "junction.t");
        auto island = role(tBake.value(), "junction.channelizing.island");
        REQUIRE(t.getCount() == 1);
        REQUIRE(island.getCount() == 1);
        REQUIRE(island.getIntAttribute(0, "road_node_id", 0) == hub.value());
        REQUIRE(island.getIntAttribute(0, "road_arm_count", 0) == 3);
        REQUIRE(std::hypot(island.getX(0), island.getZ(0)) > 1.f);
    };
    SUB_CASE("Y classification survives a duplicate control marker through legacy sampling") {
        RoadNetwork network;
        auto        hub = network.addNode(0.f, 0.f, 0.f, 4.f);
        auto        a   = network.addNode(0.f, 0.f, -16.f, 2.f);
        auto        b   = network.addNode(-14.f, 0.f, 8.f, 2.f);
        auto        c   = network.addNode(14.f, 0.f, 8.f, 2.f);
        REQUIRE(hub.ok());
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        REQUIRE(c.ok());
        REQUIRE(network.addEdge(a.value(), hub.value(), {{}, {}}, 1).ok());
        REQUIRE(network.addEdge(hub.value(), b.value(), {{}, {}, {}}, 1).ok());
        REQUIRE(network.addEdge(hub.value(), c.value(), {{}, {}}, 1).ok());
        REQUIRE(!bakeRoadNetwork(network, options).ok());
        for (const auto& edge : network.edges())
            if (edge.controlPoints.size() == 3) {
                auto style                  = edge.style;
                style.samplingSpacingMeters = 0.f;
                REQUIRE(network.setEdgeStyle(edge.id, style).ok());
            }
        auto baked = bakeRoadNetwork(network, options);
        REQUIRE(baked.ok());
        auto y      = role(baked.value(), "junction.y");
        auto island = role(baked.value(), "junction.channelizing.island");
        REQUIRE(y.getCount() == 1);
        REQUIRE(y.getIntAttribute(0, "road_arm_count", 0) == 3);
        REQUIRE(island.getCount() == 1);
        REQUIRE(island.getIntAttribute(0, "road_node_id", 0) == hub.value());
    };
    SUB_CASE("minor approach receives a yield anchor; signal controls every approach") {
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
        auto minor            = major;
        minor.trafficPriority = 2;
        auto westEdge         = network.addEdge(west.value(), hub.value(), {{}, {}}, 1, 1, major);
        auto eastEdge         = network.addEdge(east.value(), hub.value(), {{}, {}}, 1, 1, major);
        auto minorEdge        = network.addEdge(south.value(), hub.value(), {{}, {}}, 1, 1, minor);
        REQUIRE(westEdge.ok());
        REQUIRE(eastEdge.ok());
        REQUIRE(minorEdge.ok());
        REQUIRE(network.setNodeJunctionControl(hub.value(), RoadJunctionControl::Yield).ok());
        auto first  = bakeRoadNetwork(network, options);
        auto second = bakeRoadNetwork(network, options);
        REQUIRE(first.ok());
        REQUIRE(second.ok());
        auto yield = role(first.value(), "junction.control.yield");
        REQUIRE(yield.getCount() == 1);
        REQUIRE(yield.getIntAttribute(0, "road_edge_id", 0) == minorEdge.value());
        REQUIRE(yield.getIntAttribute(0, "traffic_priority", -1) == 2);
        REQUIRE(yield.getPointId(0) == role(second.value(), "junction.control.yield").getPointId(0));
        REQUIRE(network.setNodeJunctionControl(hub.value(), RoadJunctionControl::Signal).ok());
        auto signalled = bakeRoadNetwork(network, options);
        REQUIRE(signalled.ok());
        auto signals = role(signalled.value(), "junction.control.signal");
        REQUIRE(signals.getCount() == 3);
        std::vector<std::int64_t> edges;
        for (int row = 0; row < signals.getCount(); ++row)
            edges.push_back(signals.getIntAttribute(row, "road_edge_id", 0));
        std::sort(edges.begin(), edges.end());
        REQUIRE(edges == (std::vector<std::int64_t>{westEdge.value(), eastEdge.value(), minorEdge.value()}));
    };
}

TEST_CASE("procgen.road.placements.spec.clearance") {
    SUB_CASE("side anchors stay outside the junction socket") {
        auto made = RoadNetwork::makeCross(40.f, 2);
        REQUIRE(made.ok());
        std::uint32_t hubId  = 0;
        float         radius = 0.f;
        for (const auto& node : made.value().nodes())
            if (std::fabs(node.x) < 1e-4f && std::fabs(node.z) < 1e-4f) {
                hubId  = node.id;
                radius = node.junctionRadius;
            }
        REQUIRE(hubId != 0);
        auto options             = anchorOptions(2.f);
        options.includeJunctions = true;
        auto baked               = bakeRoadNetwork(made.value(), options);
        REQUIRE(baked.ok());
        int seen = 0;
        for (int row = 0; row < baked.value().placements.getCount(); ++row) {
            auto name = baked.value().placements.getStringAttribute(row, "road_role", "");
            if (name != "side.left" && name != "side.right") continue;
            const auto id =
                static_cast<std::uint32_t>(baked.value().placements.getIntAttribute(row, "road_edge_id", 0));
            auto edge = made.value().edgeResult(id);
            REQUIRE(edge.ok());
            float distance = baked.value().placements.getFloatAttribute(row, "road_distance", -1.f);
            if (edge.value().from == hubId) REQUIRE(distance >= radius);
            if (edge.value().to == hubId) REQUIRE(distance <= 20.f - radius);
            ++seen;
        }
        REQUIRE(seen > 0);
    };
    SUB_CASE("clearance rejects nearby parallel road centerlines") {
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
        auto options                = anchorOptions(8.f);
        options.sideObjectOffset    = .5f;
        options.sideObjectClearance = 2.f;
        auto baked                  = bakeRoadNetwork(network, options);
        REQUIRE(baked.ok());
        int seen = 0;
        for (int row = 0; row < baked.value().placements.getCount(); ++row) {
            auto name = baked.value().placements.getStringAttribute(row, "road_role", "");
            if (name != "side.left" && name != "side.right") continue;
            REQUIRE(baked.value().placements.getFloatAttribute(row, "road_clearance", -1.f) == 2.f);
            ++seen;
        }
        REQUIRE(seen == 24);
    };
}
