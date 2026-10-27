#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <vector>

namespace test_procgen_road_parameter_sampling_unit {
using namespace eve::procgen;
using namespace eve::procgen::road;
namespace {
RoadStyle scanStyle() {
    RoadStyle style;
    style.laneWidth             = 3.f;
    style.crossSection          = {{-3, 0, RoadMaterial::Asphalt}, {3, 0, RoadMaterial::Asphalt}};
    style.automaticStructures   = false;
    style.samplingSpacingMeters = 10.f;
    style.samplingAngleDegrees  = 20.f;
    style.samplingPolicy        = RoadSamplingPolicy::ParametricDistanceTurn;
    return style;
}
RoadBakeOptions scanOptions() {
    RoadBakeOptions options;
    options.includePiers      = false;
    options.includeMarkings   = false;
    options.includeNavigation = false;
    options.includeJunctions  = false;
    options.includePlacements = false;
    return options;
}
RoadNetwork referenceScanRoad() {
    RoadNetwork network;
    auto        a = network.addNode(-24, 0, 0), b = network.addNode(24, 1, 0);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(network.addEdge(a.value(), b.value(), {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}}, 2, 0, scanStyle())
                .ok());
    return network;
}
}  // namespace

TEST_CASE("procgen.road.authoring.parameterScanMatchesCoarseReferenceRetainedCenters") {
    auto network = referenceScanRoad();
    auto baked   = bakeRoadNetwork(network, scanOptions());
    REQUIRE(baked.ok());
    REQUIRE(baked.value().mesh.getVertexCount() == 28);
    REQUIRE(baked.value().mesh.getIndexCount() == 42);
    const std::array<float, 7> expectedX = {-15.168f, -8.f, 1.82872f, 8.f, 14.2209f, 22.7409f, 24.f};
    for (std::size_t i = 0; i < expectedX.size(); ++i) {
        const int   vertex = static_cast<int>(i * 4 + 1);
        const float x = (baked.value().mesh.getPositionX(vertex) + baked.value().mesh.getPositionX(vertex + 1)) * .5f;
        REQUIRE(std::fabs(x - expectedX[i]) < 1e-3f);
    }
    Params params;
    REQUIRE(setRoadRecipeNetwork(params, network).ok());
    params.setBool("piers", false);
    params.setBool("markings", false);
    params.setBool("navigation", false);
    params.setBool("junctions", false);
    params.setBool("placements", false);
    REQUIRE(bakeRoadNetworkRecipe(params).value().mesh.positions() == baked.value().mesh.positions());
}

TEST_CASE("procgen.road.authoring.parameterScanWorkBudgetIsSharedAndFailureAtomic") {
    RoadNetwork network;
    auto        a = network.addNode(0, 0, 0), b = network.addNode(4, 0, 0), c = network.addNode(8, 0, 0);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(c.ok());
    auto style                  = scanStyle();
    style.samplingSpacingMeters = 1.f;
    REQUIRE(network.addEdge(a.value(), b.value(), {{0, 0, 0}, {4, 0, 0}}, 2, 0, style).ok());
    REQUIRE(network.addEdge(b.value(), c.value(), {{4, 0, 0}, {8, 0, 0}}, 2, 0, style).ok());
    auto before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    auto options                       = scanOptions();
    options.maximumSamplingEvaluations = 22;
    REQUIRE(!bakeRoadNetwork(network, options).ok());
    REQUIRE(captureRoadNetwork(network).value() == before.value());
    options.maximumSamplingEvaluations = 40;
    REQUIRE(bakeRoadNetwork(network, options).ok());
}
}  // namespace test_procgen_road_parameter_sampling_unit

namespace test_procgen_road_sampling_unit {
using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
RoadNetwork sampledRoad(float length, bool curve = false) {
    RoadNetwork network;
    auto        a = network.addNode(-length * .5f, 0, 0), b = network.addNode(length * .5f, 0, 0);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    RoadStyle style;
    style.laneWidth                      = 3.f;
    style.crossSection                   = {{-3, 0, RoadMaterial::Asphalt}, {3, 0, RoadMaterial::Asphalt}};
    style.automaticStructures            = false;
    style.samplingSpacingMeters          = 2.f;
    style.samplingPolicy                 = RoadSamplingPolicy::MaximumIntervals;
    style.samplingAngleDegrees           = 4.f;
    std::vector<RoadControlPoint> points = {{-length * .5f, 0, 0}, {0, 0, curve ? 12.f : 0.f}, {length * .5f, 0, 0}};
    REQUIRE(network.addEdge(a.value(), b.value(), points, 2, 0, style).ok());
    return network;
}
RoadBakeOptions isolated() {
    RoadBakeOptions options;
    options.includePiers      = false;
    options.includeJunctions  = false;
    options.includeNavigation = false;
    options.includePlacements = false;
    options.includeMarkings   = false;
    return options;
}
}  // namespace

TEST_CASE("procgen.road.authoring.distanceSamplingStaysContinuousAcrossMarkers") {
    SplinePath path;
    REQUIRE(path.setKindResult("catmullRom").ok());
    for (const auto& p : std::vector<RoadControlPoint>{{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}}) {
        SplinePoint point;
        point.x = p.x;
        point.y = p.y;
        point.z = p.z;
        REQUIRE(path.addPointResult(point).ok());
    }
    auto projected = path.closestPointResult(-8, 2, 8, 24);
    REQUIRE(projected.ok());
    const float distance = projected.value().normalizedDistance * path.lengthResult(24).value();
    for (float offset : {-.001f, .001f}) {
        auto sample = path.evaluateDistanceResult(distance + offset, 24);
        REQUIRE(sample.ok());
        REQUIRE(std::hypot(sample.value().x + 8.f, sample.value().y - 2.f, sample.value().z - 8.f) < .004f);
    }
    auto frames = path.sampleFramesResult(7, true, 0.f, 24, true);
    REQUIRE(frames.ok());
    std::set<float> markers;
    for (const auto& frame : frames.value())
        if (frame.authoredMarker) markers.insert(frame.sample.x);
    const std::set<float> expected = {-24, -8, 8, 24};
    REQUIRE(markers == expected);
}

TEST_CASE("procgen.road.authoring.metricSamplingUsesMetresAnglesAndSectionSubdivisions") {
    auto network = sampledRoad(40);
    auto options = isolated();
    auto first   = bakeRoadNetwork(network, options);
    REQUIRE(first.ok());
    std::set<float> rings;
    for (int i = 0; i < first.value().mesh.getVertexCount(); ++i) rings.insert(first.value().mesh.getPositionX(i));
    REQUIRE(*rings.begin() == -20.f);
    REQUIRE(*rings.rbegin() == 20.f);
    float previous = *rings.begin();
    for (float x : rings) {
        REQUIRE(x - previous <= 2.03f);
        previous = x;
    }
    auto doubled = sampledRoad(80);
    auto longer  = bakeRoadNetwork(doubled, options);
    REQUIRE(longer.ok());
    REQUIRE(longer.value().mesh.getVertexCount() > first.value().mesh.getVertexCount() * 1.8f);
    options.pathSegmentsPerEdge = 96;
    REQUIRE(bakeRoadNetwork(network, options).value().mesh.positions() == first.value().mesh.positions());
    options.samplingScale = 2.f;
    auto coarse           = bakeRoadNetwork(network, options);
    REQUIRE(coarse.ok());
    REQUIRE(coarse.value().mesh.getVertexCount() < first.value().mesh.getVertexCount());
    auto style                     = network.edges()[0].style;
    style.crossSectionSubdivisions = 3;
    REQUIRE(network.setEdgeStyle(1, style).ok());
    options         = isolated();
    auto transverse = bakeRoadNetwork(network, options);
    REQUIRE(transverse.ok());
    REQUIRE(transverse.value().mesh.getVertexCount() == first.value().mesh.getVertexCount() * 3);
    REQUIRE(transverse.value().mesh.getIndexCount() == first.value().mesh.getIndexCount() * 3);
    auto curved                 = sampledRoad(48, true);
    style                       = curved.edges()[0].style;
    style.samplingSpacingMeters = 10.f;
    style.samplingAngleDegrees  = 20.f;
    REQUIRE(curved.setEdgeStyle(1, style).ok());
    auto low = bakeRoadNetwork(curved, options);
    REQUIRE(low.ok());
    style.samplingAngleDegrees = 1.f;
    REQUIRE(curved.setEdgeStyle(1, style).ok());
    auto high = bakeRoadNetwork(curved, options);
    REQUIRE(high.ok());
    REQUIRE(high.value().mesh.getVertexCount() > low.value().mesh.getVertexCount());
    Params params;
    REQUIRE(setRoadRecipeNetwork(params, curved).ok());
    params.setBool("piers", false);
    params.setBool("junctions", false);
    params.setBool("navigation", false);
    params.setBool("placements", false);
    params.setBool("markings", false);
    REQUIRE(bakeRoadNetworkRecipe(params).value().mesh.positions() == high.value().mesh.positions());
}
}  // namespace test_procgen_road_sampling_unit

namespace test_procgen_road_sampling_defaults_unit {
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.newSamplingDefaultsMatchMeasuredReferenceAndLegacyNetworkRetainsCounts") {
    auto created = RoadNetwork::makeStraight(40.f, 2);
    REQUIRE(created.ok());
    auto        network = std::move(created).takeValue();
    const auto& style   = network.edges()[0].style;
    // Actual CreateRoad readbacks before any setter: resolution 2, angle 45, subSegments 1.
    REQUIRE(style.samplingSpacingMeters == 2.f);
    REQUIRE(style.samplingAngleDegrees == 45.f);
    REQUIRE(style.crossSectionSubdivisions == 1);
    REQUIRE(style.samplingPolicy == RoadSamplingPolicy::ParametricDistanceTurn);
    RoadBakeOptions coarse, dense;
    coarse.pathSegmentsPerEdge = 4;
    dense.pathSegmentsPerEdge  = 400;
    auto first = bakeRoadNetwork(network, coarse), second = bakeRoadNetwork(network, dense);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(first.value().mesh.positions() == second.value().mesh.positions());
    REQUIRE(first.value().mesh.indices() == second.value().mesh.indices());
    Params params;
    REQUIRE(setRoadRecipeNetwork(params, network).ok());
    params.setInt("pathSegments", 4);
    auto recipe = bakeRoadNetworkRecipe(params);
    REQUIRE(recipe.ok());
    REQUIRE(recipe.value().mesh.positions() == first.value().mesh.positions());
    auto captured = captureRoadNetwork(network);
    REQUIRE(captured.ok());
    auto& value = captured.value()
                      .getIf<eve::Value::Object>()
                      ->at("edges")
                      .getIf<eve::Value::Array>()
                      ->at(0)
                      .getIf<eve::Value::Object>()
                      ->at("style");
    for (const char* key :
         {"samplingSpacingMeters", "samplingAngleDegrees", "samplingPolicy", "crossSectionSubdivisions"})
        value.getIf<eve::Value::Object>()->erase(key);
    auto restored = restoreRoadNetwork(captured.value());
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].style.samplingSpacingMeters == 0.f);
    REQUIRE(restored.value().edges()[0].style.samplingAngleDegrees == 4.f);
    REQUIRE(restored.value().edges()[0].style.samplingPolicy == RoadSamplingPolicy::MaximumIntervals);
    auto oldCoarse = bakeRoadNetwork(restored.value(), coarse), oldDense = bakeRoadNetwork(restored.value(), dense);
    REQUIRE(oldCoarse.ok());
    REQUIRE(oldDense.ok());
    REQUIRE(oldCoarse.value().mesh.getVertexCount() < oldDense.value().mesh.getVertexCount());
}
}  // namespace test_procgen_road_sampling_defaults_unit

namespace test_procgen_road_surface_path_unit {
using namespace eve::procgen;
using namespace eve::procgen::road;
namespace {
using Point = std::array<float, 3>;
float                    distance(Point a, Point b) { return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]); }
eve::Result<RoadNetwork> curve(RoadSidePathSampling policy) {
    RoadNetwork network;
    auto        a = network.addNode(0, 0, 0), b = network.addNode(0, 0, 30);
    if (!a.ok()) return eve::Result<RoadNetwork>::failure(a.status());
    if (!b.ok()) return eve::Result<RoadNetwork>::failure(b.status());
    RoadStyle style;
    style.laneWidth             = 3;
    style.automaticStructures   = false;
    style.crossSection          = {{-3, 0, RoadMaterial::Asphalt}, {3, 0, RoadMaterial::Asphalt}};
    style.samplingSpacingMeters = 0;
    style.sidePlacement         = {RoadSideReference::Center, 0, .02f, 0, policy};
    auto added = network.addEdge(a.value(), b.value(), {{0, 0, 0}, {6, 0, 15}, {0, 0, 30}}, 1, 1, style);
    if (!added.ok()) return eve::Result<RoadNetwork>::failure(added.status());
    return eve::Result<RoadNetwork>::success(std::move(network));
}
RoadBakeOptions options() {
    RoadBakeOptions result;
    result.pathSegmentsPerEdge = 4;
    result.includeNavigation = result.includeMarkings = false;
    result.sideObjectSpacing                          = 5;
    result.sideObjectClearance                        = 0;
    return result;
}
}  // namespace

TEST_CASE("procgen.road.surfacePath.followsActualMeshCentersAndSegmentDirections") {
    auto made = curve(RoadSidePathSampling::SurfaceSegments);
    REQUIRE(made.ok());
    auto network = std::move(made).takeValue();
    auto knobs   = options();
    auto baked   = bakeRoadNetwork(network, knobs);
    REQUIRE(baked.ok());
    const auto& mesh = baked.value().mesh;
    REQUIRE_EQ(mesh.getVertexCount(), 16);
    std::vector<Point> centers;
    for (int quad = 0; quad < 4; ++quad) {
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j) {
                Point a{mesh.getPositionX(quad * 4 + i), mesh.getPositionY(quad * 4 + i),
                        mesh.getPositionZ(quad * 4 + i)};
                Point b{mesh.getPositionX(quad * 4 + j), mesh.getPositionY(quad * 4 + j),
                        mesh.getPositionZ(quad * 4 + j)};
                if (std::fabs(distance(a, b) - 6) > 1e-4f) continue;
                Point center{(a[0] + b[0]) * .5f, (a[1] + b[1]) * .5f, (a[2] + b[2]) * .5f};
                if (std::none_of(centers.begin(), centers.end(), [&](Point p) { return distance(p, center) < 1e-4f; }))
                    centers.push_back(center);
            }
    }
    std::sort(centers.begin(), centers.end(), [](Point a, Point b) { return a[2] < b[2]; });
    REQUIRE_EQ(centers.size(), 5u);
    std::vector<float> distances{0};
    for (std::size_t i = 1; i < centers.size(); ++i)
        distances.push_back(distances.back() + distance(centers[i - 1], centers[i]));
    const int   intervals = static_cast<int>(std::round(distances.back() / 5));
    const float step      = distances.back() / intervals + .005f;
    int         anchors   = 0;
    for (int i = 0; i < baked.value().placements.getCount(); ++i) {
        const auto& points = baked.value().placements;
        if (points.getStringAttribute(i, "road_role", "") != "side.left") continue;
        const float along   = anchors * step;
        std::size_t segment = 0;
        while (segment + 2 < centers.size() && along > distances[segment + 1]) ++segment;
        const float t = (along - distances[segment]) / (distances[segment + 1] - distances[segment]);
        Point       expected;
        for (int axis = 0; axis < 3; ++axis)
            expected[axis] = std::lerp(centers[segment][axis], centers[segment + 1][axis], t);
        expected[1] += .02f;
        const auto& point = points.points()[i];
        REQUIRE(distance({point.x, point.y, point.z}, expected) < 1e-4f);
        const float yaw =
            std::atan2(centers[segment + 1][0] - centers[segment][0], centers[segment + 1][2] - centers[segment][2]) *
            57.2957795f;
        REQUIRE(std::fabs(point.yaw - yaw) < 1e-4f);
        ++anchors;
    }
    REQUIRE_EQ(anchors, intervals + 1);
    knobs.maximumPlacements = anchors + 2;
    REQUIRE(bakeRoadNetwork(network, knobs).ok());
    --knobs.maximumPlacements;
    REQUIRE(!bakeRoadNetwork(network, knobs).ok());
    Params params;
    params.setInt("pathSegments", 4);
    params.setFloat("sideObjectSpacing", 5);
    params.setFloat("sideObjectClearance", 0);
    params.setBool("navigation", false);
    params.setBool("markings", false);
    REQUIRE(setRoadRecipeNetwork(params, network).ok());
    auto recipe = bakeRoadNetworkRecipe(params);
    REQUIRE(recipe.ok());
    REQUIRE_EQ(recipe.value().placements.getCount(), baked.value().placements.getCount());
    for (int i = 0; i < recipe.value().placements.getCount(); ++i) {
        REQUIRE(std::fabs(recipe.value().placements.points()[i].x - baked.value().placements.points()[i].x) < 1e-6f);
        REQUIRE(std::fabs(recipe.value().placements.points()[i].yaw - baked.value().placements.points()[i].yaw) <
                1e-6f);
    }
    auto analytic = curve(RoadSidePathSampling::Analytic);
    REQUIRE(analytic.ok());
    auto old = bakeRoadNetwork(analytic.value(), options());
    REQUIRE(old.ok());
    REQUIRE_EQ(old.value().mesh.getVertexCount(), mesh.getVertexCount());
    for (int i = 0; i < mesh.getVertexCount(); ++i) {
        REQUIRE_EQ(old.value().mesh.getPositionX(i), mesh.getPositionX(i));
        REQUIRE_EQ(old.value().mesh.getPositionZ(i), mesh.getPositionZ(i));
    }
    REQUIRE(std::fabs(old.value().placements.points()[2].yaw - baked.value().placements.points()[2].yaw) > 1.f);
}

TEST_CASE("procgen.road.surfacePath.sharesScanWorkWithGeometryAndFailsAtomically") {
    auto made = curve(RoadSidePathSampling::SurfaceSegments);
    REQUIRE(made.ok());
    auto network                = std::move(made).takeValue();
    auto style                  = network.edges()[0].style;
    style.samplingSpacingMeters = 2;
    REQUIRE(network.setEdgeStyle(1, style).ok());
    auto knobs              = options();
    knobs.includePlacements = false;
    std::size_t low = 1, high = 4096;
    REQUIRE(bakeRoadNetwork(network, knobs).ok());
    while (low < high) {
        const auto middle                = (low + high) / 2;
        knobs.maximumSamplingEvaluations = middle;
        if (bakeRoadNetwork(network, knobs).ok())
            high = middle;
        else
            low = middle + 1;
    }
    knobs.maximumSamplingEvaluations = low;
    knobs.includePlacements          = true;
    REQUIRE(bakeRoadNetwork(network, knobs).ok());  // A second scan would exceed this exact budget.
    auto before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    knobs.maximumSamplingEvaluations = low - 1;
    REQUIRE(!bakeRoadNetwork(network, knobs).ok());
    REQUIRE(captureRoadNetwork(network).value() == before.value());
}

TEST_CASE("procgen.road.surfacePath.clipsVisibleJunctionIntervalsAndSuppressesOmittedStrips") {
    auto cross = RoadNetwork::makeCross(32, 1);
    REQUIRE(cross.ok());
    for (const auto& edge : std::vector<RoadEdge>(cross.value().edges().begin(), cross.value().edges().end())) {
        auto style                = edge.style;
        style.automaticStructures = false;
        style.crossSection        = {{-3, 0, RoadMaterial::Asphalt}, {3, 0, RoadMaterial::Asphalt}};
        style.laneWidth           = 3;
        style.sidePlacement       = {RoadSideReference::Center, 0, .02f, 0, RoadSidePathSampling::SurfaceSegments};
        REQUIRE(cross.value().setEdgeStyle(edge.id, style).ok());
    }
    auto baked = bakeRoadNetwork(cross.value(), options());
    REQUIRE(baked.ok());
    const auto& mesh = baked.value().mesh;
    REQUIRE(mesh.getVertexCount() >= 4);
    auto from = cross.value().nodeResult(cross.value().edges()[0].from);
    REQUIRE(from.ok());
    Point origin{from.value().x, from.value().y, from.value().z};
    Point firstCenter{};
    float closest = 1e9f;
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j) {
            Point a{mesh.getPositionX(i), mesh.getPositionY(i), mesh.getPositionZ(i)};
            Point b{mesh.getPositionX(j), mesh.getPositionY(j), mesh.getPositionZ(j)};
            if (std::fabs(distance(a, b) - 6) > 1e-4f) continue;
            Point center{(a[0] + b[0]) * .5f, (a[1] + b[1]) * .5f, (a[2] + b[2]) * .5f};
            if (distance(center, origin) < closest) {
                firstCenter = center;
                closest     = distance(center, origin);
            }
        }
    REQUIRE(closest < 1e9f);
    int  sides        = 0;
    bool checkedFirst = false;
    for (int i = 0; i < baked.value().placements.getCount(); ++i) {
        const auto& points = baked.value().placements;
        if (points.getStringAttribute(i, "road_role", "") != "side.left") continue;
        ++sides;
        if (!checkedFirst &&
            points.getIntAttribute(i, "road_edge_id", -1) == static_cast<int>(cross.value().edges()[0].id)) {
            Point expected = firstCenter;
            expected[1] += .02f;
            const auto& point = points.points()[i];
            REQUIRE(distance({point.x, point.y, point.z}, expected) < 1e-4f);
            checkedFirst = true;
        }
    }
    REQUIRE(checkedFirst);
    REQUIRE(sides >= 4);
    RoadNetwork shortRoad;
    auto        a = shortRoad.addNode(0, 0, 0), b = shortRoad.addNode(0, 0, .4f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    RoadStyle style;
    style.automaticStructures = false;
    style.sidePlacement       = {RoadSideReference::Center, 0, .02f, 0, RoadSidePathSampling::SurfaceSegments};
    REQUIRE(shortRoad.addEdge(a.value(), b.value(), {{0, 0, 0}, {0, 0, .4f}}, 1, 1, style).ok());
    auto c = shortRoad.addNode(20, 0, 0), d = shortRoad.addNode(20, 0, 30);
    REQUIRE(c.ok());
    REQUIRE(d.ok());
    REQUIRE(shortRoad.addEdge(c.value(), d.value(), {{20, 0, 0}, {20, 0, 30}}, 1, 1, style).ok());
    auto omitted = bakeRoadNetwork(shortRoad, options());
    REQUIRE(omitted.ok());
    REQUIRE(omitted.value().mesh.getVertexCount() > 0);
    int shortSides = 0;
    for (int i = 0; i < omitted.value().placements.getCount(); ++i) {
        const auto& points = omitted.value().placements;
        if (points.getIntAttribute(i, "road_edge_id", -1) == 1 &&
            points.getStringAttribute(i, "road_role", "") == "side.left")
            ++shortSides;
    }
    REQUIRE_EQ(shortSides, 0);
}

TEST_CASE("procgen.road.surfacePath.asphaltAnchorsFollowLoftedRowOffsets") {
    auto made = curve(RoadSidePathSampling::SurfaceSegments);
    REQUIRE(made.ok());
    auto network    = std::move(made).takeValue();
    auto centerBake = bakeRoadNetwork(network, options());
    REQUIRE(centerBake.ok());
    struct Row {
        Point center, left;
    };
    std::vector<Row> rows;
    const auto&      mesh = centerBake.value().mesh;
    for (int quad = 0; quad < 4; ++quad)
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j) {
                Point a{mesh.getPositionX(quad * 4 + i), mesh.getPositionY(quad * 4 + i),
                        mesh.getPositionZ(quad * 4 + i)};
                Point b{mesh.getPositionX(quad * 4 + j), mesh.getPositionY(quad * 4 + j),
                        mesh.getPositionZ(quad * 4 + j)};
                if (std::fabs(distance(a, b) - 6) > 1e-4f) continue;
                Point center{(a[0] + b[0]) * .5f, (a[1] + b[1]) * .5f, (a[2] + b[2]) * .5f};
                if (std::none_of(rows.begin(), rows.end(),
                                 [&](const Row& row) { return distance(row.center, center) < 1e-4f; }))
                    rows.push_back({center, a[0] < b[0] ? a : b});
            }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.center[2] < b.center[2]; });
    REQUIRE_EQ(rows.size(), 5u);
    std::vector<float> distances{0};
    for (std::size_t i = 1; i < rows.size(); ++i)
        distances.push_back(distances.back() + distance(rows[i - 1].center, rows[i].center));
    auto style                    = network.edges()[0].style;
    style.sidePlacement.reference = RoadSideReference::AsphaltEdges;
    style.sideObjectsRight        = false;
    REQUIRE(network.setEdgeStyle(1, style).ok());
    auto baked = bakeRoadNetwork(network, options());
    REQUIRE(baked.ok());
    int checked = 0;
    for (int i = 0; i < baked.value().placements.getCount(); ++i) {
        const auto& points = baked.value().placements;
        if (points.getStringAttribute(i, "road_role", "") != "side.left") continue;
        float along = points.getFloatAttribute(i, "road_distance", -1);
        REQUIRE(along >= 0);
        std::size_t segment = 0;
        while (segment + 2 < rows.size() && along > distances[segment + 1]) ++segment;
        const float t = (along - distances[segment]) / (distances[segment + 1] - distances[segment]);
        Point       expected{};
        for (int axis = 0; axis < 3; ++axis) {
            expected[axis] =
                std::lerp(rows[segment].center[axis], rows[segment + 1].center[axis], t) +
                std::lerp(rows[segment].left[axis] - rows[segment].center[axis],
                          rows[segment + 1].left[axis] - rows[segment + 1].center[axis], std::clamp(t, 0.f, 1.f));
        }
        expected[1] += .02f;
        const auto& point = points.points()[i];
        REQUIRE(distance({point.x, point.y, point.z}, expected) < 1e-4f);
        ++checked;
    }
    REQUIRE(checked >= 6);
}
}  // namespace test_procgen_road_surface_path_unit
