#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace test_procgen_road_circular_unit {
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.circularReferenceArcAndReversePreserveGeometry") {
    RoadNetwork network;
    auto        a = network.addNode(-24, 0, 0), b = network.addNode(24, 1, 0);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    std::vector<RoadControlPoint> points = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}};
    points[1].pathControl                = RoadPathControl::Circular;
    auto edge                            = network.addEdge(a.value(), b.value(), points, 2);
    REQUIRE(edge.ok());
    auto makePath = [](const std::vector<RoadControlPoint>& points) {
        SplinePath path;
        if (!path.setKindResult("catmullRom").ok()) throw std::runtime_error("invalid kind");
        for (const auto& p : points) {
            SplinePoint q;
            q.x                  = p.x;
            q.y                  = p.y;
            q.z                  = p.z;
            q.spanControl        = static_cast<SplineSpanControl>(p.pathControl);
            q.circularFromEnd    = p.circularFromEnd;
            q.alignStraightStart = p.alignStraightStart;
            q.alignStraightEnd   = p.alignStraightEnd;
            if (!path.addPointResult(q).ok()) throw std::runtime_error("invalid point");
        }
        return path;
    };
    auto path   = makePath(points);
    auto middle = path.evaluateResult(.5f);
    REQUIRE(middle.ok());
    REQUIRE(std::fabs(middle.value().x) < 1e-5f);
    REQUIRE(std::fabs(middle.value().z - (std::sqrt(320.f) - 8.f)) < 1e-5f);
    auto before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    REQUIRE(network.reverseEdge(edge.value()).ok());
    REQUIRE(network.edges()[0].controlPoints[1].circularFromEnd);
    auto reversed = makePath(network.edges()[0].controlPoints);
    for (int i = 0; i <= 192; ++i) {
        auto p = path.evaluateResult(static_cast<float>(i) / 192.f),
             q = reversed.evaluateResult(1.f - static_cast<float>(i) / 192.f);
        REQUIRE(p.ok());
        REQUIRE(q.ok());
        REQUIRE(std::fabs(p.value().x - q.value().x) < 1e-4f);
        REQUIRE(std::fabs(p.value().y - q.value().y) < 1e-4f);
        REQUIRE(std::fabs(p.value().z - q.value().z) < 1e-4f);
    }
    auto state = captureRoadNetwork(network);
    REQUIRE(state.ok());
    auto restored = restoreRoadNetwork(state.value());
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].controlPoints[1].circularFromEnd);
    REQUIRE(network.reverseEdge(edge.value()).ok());
    REQUIRE(captureRoadNetwork(network).value() == before.value());
    // Use an internal predecessor change; authoritative endpoint positions remain untouched.
    std::vector<RoadControlPoint> invalid = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {0, 1, 8}, {24, 1, 0}};
    invalid[2].pathControl                = RoadPathControl::Circular;
    REQUIRE(!network.setEdgeControlPoints(edge.value(), invalid).ok());
    REQUIRE(captureRoadNetwork(network).value() == before.value());
}
}  // namespace test_procgen_road_circular_unit

namespace test_procgen_road_controllers_unit {
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.outgoingStraightControllersRetainHeightContractAndReverse") {
    auto source = RoadNetwork::makeStraight(48.f, 2);
    REQUIRE(source.ok());
    auto network = std::move(source).takeValue();
    REQUIRE(network.setNodePosition(2, 24.f, 1.f, 0.f).ok());
    std::vector<RoadControlPoint> points = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}};
    points[0].pathControl                = RoadPathControl::StraightXYZ;
    points[1].pathControl                = RoadPathControl::StraightXZ;
    REQUIRE(network.setEdgeControlPoints(1, points).ok());
    auto makePath = [](const std::vector<RoadControlPoint>& points) {
        SplinePath path;
        if (!path.setKindResult("catmullRom").ok()) throw std::runtime_error("invalid kind");
        for (const auto& p : points) {
            SplinePoint q;
            q.x                  = p.x;
            q.y                  = p.y;
            q.z                  = p.z;
            q.alignStraightStart = p.alignStraightStart;
            q.alignStraightEnd   = p.alignStraightEnd;
            q.spanControl        = static_cast<SplineSpanControl>(p.pathControl);
            if (!path.addPointResult(q).ok()) throw std::runtime_error("invalid point");
        }
        return path;
    };
    auto before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    auto path   = makePath(points);
    auto middle = path.evaluateResult(.5f);
    REQUIRE(middle.ok());
    REQUIRE(std::fabs(middle.value().x) < 1e-6f);
    REQUIRE(std::fabs(middle.value().z - 8.f) < 1e-6f);
    REQUIRE(std::fabs(middle.value().y - 3.875f) < 1e-6f);
    REQUIRE(network.reverseEdge(1).ok());
    auto reversed = makePath(network.edges()[0].controlPoints);
    for (int i = 0; i <= 96; ++i) {
        auto a = path.evaluateResult(static_cast<float>(i) / 96.f);
        auto b = reversed.evaluateResult(1.f - static_cast<float>(i) / 96.f);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        REQUIRE(std::fabs(a.value().x - b.value().x) < 1e-4f);
        REQUIRE(std::fabs(a.value().y - b.value().y) < 1e-4f);
        REQUIRE(std::fabs(a.value().z - b.value().z) < 1e-4f);
    }
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(captureRoadNetwork(network).value() == before.value());
    auto restored = restoreRoadNetwork(before.value());
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].controlPoints[1].pathControl == RoadPathControl::StraightXZ);
    points[1].pathControl = RoadPathControl::StraightXYZ;
    auto straight         = makePath(points).evaluateResult(.5f);
    REQUIRE(straight.ok());
    REQUIRE(std::fabs(straight.value().y - 3.5f) < 1e-6f);
    points[1].pathControl = static_cast<RoadPathControl>(9);
    REQUIRE(!network.setEdgeControlPoints(1, points).ok());
    REQUIRE(captureRoadNetwork(network).value() == before.value());
    auto  legacy = before.value();
    auto& e      = legacy.getIf<eve::Value::Object>()->at("edges").getIf<eve::Value::Array>()->at(0);
    for (auto& p : *e.getIf<eve::Value::Object>()->at("pointProfiles").getIf<eve::Value::Array>())
        p.getIf<eve::Value::Object>()->erase("pathControl");
    auto old = restoreRoadNetwork(legacy);
    REQUIRE(old.ok());
    REQUIRE(old.value().edges()[0].controlPoints[0].pathControl == RoadPathControl::Spline);
    auto unequal           = network.edges()[0].controlPoints;
    unequal[0].pathControl = RoadPathControl::Spline;
    unequal[2].x           = 0.f;
    auto joined            = makePath(unequal).evaluateResult(5.f / 6.f);
    REQUIRE(joined.ok());
    REQUIRE(std::fabs(joined.value().x - 13.5f) < 1e-4f);
    REQUIRE(std::fabs(joined.value().z - 4.5f) < 1e-4f);
    REQUIRE(std::fabs(joined.value().y - 3.25f) < 1e-4f);
}
}  // namespace test_procgen_road_controllers_unit

namespace test_procgen_road_full_roll_unit {
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.twistedProfileUsesMeasuredReferencePhysicalDiagonal") {
    auto created = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(created.ok());
    auto network = std::move(created).takeValue();
    REQUIRE(network.setEdgeControlPoints(1, {{-18, 0, 0}, {0, 0, 0, 1, 270}, {18, 0, 0}}).ok());
    auto style      = network.edges()[0].style;
    style.laneWidth = 3.f;
    style.curbWidth = style.curbHeight = style.sidewalkWidth = style.sidewalkHeight = 0.f;
    style.deckThickness                                                             = 0.f;
    style.samplingPolicy        = RoadSamplingPolicy::ParametricDistanceTurn;
    style.samplingSpacingMeters = .375f;
    style.samplingAngleDegrees  = 4.f;
    style.uvMeters              = 2.f;
    REQUIRE(network.setEdgeStyle(1, style).ok());
    RoadBakeOptions options;
    options.includeMarkings = options.includePiers = options.includeNavigation = options.includePlacements = false;
    auto baked = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    std::unique_ptr<MeshBuild> surface;
    for (int group = 0; group < baked.value().mesh.getGroupCount(); ++group)
        if (baked.value().mesh.getGroupName(group) == "asphalt") surface = baked.value().mesh.copyGroup(group);
    REQUIRE(surface != nullptr);
    const auto& mesh = *surface;
    REQUIRE(mesh.getIndexCount() == 162 * 3);
    // Reflected reference indices [0,3,1,2,3,0] connect previous -Z to next +Z.
    std::vector<int> shared;
    for (int i = 0; i < 3; ++i)
        for (int j = 3; j < 6; ++j)
            if (mesh.getIndex(i) == mesh.getIndex(j)) shared.push_back(mesh.getIndex(i));
    REQUIRE(shared.size() == 2u);
    bool previous = false, next = false;
    for (int index : shared) {
        if (std::fabs(mesh.getPositionX(index) + 18.f) < 1e-5f) {
            previous = true;
            REQUIRE(std::fabs(mesh.getPositionZ(index) + 3.f) < 1e-5f);
            REQUIRE(mesh.getUvU(index) == 0.f);
            REQUIRE(mesh.getUvV(index) == 1.5f);
        } else {
            next = true;
            REQUIRE(std::fabs(mesh.getPositionX(index) + 17.565235f) < 1e-4f);
            REQUIRE(std::fabs(mesh.getPositionZ(index) - 2.9988985f) < 1e-4f);
            REQUIRE(mesh.getUvU(index) > 0.f);
            REQUIRE(mesh.getUvV(index) == -1.5f);
        }
    }
    REQUIRE(previous);
    REQUIRE(next);
}
}  // namespace test_procgen_road_full_roll_unit

namespace test_procgen_road_roll_pivot_unit {
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.rollPivotHoldsSelectedRailAndTravelFrame") {
    for (float pivot : {-3.f, 3.f}) {
        SplinePath path;
        for (float x : {-18.f, 18.f}) {
            SplinePoint point;
            point.x             = x;
            point.rollDegrees   = -10.f;
            point.rollPivotSide = pivot;
            point.worldUpFrame  = true;
            REQUIRE(path.addPointResult(point).ok());
        }
        auto frames = path.sampleFramesResult(8, true);
        REQUIRE(frames.ok());
        for (const auto& frame : frames.value()) {
            REQUIRE(std::fabs(frame.sample.y + pivot * frame.sideY) < 1e-5f);
            REQUIRE(std::fabs(frame.sample.z + pivot * frame.sideZ + pivot) < 1e-5f);
        }
        auto traveled = path.travelFrameResult(13.f, "clamp");
        REQUIRE(traveled.ok());
        REQUIRE(std::fabs(traveled.value().sample.y + pivot * traveled.value().sideY) < 1e-5f);
        REQUIRE(std::fabs(traveled.value().sample.z + pivot * traveled.value().sideZ + pivot) < 1e-5f);
        // Evaluated author centerline stays at its authored elevation; only section frames move.
        REQUIRE(path.travelResult(13.f, "clamp").value().y == 0.f);
    }
    SplinePath  overflow;
    SplinePoint large;
    large.rollDegrees   = 10.f;
    large.rollPivotSide = 3.f;
    large.scaleX        = std::numeric_limits<float>::max();
    REQUIRE(overflow.addPointResult(large).ok());
    large.x = 18.f;
    REQUIRE(overflow.addPointResult(large).ok());
    REQUIRE(!overflow.sampleFramesResult(4, true).ok());
    REQUIRE(!overflow.travelFrameResult(5.f, "clamp").ok());
}
}  // namespace test_procgen_road_roll_pivot_unit

namespace test_procgen_road_strength_unit {
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.splineStrengthChangesOnlyItsOutgoingSpanAndSurvivesReverse") {
    auto source = RoadNetwork::makeStraight(48.f, 2);
    REQUIRE(source.ok());
    auto                          network = std::move(source).takeValue();
    std::vector<RoadControlPoint> points  = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}};
    points[0].splineStrength              = .75f;
    points[1].splineStrength              = .25f;
    REQUIRE(network.setNodePosition(2, 24.f, 1.f, 0.f).ok());
    REQUIRE(network.setEdgeControlPoints(1, points).ok());
    auto authored = captureRoadNetwork(network);
    REQUIRE(authored.ok());
    auto makePath = [](const std::vector<RoadControlPoint>& points) {
        SplinePath path;
        auto       kind = path.setKindResult("catmullRom");
        if (!kind.ok()) throw std::runtime_error(kind.status().describe());
        for (const auto& p : points) {
            SplinePoint q;
            q.x            = p.x;
            q.y            = p.y;
            q.z            = p.z;
            q.spanStrength = p.splineStrength;
            auto added     = path.addPointResult(q);
            if (!added.ok()) throw std::runtime_error(added.status().describe());
        }
        return path;
    };
    auto path   = makePath(network.edges()[0].controlPoints);
    auto middle = path.evaluateResult(.5f);
    REQUIRE(middle.ok());
    REQUIRE(std::fabs(middle.value().z - 8.5f) < 1e-6f);
    REQUIRE(std::fabs(middle.value().y - 3.6875f) < 1e-6f);
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(network.edges()[0].controlPoints[1].splineStrength == .25f);
    REQUIRE(network.edges()[0].controlPoints[2].splineStrength == .75f);
    auto reversed = makePath(network.edges()[0].controlPoints);
    for (int i = 0; i <= 96; ++i) {
        auto a = path.evaluateResult(static_cast<float>(i) / 96.f);
        auto b = reversed.evaluateResult(1.f - static_cast<float>(i) / 96.f);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        REQUIRE(std::fabs(a.value().x - b.value().x) < 1e-4f);
        REQUIRE(std::fabs(a.value().y - b.value().y) < 1e-4f);
        REQUIRE(std::fabs(a.value().z - b.value().z) < 1e-4f);
    }
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(captureRoadNetwork(network).value() == authored.value());
    auto encoded = captureRoadNetwork(network);
    REQUIRE(encoded.ok());
    auto restored = restoreRoadNetwork(encoded.value());
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].controlPoints[1].splineStrength == .25f);
    Params params;
    REQUIRE(setRoadRecipeNetwork(params, network).ok());
    REQUIRE(bakeRoadNetworkRecipe(params).ok());
    auto invalid              = network.edges()[0].controlPoints;
    invalid[1].splineStrength = 0.f;
    REQUIRE(!network.setEdgeControlPoints(1, invalid).ok());
    REQUIRE(captureRoadNetwork(network).value() == encoded.value());
    auto  legacy     = encoded.value();
    auto& legacyEdge = legacy.getIf<eve::Value::Object>()->at("edges").getIf<eve::Value::Array>()->at(0);
    for (auto& profile : *legacyEdge.getIf<eve::Value::Object>()->at("pointProfiles").getIf<eve::Value::Array>())
        profile.getIf<eve::Value::Object>()->erase("splineStrength");
    auto migrated = restoreRoadNetwork(legacy);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().edges()[0].controlPoints[1].splineStrength == .5f);
    auto split = network.splitEdge(1, 1, 4.f);
    REQUIRE(split.ok());
    REQUIRE(network.edges()[0].controlPoints[0].splineStrength == .75f);
    REQUIRE(network.edges()[1].controlPoints[0].splineStrength == .25f);
}
}  // namespace test_procgen_road_strength_unit

namespace test_procgen_road_position_split_unit {
using namespace eve;
using namespace eve::procgen::road;

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
}  // namespace test_procgen_road_position_split_unit
